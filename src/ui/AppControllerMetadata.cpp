#include "AppController_p.h"
#include "core/NodeContracts.h"
#include "ui/DisplayTypes.h"

#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <algorithm>
#include <limits>

namespace visionrt {
namespace workbench {
namespace {
QVariantMap portMetadata(const PortDefinition &port, PortStage stage)
{
    QVariantMap row{
        {"id", static_cast<uint>(port.id)},
        {"name", q(port.nameZh)},
        {"type", static_cast<int>(port.type.type)},
        {"rank", port.type.rank},
        {"default", valueText(port.defaultValue, port.type)},
        {"editor", static_cast<int>(port.editor)},
        {"tab", static_cast<int>(port.tab)},
        {"group", q(port.group)},
        {"minimum", QString{}},
        {"maximum", QString{}},
        {"step", QString{}},
        {"decimals", port.decimals ? *port.decimals : -1},
        {"optionsProvider", static_cast<int>(port.optionsProvider)},
        {"colorOrder", static_cast<int>(port.colorOrder)},
        {"system", isReservedPort(stage, port.id)}
    };
    QVariantMap relations;
    const auto relationRef = [](const PortRef &reference)
    {
        return QVariantMap{{"stage", static_cast<int>(reference.stage)},
                           {"id", static_cast<uint>(reference.id)}};
    };
    if (port.image)
        relations.insert("image", relationRef(*port.image));
    if (port.alignment)
        relations.insert("alignment", relationRef(*port.alignment));
    if (port.sameGeometryAs)
        relations.insert("sameGeometryAs", relationRef(*port.sameGeometryAs));
    if (!relations.isEmpty())
        row.insert("relations", relations);
    if (port.typeFrom)
    {
        row.remove("type");
        row.remove("rank");
        row["default"] = QString{};
        row["typeFrom"] = QVariantMap{
            {"flow", relationRef(port.typeFrom->flowParameter)},
            {"node", relationRef(port.typeFrom->nodeParameter)},
            {"port", relationRef(port.typeFrom->portParameter)}};
    }
    if (port.type.dtype)
    {
        row["dtype"] = static_cast<int>(*port.type.dtype);
    }
    if (port.minimum)
        row["minimum"] = valueText(*port.minimum, port.type);
    if (port.maximum)
        row["maximum"] = valueText(*port.maximum, port.type);
    if (port.step)
        row["step"] = valueText(*port.step, port.type);
    QVariantList options;
    for (const auto &option : port.options) options.push_back(valueText(option, port.type));
    row["options"] = options;
    return row;
}
} // namespace

QVariantMap metadata(const ToolDefinition &definition)
{
    QVariantMap result{
        {            "id",                                                   q(definition.nameEn)},
        {        "nameZh",                                                q(definition.nameZh)},
        {       "version",                                                q(definition.version)},
        {          "kind",                                            static_cast<int>(definition.kind)},
        {      "category",                                q(categoryPath(definition))},
        {       "builtin",                      definition.source.origin == ToolOrigin::Builtin},
        {"definitionPath",                                  pathText(definition.definitionPath)},
        {   "hostBinding",                                            q(definition.hostBinding)},
        {     "nodeModel", static_cast<int>(definition.nodeModel)},
        {    "visibility", static_cast<int>(definition.visibility)},
        {         "entry", QVariantMap{{"package", pathText(definition.entry.package)},
 {"file", pathText(definition.entry.file)},
 {"symbol", q(definition.entry.symbol)}}                             }
    };
    auto ports = [](const auto &values, PortStage stage)
    {
        QVariantList rows;
        for (const auto &port : values) rows.push_back(portMetadata(port, stage));
        return rows;
    };

    result["inputs"]     = ports(definition.inputs, PortStage::Inputs);
    result["initParams"] = ports(definition.initParams, PortStage::Init);
    result["outputs"]    = ports(definition.outputs, PortStage::Outputs);
    return result;
}
} // namespace workbench

QVariantList AppController::toolDefinitions() const
{
    QVariantList result;
    for (const auto &definition : impl_->registry.definitions())
    {
        auto       item         = workbench::metadata(definition);
        const auto availability = impl_->registry.availability(definition.nameEn);
        item["available"]       = availability.state == ToolAvailability::Available;
        item["message"]         = workbench::q(availability.status.message);
        item["detail"]          = workbench::q(availability.status.detail);
        result.push_back(item);
    }
    return result;
}

QVariantMap AppController::toolMetadata(const QString &toolId) const
{
    const auto *definition = impl_->registry.find(toolId.toStdString());
    return definition ? workbench::metadata(*definition) : QVariantMap{};
}
QVariantList AppController::parameters() const
{
    using workbench::q;
    QVariantList result;
    const auto  *node = impl_->currentNode();
    if (!node)
        return result;
    if (!impl_->readsAllowed())
        return impl_->parameterRowsNode == node->id ? impl_->parameterRows : QVariantList{};
    const auto                    *tool = impl_->currentTool();
    // Rows come from the materialised contract, the same definition execution consumes; the raw
    // declaration is still the source for type_from selection options.
    const auto *contract = impl_->effectiveTool();
    auto add = [&](PortId id, const TypeSpec &type, bool initialization, const PortDefinition *declaration)
    {
        PortValue literal = Missing{};
        BindingMode mode = BindingMode::Literal;
        QString source;
        if (initialization)
        {
            const auto found = node->initParams.find(id);
            if (found != node->initParams.end())
                literal = found->second;
        }
        else if (const auto found = node->inputs.find(id); found != node->inputs.end())
        {
            literal = found->second.literal;
            mode = found->second.mode;
            // Switching to manual input cancels the relation immediately; the stored source no
            // longer describes this row because it is not the effective binding.
            if (found->second.mode == BindingMode::Upstream && found->second.source)
            {
                const auto *parent    = impl_->findNode(found->second.source->node);
                const auto *parentDef = parent ? impl_->registry.find(parent->toolId) : nullptr;
                // The display name is copied while its contract is alive: a derived contract is a
                // temporary, so a bare PortDefinition pointer would dangle past this scope.
                QString portName = q(portLabel(found->second.source->port));
                if (parent && parentDef)
                {
                    if (const auto *declared = workbench::port(parentDef->outputs, found->second.source->port))
                        portName = q(declared->nameZh);
                    else
                        try
                        {
                            const auto parentContract = nodeContract(*parent, *parentDef);
                            if (const auto *derived
                                = workbench::port(parentContract.outputs, found->second.source->port))
                                portName = q(derived->nameZh);
                        }
                        catch (const ContractError &)
                        {
                        }
                }
                source = (parent ? q(parent->title()) : QString::number(found->second.source->node)) + "-" + portName;
            }
        }
        QString text;
        try
        {
            text = valueText(literal, type);
        }
        catch (const std::exception &error)
        {
            text = q(error.what());
        }
        QVariantList options;
        const auto   addOption = [&](const QString &label, const QString &value)
        {
            const auto exists = std::any_of(options.begin(), options.end(), [&](const auto &entry)
            { return entry.toMap().value("value").toString() == value; });
            if (!exists) options.push_back(QVariantMap{{"label", label}, {"value", value}});
        };
        if (declaration)
            for (const auto &value : declaration->options)
            {
                const auto *stringValue = std::get_if<std::string>(&value);
                const auto  optionText  = stringValue ? q(*stringValue) : valueText(value, type);
                addOption(optionText, optionText);
            }
        // type_from parameters drive both the source selection and the resolved
        // contract, so their option lists still come from the raw declaration.
        if (initialization && tool)
            for (const auto &output : tool->outputs)
                if (output.typeFrom)
                {
                    const auto &reference = *output.typeFrom;
                    auto selected = [&](PortId parameter) -> QString
                    {
                        const auto found = node->initParams.find(parameter);
                        const auto *value
                            = found == node->initParams.end() ? nullptr : std::get_if<std::string>(&found->second);
                        return value ? q(*value) : QString{};
                    };
                    for (const auto &sourceFlow : impl_->editor.project().flows)
                    {
                        if (sourceFlow.id == impl_->flow.toStdString()) continue;
                        if (id == reference.flowParameter.id) addOption(q(sourceFlow.name), q(sourceFlow.id));
                        if (q(sourceFlow.id) != selected(reference.flowParameter.id)) continue;
                        for (const auto &sourceNode : sourceFlow.nodes)
                        {
                            const auto sequence = QString::number(sourceNode.id);
                            if (id == reference.nodeParameter.id) addOption(q(sourceNode.title()), sequence);
                            if (sequence != selected(reference.nodeParameter.id)) continue;
                            if (id == reference.portParameter.id)
                                for (const auto &[port, signature] : sourceNode.signatures.outputs)
                                    addOption(workbench::q(portLabel(port)), QString::number(port));
                        }
                    }
                }
        const auto *string     = std::get_if<std::string>(&literal);
        const auto  inputFound = node->inputs.find(id);
        const bool  hasSource  = inputFound != node->inputs.end()
                                 && inputFound->second.mode == BindingMode::Upstream
                                 && inputFound->second.source.has_value();
        // Every path parameter opens the same file-or-folder picker, so the editor kind is the
        // declaration itself. A path mode parameter stays a tool-owned value and no longer selects
        // a second picker.
        EditorKind editorKind = declaration ? declaration->editor : EditorKind::Default;

        QVariant rectMap;
        bool hasRect = false;
        QString rectDisplayText = QStringLiteral("[]");
        const bool  rectangleEditor = type.type == Type::ROI && editorKind == EditorKind::Rect;
        const auto *regions         = std::get_if<DataRef<ROI>>(&literal);
        if (rectangleEditor && regions && regions->get().size() == 1 && regions->get()[0].size() == 1)
        {
            const auto &record = regions->get()[0][0];
            try
            {
                const auto rectangle = RegionView(record).rectangle();
                hasRect = true;
                rectMap = QVariantMap{
                    {  "type",         2},
                    {     "x", rectangle.left()},
                    {     "y", rectangle.top()},
                    { "width", rectangle.width()},
                    {"height", rectangle.height()},
                    { "angle", rectangle.angle()}
                };
                rectDisplayText = QString::asprintf("[2, %.6g, %.6g, %.6g, %.6g, %.6g]", rectangle.left(),
                                                    rectangle.top(), rectangle.width(), rectangle.height(),
                                                    rectangle.angle());
            }
            catch (const ContractError &)
            {
                // A non-rectangle in a rect editor is reported by the parameter text below, which
                // still shows the raw value; the map only exists for editable rectangles.
            }
        }
        std::size_t roiCount = 0;
        if (regions)
            for (const auto &group : regions->get()) roiCount += group.size();
        const bool hasRois = roiCount > 0;
        QString arrayText = text;
        if (type.type == Type::Array && (text.isEmpty() || text == QStringLiteral("null")))
            arrayText = QStringLiteral("[]");
        QString displayText;
        if (rectangleEditor)
            displayText = hasRect ? rectDisplayText : QStringLiteral("[]");
        else if (type.type == Type::ROI)
            displayText = hasRois ? QStringLiteral("[%1 个区域]").arg(roiCount) : QStringLiteral("[]");
        else if (type.type == Type::Array)
            displayText = arrayText;
        else if (string)
            displayText = q(*string);
        else
            displayText = text;

        QVariantMap row{
            {             "id",                                                       static_cast<uint>(id)},
            {           "name",       declaration ? q(declaration->nameZh) : q(portLabel(id))},
            {           "type",                                 static_cast<int>(type.type)},
            {     "typeDetail",                                                           typeText(type)},
            {    "typeDisplay",                                                    displayTypeName(type)},
            {           "init",                                                           initialization},
            {        "literal",                         type.type == Type::Array ? arrayText : text},
            {      "valueText",               rectangleEditor && hasRect ? rectDisplayText
               : type.type == Type::Array ? arrayText
               : string                   ? q(*string)
               : text                                                    },
            {    "displayText",                                                              displayText},
            {           "rect",                                                                  rectMap},
            {        "hasRect",                                                                  hasRect},
            {       "roiCount",                                                                 roiCount},
            {        "hasRois",                                                                  hasRois},
            {         "source",                                                                   source},
            {     "sourceNode", hasSource ? QString::number(inputFound->second.source->node) : QString{}},
            {     "sourcePort",     hasSource ? static_cast<uint>(inputFound->second.source->port) : 0u},
            {          "title",                                                         q(node->title())},
            {         "editor",                                                               static_cast<int>(editorKind)},
            {            "tab",       declaration ? static_cast<int>(declaration->tab) : static_cast<int>(ParameterTab::BasicInput)},
            {          "group",                          initialization ? QStringLiteral("初始化配置")
                          : declaration  ? q(declaration->group)
                          : QString{}                                    },
            {        "options",                                                                  options},
            {"optionsProvider",                declaration ? static_cast<int>(declaration->optionsProvider) : static_cast<int>(OptionsProvider::None)},
            {        "minimum",                                                                QString{}},
            {        "maximum",                                                                QString{}},
            {           "step",                                                                QString{}},
            {       "decimals",                                                                       -1},
            {   "canIncrement",                                                                    false},
            {   "canDecrement",                                                                    false},
            {      "inputMode",                                                                    static_cast<int>(mode)}
        };
        if (type.type == Type::Int && (initialization || mode == BindingMode::Literal))
        {
            if (const auto *value = std::get_if<std::int64_t>(&literal))
            {
                std::int64_t stepSize = 1;
                if (declaration && declaration->step)
                {
                    const auto *declared = std::get_if<std::int64_t>(&*declaration->step);
                    if (declared && *declared > 0)
                        stepSize = *declared;
                }
                auto within = [&](std::int64_t candidate)
                {
                    if (declaration && declaration->minimum)
                        if (const auto *minimum = std::get_if<std::int64_t>(&*declaration->minimum);
                            minimum && candidate < *minimum)
                            return false;
                    if (declaration && declaration->maximum)
                        if (const auto *maximum = std::get_if<std::int64_t>(&*declaration->maximum);
                            maximum && candidate > *maximum)
                            return false;
                    return true;
                };
                row["canIncrement"] = *value <= std::numeric_limits<std::int64_t>::max() - stepSize
                                   && within(*value + stepSize);
                row["canDecrement"] = *value >= std::numeric_limits<std::int64_t>::min() + stepSize
                                   && within(*value - stepSize);
            }
        }
        if (declaration)
        {
            if (declaration->minimum)
                row["minimum"] = valueText(*declaration->minimum, type);
            if (declaration->maximum)
                row["maximum"] = valueText(*declaration->maximum, type);
            if (declaration->step)
                row["step"] = valueText(*declaration->step, type);
            if (declaration->decimals)
                row["decimals"] = *declaration->decimals;
        }
        result.push_back(row);
    };

    auto addMeta = [&](int id, const QString &name, EditorKind editorKind,
                       const QString &displayText, bool isSingle = false, const QVariantMap &extras = {})
    {
        QVariantMap row{
            {             "id",                                                                    id},
            {           "name",                                                                  name},
            {           "type",                                                  static_cast<int>(Type::String)},
            {     "typeDetail",                                                  QStringLiteral("String")},
            {    "typeDisplay",                                                  QStringLiteral("宿主配置")},
            {           "init",                                                                 false},
            {        "literal",                                                           displayText},
            {      "valueText",                                                           displayText},
            {    "displayText",                                                           displayText},
            {          "title",                                                      workbench::q(node->title())},
            {         "editor",                                                            static_cast<int>(editorKind)},
            {            "tab",                                                  static_cast<int>(ParameterTab::BasicInput)},
            {          "group",                                                             QString{}},
            {         "isMeta",                                                                  true},
            {"isSingleFormula",                                                              isSingle},
            {        "options",                                                         QVariantList{}},
            {"optionsProvider",                                  static_cast<int>(OptionsProvider::None)},
            {        "minimum",                                                             QString{}},
            {        "maximum",                                                             QString{}},
            {           "step",                                                             QString{}},
            {       "decimals",                                                                    -1},
            {   "canIncrement",                                                                 false},
            {   "canDecrement",                                                                 false},
            {      "inputMode",                                                     static_cast<int>(BindingMode::Literal)}
        };
        for (auto entry = extras.begin(); entry != extras.end(); ++entry)
            row.insert(entry.key(), entry.value());
        result.push_back(row);
    };

    if (contract)
    {
        // Published models are derived from the revision. Before publication the same business
        // parameters remain available for choosing an existing model.
        const auto derivedTrainingPath = [&](PortId id)
        {
            if (!tool->training || node->trainingAsset.empty())
                return false;

            return std::any_of(tool->training->artifacts.begin(), tool->training->artifacts.end(),
                               [&](const TrainingArtifactSpec &artifact)
                               { return artifact.target.stage == PortStage::Init && artifact.target.id == id; });
        };
        for (const auto &port : contract->initParams)
        {
            // Derived initialisation entries are internal presentation sources: the parameter
            // editors own them, so they never appear as editable rows while still being handed to
            // the tool through nodeParameters().
            if (tool->derivesPorts() && isDerivedInitParam(tool->nodeModel, port.id))
                continue;
            if (derivedTrainingPath(port.id))
                continue;
            add(port.id, port.type, true, &port);
        }

        // Explicit execution conditions exist on the logical models only; a fixed
        // tool exposes its real upstream_status input instead.


        if (tool->nodeModel == NodeModel::Expression)
        {
            QString exprText;
            bool isSingle = false;
            if (node->expression)
            {
                if (node->expression->program.size() == 1)
                {
                    exprText = workbench::q(node->expression->program[0].expression);
                    isSingle = true;
                }
                else if (node->expression->program.size() > 1)
                {
                    // The row shows the configured value itself, never a summary of it.
                    QJsonArray program;
                    for (const auto &entry : node->expression->program)
                        program.append(QJsonObject{{"name", workbench::q(entry.name)},
                                                   {"expression", workbench::q(entry.expression)}});
                    exprText = QString::fromUtf8(QJsonDocument(program).toJson(QJsonDocument::Compact));
                    isSingle = false;
                }
                else
                {
                    exprText = QString{};
                    isSingle = true;
                }
            }
            else
            {
                exprText = QString{};
                isSingle = true;
            }
            addMeta(workbench::kExpressionRow, QStringLiteral("表达式"), EditorKind::Expression,
                    exprText, isSingle);
        }
        else if (tool->nodeModel == NodeModel::Condition)
        {
            QString condText;
            if (node->expression && !node->expression->program.empty())
                condText = workbench::q(node->expression->program[0].expression);
            const bool       upstream = conditionUsesUpstream(*node);
            const auto       found    = node->inputs.find(kConditionInputPort);
            const bool       bound    = found != node->inputs.end() && found->second.source.has_value();
            QVariantMap      extras{{"inputMode", static_cast<int>(upstream ? BindingMode::Upstream : BindingMode::Literal)},
                                     {"bindingPort", static_cast<uint>(kConditionInputPort)}};
            if (bound)
            {
                const auto &source = *found->second.source;
                const auto *parent = impl_->findNode(source.node);
                QString     name   = workbench::q(portLabel(source.port));
                if (parent)
                {
                    const auto *parentTool = impl_->registry.find(parent->toolId);
                    if (parentTool)
                        try
                        {
                            const auto resolved = nodeContract(*parent, *parentTool);
                            if (const auto *port = workbench::port(resolved.outputs, source.port))
                                name = workbench::q(port->nameZh);
                        }
                        catch (const ContractError &)
                        {
                            name = workbench::q(portLabel(source.port));
                        }
                    extras.insert("source", workbench::q(parent->title()) + "-" + name);
                }
                extras.insert("sourceNode", QString::number(source.node));
                extras.insert("sourcePort", static_cast<uint>(source.port));
            }
            addMeta(workbench::kConditionRow, QStringLiteral("条件公式"),
                    upstream ? EditorKind::ConditionUpstream : EditorKind::Condition, condText, true,
                    extras);
        }
        else if (tool->nodeModel == NodeModel::Select)
        {
            QString selectText = node->selectType ? displayTypeName(*node->selectType) : QString{};
            addMeta(workbench::kSelectTypeRow, QStringLiteral("合流类型"), EditorKind::Select,
                    selectText, false);
        }

        if (contract)
            for (const auto &port : contract->inputs)
            {
                // A condition node maps its single condition input onto the dedicated condition row;
                // enumerating the raw port again would show the same value twice.
                if (tool && tool->nodeModel == NodeModel::Condition && port.id == kConditionInputPort)
                    continue;
                add(port.id, port.type, false, &port);
            }
    }
    else
    {
        for (const auto &[id, type] : node->signatures.initParams)
        {
            if (tool && tool->derivesPorts() && isDerivedInitParam(tool->nodeModel, id))
                continue;
            add(id, type, true, nullptr);
        }

        for (const auto &[id, type] : node->signatures.inputs) add(id, type, false, nullptr);
    }
    impl_->parameterRows     = result;
    impl_->parameterRowsNode = node->id;
    return result;
}

} // namespace visionrt
