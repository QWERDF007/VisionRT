#include "core/DefinitionJson.h"

#include "core/JsonFields.h"
#include "core/ValueJson.h"

#include <limits>

namespace visionrt {
namespace {
std::vector<PortDefinition> ports(const ValueJson::Document &document, const QJsonObject &root,
                                  const QString &direction)
{
    const auto                  items  = json::array(root.value(direction), direction);
    const bool                  output = direction == "outputs";
    std::vector<PortDefinition> result;
    result.reserve(items.size());
    for (qsizetype index = 0; index < items.size(); ++index)
    {
        const auto context = direction + "[" + QString::number(index) + "]";
        const auto item    = json::object(items[index], context);
        if (output)
            json::fields(item,
                         {"id", "name", "type", "dtype", "rank", "required", "type_from", "image_port", "display"},
                         context);
        else
            json::fields(item,
                         {"id", "name", "type", "dtype", "rank", "required", "default", "editor", "tab", "group", "min",
                          "max", "step", "decimals", "options", "options_provider", "image_input", "path_mode_from"},
                         context);
        PortDefinition port;
        port.id = json::text(item.value("id"), context + ".id");
        if (port.id.starts_with("__"))
            throw ContractError((context + ".id 不能声明宿主系统端口").toStdString());
        port.name = json::text(item.value("name"), context + ".name");
        if (item.contains("required"))
        {
            if (!item.value("required").isBool())
                throw ContractError((context + ".required 必须是布尔值").toStdString());
            port.required = item.value("required").toBool();
        }
        try
        {
            if (item.contains("type_from"))
            {
                for (const auto name : {"type", "dtype", "rank", "image_port", "display"})
                    if (item.contains(name))
                        throw ContractError("type_from 不能同时声明 " + std::string(name));
                const auto reference = json::object(item.value("type_from"), context + ".type_from");
                json::fields(reference, {"flow", "node", "port"}, context + ".type_from");
                port.typeFrom = PortTypeReference{json::text(reference.value("flow"), context + ".type_from.flow"),
                                                  json::text(reference.value("node"), context + ".type_from.node"),
                                                  json::text(reference.value("port"), context + ".type_from.port")};
            }
            else
            {
                QJsonObject type;
                for (const auto field : {"type", "dtype", "rank"})
                    if (item.contains(field))
                        type.insert(field, item.value(field));
                port.type = ValueJson::decodeType(type);
            }
            const auto decode = [&](const QString &field, const QStringList &suffix = QStringList{})
            {
                QStringList path{direction, QString::number(index), field};
                path.append(suffix);
                try
                {
                    return document.decode(path, port.type);
                }
                catch (const ContractError &error)
                {
                    throw ContractError((field + "：").toStdString() + error.what());
                }
            };
            if (item.contains("default"))
                port.defaultValue = decode("default");
            if (item.contains("min"))
                port.minimum = decode("min");
            if (item.contains("max"))
                port.maximum = decode("max");
            if (item.contains("step"))
                port.step = decode("step");
            if (item.contains("decimals"))
            {
                const auto value = document.decode({direction, QString::number(index), "decimals"}, {Type::Int});
                if (!std::holds_alternative<std::int64_t>(value) || std::get<std::int64_t>(value) < 0
                    || std::get<std::int64_t>(value) > std::numeric_limits<int>::max())
                    throw ContractError("decimals 必须是非负 int 范围整数");
                port.decimals = static_cast<int>(std::get<std::int64_t>(value));
            }
            if (item.contains("options"))
            {
                const auto options = json::array(item.value("options"), context + ".options");
                for (qsizetype option = 0; option < options.size(); ++option)
                    port.options.push_back(decode("options", {QString::number(option)}));
            }
            for (const auto &[key, target] : std::initializer_list<std::pair<const char *, std::string *>>{
                     {          "editor",          &port.editor},
                     {             "tab",             &port.tab},
                     {           "group",           &port.group},
                     {"options_provider", &port.optionsProvider},
                     {     "image_input",      &port.imageInput},
                     {      "image_port",       &port.imagePort},
                     {  "path_mode_from",    &port.pathModeFrom}
            })
                if (item.contains(key))
                    *target = json::text(item.value(key), context + "." + key, false);
            if (item.contains("display"))
            {
                const auto display = json::object(item.value("display"), context + ".display");
                json::fields(display, {"color_order", "same_geometry_as_input"}, context + ".display");
                if (display.contains("color_order"))
                    port.colorOrder = json::text(display.value("color_order"), context + ".display.color_order");
                if (display.contains("same_geometry_as_input"))
                    port.sameGeometryAsInput = json::text(display.value("same_geometry_as_input"),
                                                          context + ".display.same_geometry_as_input");
            }
        }
        catch (const std::exception &error)
        {
            throw ContractError(context.toStdString() + "（" + port.id + "）：" + error.what());
        }
        result.push_back(std::move(port));
    }
    return result;
}

QJsonArray encodePorts(const std::vector<PortDefinition> &ports, bool output)
{
    QJsonArray result;
    for (const auto &port : ports)
    {
        if (port.id.starts_with("__"))
            continue;
        QJsonObject item{
            {      "id",   QString::fromStdString(port.id)},
            {    "name", QString::fromStdString(port.name)},
            {"required",                     port.required}
        };
        if (port.typeFrom)
            item.insert("type_from", QJsonObject{
                                         {"flow", QString::fromStdString(port.typeFrom->flowParameter)},
                                         {"node", QString::fromStdString(port.typeFrom->nodeParameter)},
                                         {"port", QString::fromStdString(port.typeFrom->portParameter)}
            });
        else
        {
            const auto type = ValueJson::encodeType(port.type);
            for (auto field = type.begin(); field != type.end(); ++field) item.insert(field.key(), field.value());
        }
        if (!output)
        {
            if (!isMissing(port.defaultValue))
                item.insert("default", ValueJson::encode(port.defaultValue, port.type));
            if (port.minimum)
                item.insert("min", ValueJson::encode(*port.minimum, port.type));
            if (port.maximum)
                item.insert("max", ValueJson::encode(*port.maximum, port.type));
            if (port.step)
                item.insert("step", ValueJson::encode(*port.step, port.type));
            if (port.decimals)
                item.insert("decimals", *port.decimals);
            for (const auto &[key, value] : std::initializer_list<std::pair<const char *, std::string>>{
                     {          "editor",          port.editor},
                     {             "tab",             port.tab},
                     {           "group",           port.group},
                     {"options_provider", port.optionsProvider},
                     {     "image_input",      port.imageInput},
                     {  "path_mode_from",    port.pathModeFrom}
            })
                if (!value.empty())
                    item.insert(key, QString::fromStdString(value));
            if (!port.options.empty())
            {
                QJsonArray options;
                for (const auto &value : port.options) options.append(ValueJson::encode(value, port.type));
                item.insert("options", options);
            }
        }
        else
        {
            if (!port.imagePort.empty())
                item.insert("image_port", QString::fromStdString(port.imagePort));
            QJsonObject display;
            if (!port.colorOrder.empty())
                display.insert("color_order", QString::fromStdString(port.colorOrder));
            if (!port.sameGeometryAsInput.empty())
                display.insert("same_geometry_as_input", QString::fromStdString(port.sameGeometryAsInput));
            if (!display.isEmpty())
                item.insert("display", display);
        }
        result.append(item);
    }
    return result;
}
}

ToolDefinition parseToolDefinitionJson(const std::string &content, const std::filesystem::path &path, ToolSource source)
{
    try
    {
        const ValueJson::Document document(QByteArray::fromStdString(content));
        if (!document.json().isObject())
            throw ContractError("工具声明必须是 JSON 对象");
        const auto root = document.json().object();
        json::fields(root,
                     {"schema_version", "tool_id", "display_name", "version", "kind", "entry", "category",
                      "subcategory", "resources", "init_params", "inputs", "outputs"},
                     "工具声明");
        if (!equalValues(document.decode({"schema_version"}, {Type::Int}), PortValue{std::int64_t{2}}))
            throw ContractError("schema_version 必须是整数 2");
        ToolDefinition definition;
        definition.id          = json::text(root.value("tool_id"), "tool_id");
        definition.displayName = json::text(root.value("display_name"), "display_name");
        definition.version     = json::text(root.value("version"), "version");
        definition.source      = std::move(source);
        if (!path.empty())
            definition.definitionPath = std::filesystem::absolute(path).lexically_normal();
        if (root.contains("category"))
            definition.category = json::text(root.value("category"), "category", false);
        if (root.contains("subcategory"))
            definition.subcategory = json::text(root.value("subcategory"), "subcategory", false);
        const auto kind = json::text(root.value("kind"), "kind");
        if (kind == "cpp")
        {
            definition.kind = ToolKind::Cpp;
            if (root.contains("entry"))
                throw ContractError("cpp 按 tool_id 注册，不接受 entry");
        }
        else
        {
            const auto entry = json::object(root.value("entry"), "entry");
            if (kind == "host")
            {
                definition.kind = ToolKind::Host;
                json::fields(entry, {"binding"}, "entry");
                definition.hostBinding = json::text(entry.value("binding"), "entry.binding");
            }
            else if (kind == "python_function" || kind == "python_class")
            {
                definition.kind = kind == "python_function" ? ToolKind::PythonFunction : ToolKind::PythonClass;
                json::fields(entry, {"package", "file", "symbol"}, "entry");
                definition.entry.package
                    = json::relativePath(json::text(entry.value("package"), "entry.package"), "entry.package");
                definition.entry.file = json::relativePath(json::text(entry.value("file"), "entry.file"), "entry.file");
                definition.entry.symbol = json::text(entry.value("symbol"), "entry.symbol");
            }
            else
                throw ContractError("未知工具 kind：" + kind);
        }
        if (root.contains("resources"))
            for (const auto &value : json::array(root.value("resources"), "resources"))
                definition.resources.push_back(json::relativePath(json::text(value, "resources"), "resources"));
        definition.initParams = ports(document, root, "init_params");
        definition.inputs     = ports(document, root, "inputs");
        definition.outputs    = ports(document, root, "outputs");
        PortDefinition code{"__status_code", "状态码", {Type::Int}};
        PortDefinition message{"__status_message", "状态消息", {Type::String}};
        definition.outputs.insert(definition.outputs.begin(), {std::move(code), std::move(message)});
        validateToolDefinition(definition);
        return definition;
    }
    catch (const std::exception &error)
    {
        throw ContractError((path.empty() ? std::string{} : json::pathText(path).toStdString() + ": ") + error.what());
    }
}

ToolDefinition parseToolDefinitionFile(const std::filesystem::path &path, ToolSource source)
{
    if (path.extension() != ".json")
        throw ContractError("工具声明仅支持 JSON：" + json::pathText(path).toStdString());
    return parseToolDefinitionJson(json::read(path).toStdString(), path, std::move(source));
}

std::string emitNormalizedToolJson(const ToolDefinition &definition)
{
    validateToolDefinition(definition);
    QJsonObject root{
        {"schema_version", definition.schemaVersion},
        {"tool_id", QString::fromStdString(definition.id)},
        {"display_name", QString::fromStdString(definition.displayName)},
        {"version", QString::fromStdString(definition.version)},
        {"category", QString::fromStdString(definition.category)},
        {"subcategory", QString::fromStdString(definition.subcategory)},
        {"init_params", encodePorts(definition.initParams, false)},
        {"inputs", encodePorts(definition.inputs, false)},
        {"outputs", encodePorts(definition.outputs, true)}
    };
    switch (definition.kind)
    {
    case ToolKind::Cpp:
        root.insert("kind", "cpp");
        break;
    case ToolKind::Host:
        root.insert("kind", "host");
        root.insert("entry", QJsonObject{
                                 {"binding", QString::fromStdString(definition.hostBinding)}
        });
        break;
    case ToolKind::PythonFunction:
    case ToolKind::PythonClass:
        root.insert("kind", definition.kind == ToolKind::PythonFunction ? "python_function" : "python_class");
        root.insert("entry", QJsonObject{
                                 {"package",        json::pathText(definition.entry.package)},
                                 {   "file",           json::pathText(definition.entry.file)},
                                 { "symbol", QString::fromStdString(definition.entry.symbol)}
        });
        break;
    }
    if (!definition.resources.empty())
    {
        QJsonArray resources;
        for (const auto &path : definition.resources) resources.append(json::pathText(path));
        root.insert("resources", resources);
    }
    return QJsonDocument(root).toJson(QJsonDocument::Indented).toStdString();
}
}
