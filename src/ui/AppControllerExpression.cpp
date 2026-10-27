#include "core/NodeContracts.h"
#include "ui/DisplayTypes.h"
#include "ui/OutputModel.h"
#include "ui/AppController_p.h"

#include <algorithm>
#include <optional>

namespace visionrt {
namespace {
const char *modelName(NodeModel model)
{
    switch (model)
    {
    case NodeModel::Expression:
        return "expression";
    case NodeModel::Condition:
        return "condition";
    case NodeModel::Select:
        return "select";
    case NodeModel::Fixed:
        break;
    }
    return "fixed";
}

Type typeFromName(const QString &name)
{
    for (int index = 0; index <= static_cast<int>(Type::ROI); ++index)
        if (name == QString::fromLatin1(typeName(static_cast<Type>(index))))
            return static_cast<Type>(index);
    throw ContractError("未知类型：" + name.toStdString());
}

TypeSpec typeFromMap(const QVariantMap &row)
{
    TypeSpec spec;
    spec.type = typeFromName(row.value("type").toString());
    if (spec.type == Type::Array)
    {
        const auto dtype = row.value("dtype").toString();
        if (dtype.isEmpty())
            throw ContractError("数组类型需要元素类型");
        for (int index = 0; index <= static_cast<int>(DType::String); ++index)
            if (dtype == QString::fromLatin1(dtypeName(static_cast<DType>(index))))
            {
                spec.dtype = static_cast<DType>(index);
                break;
            }
        if (!spec.dtype)
            throw ContractError("未知数组元素类型：" + dtype.toStdString());
        spec.rank = row.value("rank").toInt();
    }
    validateTypeSpec(spec);
    return spec;
}

QVariantMap typeMap(const TypeSpec &type)
{
    return {{"type", QString::fromLatin1(typeName(type.type))},
            {"dtype", type.dtype ? QString::fromLatin1(dtypeName(*type.dtype)) : QString{}},
            {"rank", type.rank}};
}
} // namespace

QString AppController::nodeModel() const
{
    const auto *node = impl_->currentNode();
    const auto *tool = impl_->currentTool();
    return node && tool ? QString::fromLatin1(modelName(tool->nodeModel)) : QStringLiteral("fixed");
}

QString AppController::executionGuardText() const
{
    const auto *node = impl_->currentNode();
    if (!node || !node->executionGuard || node->executionGuard->mode != BindingMode::Upstream
        || !node->executionGuard->source)
        return {};
    const auto &guardSource = *node->executionGuard->source;
    const auto *source   = impl_->findNode(guardSource.node);
    const auto *tool     = source ? impl_->registry.find(source->toolId) : nullptr;
    QString     portName = QString::fromStdString(guardSource.port);
    if (source && tool)
    {
        const PortDefinition *port = workbench::port(tool->outputs, guardSource.port);
        // The materialized contract owns the port storage, so it must outlive every read of `port`.
        std::optional<ToolDefinition> contract;
        if (!port)
            try
            {
                contract = nodeContract(*source, *tool);
                port     = workbench::port(contract->outputs, guardSource.port);
            }
            catch (const ContractError &)
            {
                port = nullptr;
            }
        if (port)
            portName = workbench::q(port->name);
    }
    return (source ? workbench::q(source->title()) : QString::number(guardSource.node)) + "-" + portName;
}

QString AppController::conditionMode() const
{
    const auto *node = impl_->currentNode();
    const auto *tool = impl_->currentTool();
    if (!node || !tool || tool->nodeModel != NodeModel::Condition)
        return {};
    return QString::fromStdString(conditionModeText(*node));
}

bool AppController::executionGuardActive() const
{
    const auto *node = impl_->currentNode();
    const auto *tool = impl_->currentTool();
    return node && tool && supportsExecutionGuard(tool->nodeModel) && node->executionGuard.has_value()
        && node->executionGuard->mode == BindingMode::Upstream;
}

QString AppController::selectTypeText() const
{
    const auto *node = impl_->currentNode();
    if (!node || !node->selectType)
        return {};
    return visionrt::typeText(*node->selectType);
}

QVariantMap AppController::expressionConfig(const QString &nodeId) const
{
    const auto *node = impl_->findNode(workbench::nodeId(nodeId));
    const auto *tool = impl_->currentTool();
    if (!node || !tool || !tool->derivesPorts())
        return {};
    if (node->toolId != tool->id)
        return {};
    const ExpressionConfig config = node->expression.value_or(ExpressionConfig{});
    QVariantList           variables;
    for (const auto &variable : config.variables)
    {
        auto row = typeMap(variable.type);
        row.insert("portId", workbench::q(variable.portId));
        row.insert("name", workbench::q(variable.name));
        row.insert("symbol", workbench::q(variable.symbol));
        row.insert("allowMissing", variable.allowMissing);
        variables.push_back(row);
    }
    QVariantList program;
    for (const auto &result : config.program)
    {
        auto row = typeMap(result.type);
        row.insert("portId", workbench::q(result.portId));
        row.insert("name", workbench::q(result.name));
        row.insert("symbol", workbench::q(result.symbol));
        row.insert("expression", workbench::q(result.expression));
        row.insert("exposed", result.exposed);
        program.push_back(row);
    }
    return {{"node", nodeId},
            {"model", QString::fromLatin1(modelName(tool->nodeModel))},
            {"nextPortSeq", static_cast<qulonglong>(config.nextPortSeq)},
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
    std::uint64_t sequenceFloor = 1;
    if (const auto *existing = impl_->findNode(id); existing && existing->expression)
        sequenceFloor = existing->expression->nextPortSeq;
    bool ok = impl_->command(
        [&]
        {
            ExpressionConfig config;
            std::uint64_t    next = sequenceFloor;
            for (const auto &item : variables)
            {
                const auto row = item.toMap();
                ExpressionVariable variable;
                variable.portId     = row.value("portId").toString().toStdString();
                variable.name       = row.value("name").toString().toStdString();
                variable.symbol     = row.value("symbol").toString().toStdString();
                variable.type       = typeFromMap(row);
                variable.allowMissing = row.value("allowMissing").toBool();
                next = std::max(next, static_cast<std::uint64_t>(row.value("portId").toString().mid(1).toULongLong()) + 1);
                config.variables.push_back(std::move(variable));
            }
            for (const auto &item : program)
            {
                const auto row = item.toMap();
                ExpressionResult result;
                result.portId     = row.value("portId").toString().toStdString();
                result.name       = row.value("name").toString().toStdString();
                result.symbol     = row.value("symbol").toString().toStdString();
                result.expression = row.value("expression").toString().toStdString();
                result.type       = typeFromMap(row);
                result.exposed    = row.value("exposed").toBool();
                next = std::max(next, static_cast<std::uint64_t>(row.value("portId").toString().mid(1).toULongLong()) + 1);
                config.program.push_back(std::move(result));
            }
            config.nextPortSeq = next;
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

    std::uint64_t sequenceFloor = 1;
    if (node->expression)
        sequenceFloor = node->expression->nextPortSeq;

    bool ok = impl_->command(
        [&]
        {
            ExpressionConfig config = node->expression.value_or(ExpressionConfig{});
            config.nextPortSeq      = sequenceFloor;
            if (config.program.empty())
            {
                ExpressionResult result;
                result.portId     = "p" + std::to_string(config.nextPortSeq++);
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

QVariantList AppController::guardCandidates(const QString &nodeId) const
{
    QVariantList result;
    const auto   id = workbench::nodeId(nodeId);
    const auto  *target = impl_->findNode(id);
    if (!target)
        return result;
    for (const auto &flow : impl_->editor.project().flows)
    {
        if (flow.id != impl_->flow.toStdString())
            continue;
        {
            for (const auto &node : flow.nodes)
            {
                const auto *tool = impl_->registry.find(node.toolId);
                if (!tool || node.id == id || node.position.y >= target->position.y)
                    continue;
                const auto contract = nodeContract(node, *tool);
                for (const auto &port : contract.outputs)
                {
                    if (port.type != TypeSpec{Type::Bool})
                        continue;
                    const bool selected = target->executionGuard && target->executionGuard->source
                                       && target->executionGuard->source->node == node.id
                                       && target->executionGuard->source->port == port.id;
                    result.push_back(QVariantMap{{"node", QString::number(node.id)},
                                                 {"title", workbench::q(node.title())},
                                                 {"port", workbench::q(port.id)},
                                                 {"portName", workbench::q(port.name)},
                                                 {"selected", selected}});
                }
            }
        }
    }
    return result;
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

bool AppController::setExecutionGuardMode(const QString &nodeId, bool upstream)
{
    const auto id = workbench::nodeId(nodeId);
    if (!impl_->idle())
        return false;
    const auto *tool = impl_->registry.find(impl_->currentNode() ? impl_->currentNode()->toolId : std::string{});
    if (!tool || !supportsExecutionGuard(tool->nodeModel))
        return false;
    // The mode is the only thing this transaction changes; a retained source stays
    // available for switching back.
    std::optional<ExecutionGuard> guard;
    const auto *current = impl_->currentNode();
    if (current && current->executionGuard)
        guard = current->executionGuard;
    else if (upstream)
        guard = ExecutionGuard{};
    if (guard)
        guard->mode = upstream ? BindingMode::Upstream : BindingMode::Literal;
    const bool ok = impl_->command(
        [&]
        {
            return impl_->editor.setExecutionGuard(impl_->flow.toStdString(), id, std::move(guard), *tool);
        },
        true);
    if (ok)
    {
        impl_->publishNodesChanged();
        emit parametersChanged();
    }
    return ok;
}

bool AppController::applyExecutionGuard(const QString &nodeId, const QString &sourceNode, const QString &port)
{
    const auto id = workbench::nodeId(nodeId);
    if (!impl_->idle())
        return false;
    const auto *tool = impl_->currentTool();
    if (!tool || !supportsExecutionGuard(tool->nodeModel))
        return false;
    std::optional<ExecutionGuard> guard;
    if (!sourceNode.isEmpty())
    {
        guard = ExecutionGuard{SourcePort{workbench::nodeId(sourceNode), port.toStdString()}};
        guard->mode = BindingMode::Upstream;
    }
    else if (const auto *current = impl_->currentNode(); current && current->executionGuard)
    {
        // Unbinding drops the source only; the node stays in upstream mode and the mode
        // highlight must not be replaced by a colour that pretends the mode changed.
        guard         = current->executionGuard;
        guard->source.reset();
        guard->mode = BindingMode::Upstream;
    }
    const bool ok = impl_->command(
        [&]
        {
            return impl_->editor.setExecutionGuard(impl_->flow.toStdString(), id, std::move(guard), *tool);
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

bool AppController::applySelectType(const QString &nodeId, const QString &type, const QString &dtype, int rank)
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

QString AppController::typeDescription(const QString &type, const QString &dtype, int rank) const
{
    TypeSpec spec;
    try
    {
        spec = typeFromMap(QVariantMap{{"type", type}, {"dtype", dtype}, {"rank", rank}});
    }
    catch (const ContractError &)
    {
        return {};
    }
    return displayTypeName(spec);
}
} // namespace visionrt
