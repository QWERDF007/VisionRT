#include "ui/AppController_p.h"

#include <limits>

namespace visionrt {
namespace workbench {
namespace {
QString kindName(ToolKind kind)
{
    switch (kind)
    {
    case ToolKind::Cpp:
        return "cpp";
    case ToolKind::Host:
        return "host";
    case ToolKind::PythonFunction:
        return "python_function";
    case ToolKind::PythonClass:
        return "python_class";
    }
    return {};
}

QVariantMap portMetadata(const PortDefinition &port)
{
    QVariantMap row{
        {"id", q(port.id)},
        {"name", q(port.name)},
        {"type", QString::fromLatin1(visionrt::typeName(port.type.type))},
        {"rank", port.type.rank},
        {"layout", port.type.layout == Layout::Dense ? "dense" : "batch_ragged"},
        {"dtype", QString{}},
        {"default", valueText(port.defaultValue, port.type)},
        {"editor", q(port.editor)},
        {"tab", q(port.tab)},
        {"group", q(port.group)},
        {"minimum", QString{}},
        {"maximum", QString{}},
        {"step", QString{}},
        {"decimals", port.decimals ? *port.decimals : -1},
        {"optionsProvider", q(port.optionsProvider)},
        {"imageInput", q(port.imageInput)},
        {"imagePort", q(port.imagePort)},
        {"colorOrder", q(port.colorOrder)},
        {"sameGeometryAsInput", q(port.sameGeometryAsInput)},
        {"system", port.id.starts_with("__")}
    };
    if (port.typeFrom)
    {
        row["type"] = QStringLiteral("待解析");
        row.remove("rank");
        row.remove("layout");
        row["default"] = QString{};
        row["typeFrom"] = QVariantMap{{"flow", q(port.typeFrom->flowParameter)},
            {"node", q(port.typeFrom->nodeParameter)}, {"port", q(port.typeFrom->portParameter)}};
    }
    if (port.type.dtype)
    {
        row["dtype"] = QString::fromLatin1(dtypeName(*port.type.dtype));
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
        {            "id",                    q(definition.id)},
        {   "displayName",           q(definition.displayName)},
        {       "version",               q(definition.version)},
        {          "kind",           kindName(definition.kind)},
        {      "category",              q(definition.category)},
        {   "subcategory",           q(definition.subcategory)},
        {       "builtin",                  definition.builtin},
        {"definitionPath", pathText(definition.definitionPath)},
        {  "hostBinding",          q(definition.hostBinding)},
        {        "symbol",                q(definition.symbol)},
        {        "script",                           QString{}}
    };
    if (!definition.script.empty())
        result["script"] = pathText(definition.script.lexically_relative(definition.definitionPath.parent_path()));
    auto ports = [](const auto &values)
    {
        QVariantList rows;
        for (const auto &port : values) rows.push_back(portMetadata(port));
        return rows;
    };
    result["inputs"]     = ports(definition.inputs);
    result["initParams"] = ports(definition.initParams);
    result["outputs"]    = ports(definition.outputs);
    return result;
}
} // namespace workbench

QVariantList AppController::toolDefinitions() const
{
    QVariantList result;
    for (const auto &definition : impl_->registry.definitions())
    {
        auto       item         = workbench::metadata(definition);
        const auto availability = impl_->registry.availability(definition.id);
        item["available"]       = availability.state == ToolAvailability::Available;
        item["message"]         = workbench::q(availability.message);
        item["detail"]          = workbench::q(availability.detail);
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
    const auto *tool = impl_->currentTool();
    auto add = [&](const std::string &id, const TypeSpec &type, bool initialization, const PortDefinition *declaration)
    {
        PortValue literal = Missing{};
        QString   mode    = "literal", source;
        if (initialization)
        {
            const auto found = node->initParams.find(id);
            if (found != node->initParams.end())
                literal = found->second;
        }
        else if (const auto found = node->inputs.find(id); found != node->inputs.end())
        {
            literal = found->second.literal;
            mode    = found->second.mode == BindingMode::Literal ? "literal" : "upstream";
            if (found->second.source)
            {
                const auto *parent    = impl_->findNode(found->second.source->node);
                const auto *parentDef = parent ? impl_->registry.find(parent->toolId) : nullptr;
                const auto *portDef
                    = parentDef ? workbench::port(parentDef->outputs, found->second.source->port) : nullptr;
                const QString portName = portDef ? q(portDef->name) : q(found->second.source->port);
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
                const auto *stringValue = std::get_if<Utf8>(&value);
                const auto  text        = stringValue ? q(stringValue->str()) : valueText(value, type);
                addOption(text, text);
            }
        if (initialization && tool)
            for (const auto &output : tool->outputs)
                if (output.typeFrom)
                {
                    const auto &reference = *output.typeFrom;
                    auto selected = [&](const std::string &parameter) -> QString
                    {
                        const auto found = node->initParams.find(parameter);
                        const auto *value = found == node->initParams.end() ? nullptr : std::get_if<Utf8>(&found->second);
                        return value ? q(value->str()) : QString{};
                    };
                    for (const auto &sourceFlow : impl_->editor.project().flows)
                    {
                        if (sourceFlow.id == impl_->flow.toStdString()) continue;
                        if (id == reference.flowParameter) addOption(q(sourceFlow.name), q(sourceFlow.id));
                        if (q(sourceFlow.id) != selected(reference.flowParameter)) continue;
                        for (const auto &sourceNode : sourceFlow.nodes)
                        {
                            const auto sequence = QString::number(sourceNode.id);
                            if (id == reference.nodeParameter) addOption(q(sourceNode.title()), sequence);
                            if (sequence != selected(reference.nodeParameter)) continue;
                            if (id == reference.portParameter)
                                for (const auto &[port, signature] : sourceNode.signatures.outputs)
                                    addOption(q(port), q(port));
                        }
                    }
                }
        const auto *string     = std::get_if<Utf8>(&literal);
        const auto  inputFound = node->inputs.find(id);
        const bool  hasSource  = (inputFound != node->inputs.end() && inputFound->second.source.has_value());
        QString editorKind = declaration ? q(declaration->editor) : QString{};
        if (declaration && declaration->editor == "path")
        {
            PortValue modeValue = Missing{};
            if (initialization)
            {
                const auto found = node->initParams.find(declaration->pathModeFrom);
                if (found != node->initParams.end()) modeValue = found->second;
            }
            else
                modeValue = impl_->resolvedInput(*node, declaration->pathModeFrom);
            const auto *pathMode = std::get_if<Utf8>(&modeValue);
            editorKind = pathMode && (pathMode->str() == "file" || pathMode->str() == "directory")
                ? q(pathMode->str()) : QString{};
        }

        QVariant rectMap;
        bool hasRect = false;
        QString rectDisplayText = QStringLiteral("[]");
        if (type.type == Type::Rect)
        {
            if (const auto *roi = std::get_if<Roi>(&literal))
            {
                if (roi->kind() == RoiKind::Rectangle)
                {
                    const auto enc = roi->encoded();
                    if (enc.size() >= 6)
                    {
                        hasRect = true;
                        rectMap = QVariantMap{
                            {"type", 2},
                            {"x", double(enc[1])},
                            {"y", double(enc[2])},
                            {"width", double(enc[3])},
                            {"height", double(enc[4])},
                            {"angle", double(enc[5])}
                        };
                        rectDisplayText = QString::asprintf("[2, %.6g, %.6g, %.6g, %.6g, %.6g]",
                                                            enc[1], enc[2], enc[3], enc[4], enc[5]);
                    }
                }
            }
        }

        int roiCount = 0;
        bool hasRois = false;
        if (type.type == Type::RoiBatch)
        {
            if (const auto *batch = std::get_if<RoiBatch>(&literal))
            {
                for (const auto &imgRois : batch->images())
                    roiCount += static_cast<int>(imgRois.size());
                hasRois = roiCount > 0;
            }
        }

        QString displayText;
        if (type.type == Type::Rect)
            displayText = hasRect ? rectDisplayText : QStringLiteral("[]");
        else if (type.type == Type::RoiBatch)
            displayText = hasRois ? QStringLiteral("[%1 个区域]").arg(roiCount) : QStringLiteral("[]");
        else if (string)
            displayText = q(string->str());
        else
            displayText = text;

        QVariantMap row{
            {             "id",                                                                    q(id)},
            {           "name",                               declaration ? q(declaration->name) : q(id)},
            {           "type",                            QString::fromLatin1(typeName(type.type))},
            {     "typeDetail",                                                           typeText(type)},
            {           "init",                                                           initialization},
            {           "mode",                                                                     mode},
            {        "literal",                                                                     text},
            {      "valueText",                       type.type == Type::Rect && hasRect ? rectDisplayText
                                                      : string ? q(string->str()) : text},
            {    "displayText",                                                              displayText},
            {           "rect",                                                                  rectMap},
            {        "hasRect",                                                                  hasRect},
            {       "roiCount",                                                                 roiCount},
            {        "hasRois",                                                                  hasRois},
            {         "source",                                                                   source},
            {     "sourceNode", hasSource ? QString::number(inputFound->second.source->node) : QString{}},
            {     "sourcePort",               hasSource ? q(inputFound->second.source->port) : QString{}},
            {          "title",                                                         q(node->title())},
            {         "editor",                                                               editorKind},
            {            "tab",           declaration ? q(declaration->tab) : QStringLiteral("基本输入")},
            {          "group",                          initialization ? QStringLiteral("初始化配置")
                          : declaration  ? q(declaration->group)
                          : QString{}                                    },
            {        "options",                                                                  options},
            {"optionsProvider",                declaration ? q(declaration->optionsProvider) : QString{}},
            {     "imageInput",                     declaration ? q(declaration->imageInput) : QString{}},
            {        "minimum",                                                                QString{}},
            {        "maximum",                                                                QString{}},
            {           "step",                                                                QString{}},
            {       "decimals",                                                                       -1},
            {   "canIncrement",                                                                    false},
            {   "canDecrement",                                                                    false}
        };
        if (type.type == Type::Int && (initialization || mode == "literal"))
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
    if (tool)
    {
        for (const auto &port : tool->initParams) add(port.id, port.type, true, &port);
        for (const auto &port : tool->inputs) add(port.id, port.type, false, &port);
    }
    else
    {
        for (const auto &[id, type] : node->signatures.initParams) add(id, type, true, nullptr);
        for (const auto &[id, type] : node->signatures.inputs) add(id, type, false, nullptr);
    }
    return result;
}

} // namespace visionrt
