#include "core/NodeContracts.h"
#include "core/ValueJson.h"
#include "ui/DisplayTypes.h"
#include "ui/OutputModel.h"
#include "AppController_p.h"

#include <algorithm>
#include <optional>

namespace visionrt {
namespace {
TypeSpec typeFromMap(const QVariantMap &row)
{
    QJsonObject type{{"type", row.value("type").toInt()}};
    if (row.value("type").toInt() == static_cast<int>(Type::Array)) {
        type.insert("dtype", row.value("dtype").toInt()); type.insert("rank", row.value("rank").toInt());
    }
    return ValueJson::decodeTypeToken(type);
}
QVariantMap typeMap(const TypeSpec &type)
{
    return ValueJson::encodeTypeToken(type).toVariantMap();
}
} // namespace



NodeModel AppController::nodeModel() const
{
    const auto *node = impl_->currentNode();
    const auto *tool = impl_->currentTool();
    return node && tool ? tool->nodeModel : NodeModel::Fixed;
}

BindingMode AppController::conditionMode() const
{
    const auto *node = impl_->currentNode();
    const auto *tool = impl_->currentTool();
    if (!node || !tool || tool->nodeModel != NodeModel::Condition)
        return BindingMode::Literal;
    return conditionUsesUpstream(*node) ? BindingMode::Upstream : BindingMode::Literal;
}

QVariantMap AppController::expressionConfig(const QString &nodeId) const
{
    const auto *node = impl_->findNode(workbench::nodeId(nodeId));
    const auto *tool = impl_->currentTool();
    if (!node || !tool || !tool->derivesPorts())
        return {};
    if (node->toolId != tool->nameEn)
        return {};
    const ExpressionConfig config = node->expression.value_or(ExpressionConfig{});
    QVariantList           variables;
    for (const auto &variable : config.variables)
    {
        auto row = typeMap(variable.type);
        row.insert("portId", static_cast<uint>(variable.portId));
        row.insert("name", workbench::q(variable.name));
        row.insert("symbol", workbench::q(variable.symbol));
        row.insert("allowMissing", variable.allowMissing);
        variables.push_back(row);
    }
    QVariantList program;
    for (const auto &result : config.program)
    {
        auto row = typeMap(result.type);
        row.insert("portId", static_cast<uint>(result.portId));
        row.insert("name", workbench::q(result.name));
        row.insert("symbol", workbench::q(result.symbol));
        row.insert("expression", workbench::q(result.expression));
        row.insert("exposed", result.exposed);
        program.push_back(row);
    }
    return {{"node", nodeId},
            {"model", static_cast<int>(tool->nodeModel)},
            {"nextPortId", static_cast<qulonglong>(config.nextPortId)},
            {"variables", variables},
            {"program", program}};
}

bool AppController::applyExpressionConfig(const QString &nodeId, const QVariantList &variables,
                                          const QVariantList &program)
{
    const auto *tool = impl_->currentTool();
    const auto  id   = workbench::nodeId(nodeId);
    if (!tool || tool->nodeModel == NodeModel::Fixed || tool->nodeModel == NodeModel::Select)
        return false;
    if (!impl_->idle())
        return false;
    // The allocator only moves forward: committed identifiers are never recycled, even when the
    // highest one was deleted in this very transaction.
    // Business outputs start after the host status ports 0 and 1.
    std::uint64_t sequenceFloor = 2;
    if (const auto *existing = impl_->findNode(id); existing && existing->expression)
        sequenceFloor = existing->expression->nextPortId;
    bool ok = impl_->command(
        [&]
        {
            ExpressionConfig config;
            std::uint64_t    next = sequenceFloor;
            for (const auto &item : variables)
            {
                const auto row = item.toMap();
                ExpressionVariable variable;
                variable.portId     = static_cast<PortId>(row.value("portId").toUInt());
                variable.name       = row.value("name").toString().toStdString();
                variable.symbol     = row.value("symbol").toString().toStdString();
                variable.type       = typeFromMap(row);
                variable.allowMissing = row.value("allowMissing").toBool();
                next = std::max(next, static_cast<std::uint64_t>(variable.portId) + 1);
                config.variables.push_back(std::move(variable));
            }
            for (const auto &item : program)
            {
                const auto row = item.toMap();
                ExpressionResult result;
                result.portId     = static_cast<PortId>(row.value("portId").toUInt());
                result.name       = row.value("name").toString().toStdString();
                result.symbol     = row.value("symbol").toString().toStdString();
                result.expression = row.value("expression").toString().toStdString();
                result.type       = typeFromMap(row);
                result.exposed    = row.value("exposed").toBool();
                next = std::max(next, static_cast<std::uint64_t>(result.portId) + 1);
                config.program.push_back(std::move(result));
            }
            config.nextPortId = next;
            return impl_->editor.setExpressionConfig(impl_->flow.toStdString(), id, std::move(config), *tool);
        },
        true);
    if (ok)
    {
        impl_->publishNodesChanged();
        emit parametersChanged();
    }
    return ok;
}

bool AppController::applySingleExpression(const QString &nodeId, const QString &text)
{
    const auto  id   = workbench::nodeId(nodeId);
    const auto *node = impl_->findNode(id);
    const auto *tool = impl_->currentTool();
    if (!node || !tool || !tool->derivesPorts())
        return false;
    if (!impl_->idle())
        return false;

    // Business outputs start after the host status ports 0 and 1.
    std::uint64_t sequenceFloor = 2;
    if (node->expression)
        sequenceFloor = node->expression->nextPortId;

    bool ok = impl_->command(
        [&]
        {
            ExpressionConfig config = node->expression.value_or(ExpressionConfig{});
            config.nextPortId       = sequenceFloor;
            if (config.program.empty())
            {
                ExpressionResult result;
                result.portId     = static_cast<PortId>(config.nextPortId++);
                result.name       = tool->nodeModel == NodeModel::Condition ? "条件" : "结果1";
                result.symbol     = tool->nodeModel == NodeModel::Condition ? "c" : "r1";
                result.type       = tool->nodeModel == NodeModel::Condition ? TypeSpec{Type::Bool} : TypeSpec{Type::Float};
                result.exposed    = tool->nodeModel != NodeModel::Condition;
                result.expression = text.toUtf8().toStdString();
                config.program.push_back(std::move(result));
            }
            else
            {
                config.program[0].expression = text.toUtf8().toStdString();
            }
            return impl_->editor.setExpressionConfig(impl_->flow.toStdString(), id, std::move(config), *tool);
        },
        true);
    if (ok)
    {
        impl_->publishNodesChanged();
        emit parametersChanged();
    }
    return ok;
}



bool AppController::setConditionMode(const QString &nodeId, bool upstream)
{
    const auto id = workbench::nodeId(nodeId);
    if (!impl_->idle())
        return false;
    const auto *tool = impl_->currentTool();
    if (!tool || tool->nodeModel != NodeModel::Condition)
        return false;
    const bool ok = impl_->command(
        [&]
        {
            return impl_->editor.setConditionMode(impl_->flow.toStdString(), id,
                                                  upstream ? BindingMode::Upstream : BindingMode::Literal, *tool);
        },
        true);
    if (ok)
    {
        impl_->publishNodesChanged();
        emit parametersChanged();
    }
    return ok;
}





QVariantMap AppController::selectType(const QString &nodeId) const
{
    const auto *node = impl_->findNode(workbench::nodeId(nodeId));
    if (!node || !node->selectType)
        return {};
    return typeMap(*node->selectType);
}

bool AppController::applySelectType(const QString &nodeId, int type, int dtype, int rank)
{
    const auto *tool = impl_->currentTool();
    if (!tool || tool->nodeModel != NodeModel::Select || !impl_->idle())
        return false;
    const bool ok = impl_->command(
        [&]
        {
            const QVariantMap row{{"type", type}, {"dtype", dtype}, {"rank", rank}};
            return impl_->editor.setSelectType(impl_->flow.toStdString(), workbench::nodeId(nodeId),
                                               typeFromMap(row), *tool);
        },
        true);
    if (ok)
    {
        impl_->publishNodesChanged();
        emit parametersChanged();
    }
    return ok;
}

QString AppController::typeDescription(int type, int dtype, int rank) const
{
    TypeSpec spec{static_cast<Type>(type)};
    if (spec.type == Type::Array) {
        if (dtype >= 0) spec.dtype = static_cast<DType>(dtype);
        spec.rank = rank;
    }
    return displayTypeName(spec);
}

QVariantList AppController::parameterTypes() const
{
    QVariantList result;
    for (const auto type : kTypeValues)
        result.push_back(QVariantMap{{"name", static_cast<int>(type)}, {"label", QString::fromUtf8(typeLabel(type))}});
    return result;
}

QVariantList AppController::arrayElementTypes() const
{
    QVariantList result;
    for (const auto type : kDTypeValues)
        result.push_back(QVariantMap{{"name", static_cast<int>(type)}, {"label", displayDtypeName(type)}});
    return result;
}

QString AppController::dtypeDescription(int dtype) const
{
    return displayDtypeName(static_cast<DType>(dtype));
}
} // namespace visionrt
