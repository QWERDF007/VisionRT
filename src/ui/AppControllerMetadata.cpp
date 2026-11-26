#include "AppController_p.h"
#include "core/NodeContracts.h"
#include "core/ValueJson.h"
#include "ui/DisplayTypes.h"
#include "ui/ValueDisplay.h"

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
    const auto  relationRef = [](const PortRef &reference)
    {
        return QVariantMap{
            {"stage", static_cast<int>(reference.stage)},
            {   "id",   static_cast<uint>(reference.id)}
        };
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
        row["default"]  = QString{};
        row["typeFrom"] = QVariantMap{
            {"flow", relationRef(port.typeFrom->flowParameter)},
            {"node", relationRef(port.typeFrom->nodeParameter)},
            {"port", relationRef(port.typeFrom->portParameter)}
        };
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
    for (const auto &option : port.options)
        options.push_back(QVariantMap{{"value", valueText(option.value, port.type)}, {"label", q(option.label)}});
    row["options"] = options;
    return row;
}
} // namespace

QVariantMap metadata(const ToolDefinition &definition)
{
    QVariantMap result{
        {            "id",                                                 q(definition.nameEn)},
        {        "nameZh",                                                 q(definition.nameZh)},
        {       "version",                                                q(definition.version)},
        {          "kind",                                    static_cast<int>(definition.kind)},
        {      "category",                                          q(categoryPath(definition))},
        {       "builtin",                      definition.source.origin == ToolOrigin::Builtin},
        {"definitionPath",                                  pathText(definition.definitionPath)},
        {   "hostBinding",                                            q(definition.hostBinding)},
        {     "nodeModel",                               static_cast<int>(definition.nodeModel)},
        {    "visibility",                              static_cast<int>(definition.visibility)},
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
    result["trainParams"] = ports(definition.trainParams, PortStage::Train);
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

std::vector<ParameterListModel::Row> AppController::Impl::buildParameterRows() const
{
    using workbench::q;
    std::vector<ParameterListModel::Row> result;
    const auto                          *node = currentNode();
    if (!node)
        return result;
    const auto  nodeIdText = QString::number(node->id);
    const auto *tool       = currentTool();
    // Rows come from the materialised contract, the same definition execution consumes; the raw
    // declaration is still the source for type_from selection options.
    const auto *contract = effectiveTool();
    auto        add      = [&](PortId id, const TypeSpec &type, PortStage stage, const PortDefinition *declaration, bool recordScoped = false)
    {
        const bool initialization = stage == PortStage::Init;
        const bool trainingParameter = stage == PortStage::Train;
        PortValue   literal = Missing{};
        BindingMode mode    = BindingMode::Literal;
        QString     source;
        if (trainingParameter)
        {
            if (!recordScoped)
            {
                const auto found = training->parameter(id);
                if (found) literal = *found;
            }
            else if (training && training->selected() >= 0)
            {
                if (const auto value = training->value(training->selected(), id)) literal = *value;
            }
        }
        else if (initialization)
        {
            const auto found = node->initParams.find(id);
            if (found != node->initParams.end())
                literal = found->second;
        }
        else if (const auto found = node->inputs.find(id); found != node->inputs.end())
        {
            literal = found->second.literal;
            mode    = found->second.mode;
            // Switching to manual input cancels the relation immediately; the stored source no
            // longer describes this row because it is not the effective binding.
            if (found->second.mode == BindingMode::Upstream && found->second.source)
            {
                const auto *parent    = findNode(found->second.source->node);
                const auto *parentDef = parent ? registry.find(parent->toolId) : nullptr;
                // The display name is copied while its contract is alive: a derived contract is a
                // temporary, so a bare PortDefinition pointer would dangle past this scope.
                QString     portName = q(portLabel(found->second.source->port));
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
        if (!recordScoped && roiSession.active() && roiSession.parameterStage() == stage
            && roiSession.original().mode == BindingMode::Literal && roiSession.node() == node->id
            && roiSession.input() == id)
            literal = roiSession.draft();
        QString text;
        QString formattedText;
        try
        {
            formattedText = displayValueText(literal, type);
            text = type.type == Type::Image || type.type == Type::TrainingSet ? formattedText : valueText(literal, type);
        }
        catch (const std::exception &error)
        {
            text = q(error.what());
            formattedText = text;
        }
        QVariantList options;
        const auto   addOption = [&](const QString &label, const QString &value)
        {
            const auto exists = std::any_of(options.begin(), options.end(), [&](const auto &entry)
                                            { return entry.toMap().value("value").toString() == value; });
            if (!exists)
                options.push_back(QVariantMap{
                    {"label", label},
                    {"value", value}
                });
        };
        if (declaration)
            for (const auto &option : declaration->options)
            {
                const auto &value = option.value;
                const auto *stringValue = std::get_if<std::string>(&value);
                const auto  optionText  = stringValue ? q(*stringValue) : valueText(value, type);
                addOption(option.label.empty() ? (stringValue ? optionText : displayValueText(value, type)) : q(option.label), optionText);
            }
        // type_from parameters drive both the source selection and the resolved
        // contract, so their option lists still come from the raw declaration.
        if (initialization && tool)
            for (const auto &output : tool->outputs)
                if (output.typeFrom)
                {
                    const auto &reference = *output.typeFrom;
                    auto        selected  = [&](PortId parameter) -> QString
                    {
                        const auto  found = node->initParams.find(parameter);
                        const auto *value
                            = found == node->initParams.end() ? nullptr : std::get_if<std::string>(&found->second);
                        return value ? q(*value) : QString{};
                    };
                    for (const auto &sourceFlow : editor.project().flows)
                    {
                        if (sourceFlow.id == flow.toStdString())
                            continue;
                        if (id == reference.flowParameter.id)
                            addOption(q(sourceFlow.name), q(sourceFlow.id));
                        if (q(sourceFlow.id) != selected(reference.flowParameter.id))
                            continue;
                        for (const auto &sourceNode : sourceFlow.nodes)
                        {
                            const auto sequence = QString::number(sourceNode.id);
                            if (id == reference.nodeParameter.id)
                                addOption(q(sourceNode.title()), sequence);
                            if (sequence != selected(reference.nodeParameter.id))
                                continue;
                            if (id == reference.portParameter.id)
                                for (const auto &[port, signature] : sourceNode.signatures.outputs)
                                    addOption(workbench::q(portLabel(port)), QString::number(port));
                        }
                    }
                }
        const auto *string     = std::get_if<std::string>(&literal);
        const auto  inputFound = node->inputs.find(id);
        const bool  hasSource  = !trainingParameter && inputFound != node->inputs.end() && inputFound->second.mode == BindingMode::Upstream
                            && inputFound->second.source.has_value();
        EditorKind editorKind = declaration ? declaration->editor : EditorKind::Default;

        const auto *regions = shapeRegions(literal);
        const bool rectangleEditor = type.type == Type::Rectangle && editorKind == EditorKind::Rect;
        const bool hasRect = rectangleEditor && regions && regions->size() == 1 && regions->front().size() == 1;
        std::size_t roiCount = 0;
        if (regions)
            for (const auto &group : (*regions)) roiCount += group.size();
        const bool  hasRois      = roiCount > 0;
        const int roiDrawShape = ImageViewport::drawShapeForType(type.type);
        QString arrayText = text;
        if (type.type == Type::Array && (text.isEmpty() || text == QStringLiteral("null")))
            arrayText = QStringLiteral("[]");
        QString displayText;
        if (rectangleEditor)
            displayText = hasRect ? formattedText : QStringLiteral("[]");
        else if (isShapeType(type.type))
            displayText = formattedText;
        else if (type.type == Type::Array)
            displayText = arrayText == QStringLiteral("[]") ? arrayText : formattedText;
        else if (string)
            displayText = q(*string);
        else
            displayText = formattedText;

        QString editText;
        if (rectangleEditor)
            editText = hasRect ? text : QStringLiteral("[]");
        else if (type.type == Type::Array)
            editText = arrayText;
        else if (string)
            editText = q(*string);
        else if (type.type == Type::Float)
            editText = isMissing(literal) ? QString{} : text;
        else
            editText = text;

        const QVariant datasetId = training && training->datasetParameter() ? QVariant::fromValue(*training->datasetParameter()) : QVariant{};
        const QString  recordId  = recordScoped ? this->q->selectedTrainingDraftRecord() : QString{};
        const QString  key       = ParameterListModel::makeKey(nodeIdText, static_cast<int>(stage), recordScoped,
                                                               datasetId, recordId, static_cast<int>(id));

        QVariantMap row{
            {"key", key},
            {"roiDrawShape", roiDrawShape},
            {"isShape", isShapeType(type.type)},
            {"emptyLiteral", type.type == Type::TrainingSet ? ValueJson::text(defaultValue(type), type) : QStringLiteral("[]")},
            {"id", static_cast<uint>(id)},
            {"stage", static_cast<int>(stage)},
            {"recordScoped", recordScoped},
            {"datasetEditor", trainingParameter && !recordScoped && tool && tool->training && std::ranges::any_of(tool->training->datasets, [&](const auto &dataset) { return dataset.parameter == id; })},
            {"datasetId", datasetId},
            {"recordId", recordId},
            {"name", declaration ? q(declaration->nameZh) : q(portLabel(id))},
            {"type", static_cast<int>(type.type)},
            {"typeDetail", typeText(type)},
            {"typeDisplay", displayTypeName(type)},
            {"init", initialization},
            {"literal", type.type == Type::Array ? arrayText : text},
            {"valueText", rectangleEditor && hasRect ? formattedText
                          : type.type == Type::Array ? arrayText
                          : string                   ? q(*string)
                                                     : text},
            {"displayText", displayText},
            {"editText", editText},
            {"hasRect", hasRect},
            {"roiCount", roiCount},
            {"hasRois", hasRois},
            {"source", source},
            {"sourceNode", hasSource ? QString::number(inputFound->second.source->node) : QString{}},
            {"sourcePort", hasSource ? static_cast<uint>(inputFound->second.source->port) : 0u},
            {"title", q(node->title())},
            {"editor", static_cast<int>(editorKind)},
            {"tab", static_cast<int>(trainingParameter ? ParameterTab::Training : initialization ? ParameterTab::Initialization : declaration ? declaration->tab : ParameterTab::BasicInput)},
            {"group", declaration ? q(declaration->group) : QString{}},
            {"options", options},
            {"optionsProvider",
             declaration ? static_cast<int>(declaration->optionsProvider) : static_cast<int>(OptionsProvider::None)},
            {"minimum", QString{}},
            {"maximum", QString{}},
            {"step", QString{}},
            {"decimals", type.type == Type::Float ? kValueDisplayDecimals : -1},
            {"canIncrement", false},
            {"canDecrement", false},
            {"inputMode", static_cast<int>(mode)}
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
                row["canIncrement"]
                    = *value <= std::numeric_limits<std::int64_t>::max() - stepSize && within(*value + stepSize);
                row["canDecrement"]
                    = *value >= std::numeric_limits<std::int64_t>::min() + stepSize && within(*value - stepSize);
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
            if (declaration->decimals && type.type != Type::Float)
                row["decimals"] = *declaration->decimals;
        }
        result.push_back({key, nodeIdText, row.value("group").toString(), row.value("tab").toInt(), std::move(row)});
    };

    auto addMeta = [&](int id, const QString &name, EditorKind editorKind, const QString &displayText,
                       bool isSingle = false, const QVariantMap &extras = {})
    {
        const QString key = ParameterListModel::makeKey(nodeIdText, static_cast<int>(PortStage::Inputs), false,
                                                        QVariant{}, QString{}, id);
        QVariantMap row{
            {            "key",                                        key},
            {             "id",                                         id},
            {          "stage",          static_cast<int>(PortStage::Inputs)},
            {   "recordScoped",                                      false},
            {      "datasetId",                                 QVariant{}},
            {       "recordId",                                  QString{}},
            {           "name",                                       name},
            {           "type",             static_cast<int>(Type::String)},
            {     "typeDetail",                   QStringLiteral("String")},
            {    "typeDisplay",             QStringLiteral("宿主配置")},
            {           "init",                                      false},
            {        "literal",                                displayText},
            {      "valueText",                                displayText},
            {    "displayText",                                displayText},
            {       "editText",                                displayText},
            {          "title",                workbench::q(node->title())},
            {         "editor",               static_cast<int>(editorKind)},
            {            "tab", static_cast<int>(ParameterTab::BasicInput)},
            {          "group",                                  QString{}},
            {         "isMeta",                                       true},
            {"isSingleFormula",                                   isSingle},
            {        "options",                             QVariantList{}},
            {"optionsProvider",    static_cast<int>(OptionsProvider::None)},
            {        "minimum",                                  QString{}},
            {        "maximum",                                  QString{}},
            {           "step",                                  QString{}},
            {       "decimals",                                         -1},
            {   "canIncrement",                                      false},
            {   "canDecrement",                                      false},
            {      "inputMode",     static_cast<int>(BindingMode::Literal)}
        };
        for (auto entry = extras.begin(); entry != extras.end(); ++entry) row.insert(entry.key(), entry.value());
        result.push_back({key, nodeIdText, row.value("group").toString(), row.value("tab").toInt(), std::move(row)});
    };

    if (contract)
    {
        if (this->q->trainingAvailable())
        {
            const_cast<AppController::Impl *>(this)->ensureTrainingSession();
            for (const auto &port : tool->trainParams)
                add(port.id, port.type, PortStage::Train, &port);
            for (const auto &port : training->recordParameters())
                add(port.id, port.type, PortStage::Train, &port, true);
        }
        for (const auto &port : contract->initParams)
        {
            if (tool->derivesPorts() && isDerivedInitParam(tool->nodeModel, port.id)) continue;
            add(port.id, port.type, PortStage::Init, &port);
        }

        if (tool->nodeModel == NodeModel::Expression)
        {
            QString exprText;
            bool    isSingle = false;
            if (node->expression)
            {
                if (node->expression->program.size() == 1)
                {
                    exprText = workbench::q(node->expression->program[0].expression);
                    isSingle = true;
                }
                else if (node->expression->program.size() > 1)
                {
                    QJsonArray program;
                    for (const auto &entry : node->expression->program)
                        program.append(QJsonObject{
                            {      "name",       workbench::q(entry.name)},
                            {"expression", workbench::q(entry.expression)}
                        });
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
            addMeta(workbench::kExpressionRow, QStringLiteral("表达式"), EditorKind::Expression, exprText, isSingle);
        }
        else if (tool->nodeModel == NodeModel::Condition)
        {
            QString condText;
            if (node->expression && !node->expression->program.empty())
                condText = workbench::q(node->expression->program[0].expression);
            const bool  upstream = conditionUsesUpstream(*node);
            const auto  found    = node->inputs.find(kConditionInputPort);
            const bool  bound    = found != node->inputs.end() && found->second.source.has_value();
            QVariantMap extras{
                {  "inputMode", static_cast<int>(upstream ? BindingMode::Upstream : BindingMode::Literal)},
                {"bindingPort",                                    static_cast<uint>(kConditionInputPort)}
            };
            if (bound)
            {
                const auto &source = *found->second.source;
                const auto *parent = findNode(source.node);
                QString     name   = workbench::q(portLabel(source.port));
                if (parent)
                {
                    const auto *parentTool = registry.find(parent->toolId);
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
                    upstream ? EditorKind::ConditionUpstream : EditorKind::Condition, condText, true, extras);
        }
        else if (tool->nodeModel == NodeModel::Select)
        {
            QString selectText = node->selectType ? displayTypeName(*node->selectType) : QString{};
            addMeta(workbench::kSelectTypeRow, QStringLiteral("合流类型"), EditorKind::Select, selectText, false);
        }

        if (contract)
            for (const auto &port : contract->inputs)
            {
                if (tool && tool->nodeModel == NodeModel::Condition && port.id == kConditionInputPort)
                    continue;
                add(port.id, port.type, PortStage::Inputs, &port);
            }
    }
    else
    {
        for (const auto &[id, type] : node->signatures.initParams)
        {
            if (tool && tool->derivesPorts() && isDerivedInitParam(tool->nodeModel, id))
                continue;
            add(id, type, PortStage::Init, nullptr);
        }

        for (const auto &[id, type] : node->signatures.inputs) add(id, type, PortStage::Inputs, nullptr);
    }
    const QStringList basicGroups{"流程控制", "检测图像", "检测区域", "输入数据"};
    for (auto &item : result)
    {
        auto &row = item.map;
        if (!row.contains("stage")) row.insert("stage", static_cast<int>(PortStage::Inputs));
        if (row.value("tab").toInt() == static_cast<int>(ParameterTab::BasicInput))
        {
            QString group;
            if ((row.value("isMeta").toBool() && row.value("editor").toInt() != static_cast<int>(EditorKind::Expression)) || row.value("system").toBool()
                || row.value("id").toUInt() == kUpstreamStatusPort || row.value("group") == QStringLiteral("流程控制")) group = basicGroups[0];
            else if (row.value("type").toInt() == static_cast<int>(Type::Image)) group = basicGroups[1];
            else if (isShapeType(static_cast<Type>(row.value("type").toInt()))) group = basicGroups[2];
            else group = basicGroups[3];
            row.insert("group", group);
            item.group = group;
        }
        item.tab = row.value("tab").toInt();
    }
    std::stable_sort(result.begin(), result.end(), [&](const ParameterListModel::Row &a, const ParameterListModel::Row &b)
    {
        if (a.tab != b.tab) return a.tab < b.tab;
        if (a.tab == static_cast<int>(ParameterTab::BasicInput))
            return basicGroups.indexOf(a.group) < basicGroups.indexOf(b.group);
        return false;
    });
    return result;
}

void AppController::Impl::refreshParameterModel()
{
    const auto *node = currentNode();
    if (!node)
    {
        parameterModel.clear();
        parameterModelNode = 0;
        return;
    }
    if (!readsAllowed())
    {
        if (parameterModelNode != node->id)
        {
            parameterModel.clear();
            parameterModelNode = 0;
        }
        return;
    }
    parameterModel.sync(buildParameterRows());
    parameterModelNode = node->id;
}

QAbstractListModel *AppController::parameterModel() const
{
    return &impl_->parameterModel;
}

QVariantList AppController::parameters() const
{
    impl_->refreshParameterModel();
    return impl_->parameterModel.snapshot();
}

QVariantList AppController::parameterGroups() const
{
    impl_->refreshParameterModel();
    return impl_->parameterModel.groupsSnapshot();
}

QString AppController::outputTitle() const
{
    const auto *node = impl_->currentNode();
    const auto result = node ? impl_->presentation.displaySnapshot(*node) : ResultSlot::Snapshot{};
    return result && result->invocation == ProducedBy::Train ? QStringLiteral("训练输出") : QStringLiteral("输出参数");
}

} // namespace visionrt
