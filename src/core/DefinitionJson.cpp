#include "core/DefinitionJson.h"

#include "core/JsonFields.h"
#include "core/ValueJson.h"

#include <QIODevice>
#include <QSaveFile>

#include <limits>
#include <string_view>

namespace visionrt {
namespace {

PortRef reference(const QJsonValue &value, const QString &context)
{
    const auto block = json::object(value, context);
    json::fields(block, {"stage", "id"}, context);
    const auto parsed = json::enumeration<PortStage>(block.value("stage"), PortStage::Outputs, context + ".stage");
    const auto id = block.value("id");
    if (!id.isDouble() || id.toDouble() < 0.0 || id.toDouble() > 4294967295.0
        || id.toDouble() != std::floor(id.toDouble()))
        throw ContractError((context + ".id 必须是非负 32 位整数").toStdString());
    return {parsed, static_cast<PortId>(id.toDouble())};
}

PortId portId(const QJsonValue &value, const QString &context)
{
    if (!value.isDouble() || value.toDouble() < 0.0 || value.toDouble() > 4294967295.0
        || value.toDouble() != std::floor(value.toDouble()))
        throw ContractError((context + " 必须是非负 32 位整数").toStdString());
    return static_cast<PortId>(value.toDouble());
}

std::vector<PortDefinition> ports(const ValueJson::Document &document, const QJsonObject &root, PortStage stage)
{
    const auto direction = QString::fromLatin1(stage == PortStage::Init    ? "init_params"
                                               : stage == PortStage::Inputs ? "inputs"
                                                                            : "outputs");
    const bool output = stage == PortStage::Outputs;
    if (!root.contains(direction))
        return {};
    const auto   items = json::array(root.value(direction), direction);
    std::vector<PortDefinition> result;
    result.reserve(items.size());
    for (qsizetype index = 0; index < items.size(); ++index)
    {
        const auto path    = QStringList{direction, QString::number(index)};
        const auto context = direction + "[" + QString::number(index) + "]";
        const auto item    = json::object(items[index], context);
        json::fields(item,
                     {"id", "name_zh", "name_en", "type", "type_from", "default", "constraints", "ui", "relations",
                      "display"},
                     context);
        PortDefinition port;
        if (!item.contains("id"))
            throw ContractError((context + " 缺少 id").toStdString());
        port.id     = portId(item.value("id"), context + ".id");
        port.nameZh = json::text(item.value("name_zh"), context + ".name_zh");
        if (item.contains("name_en"))
            port.nameEn = json::text(item.value("name_en"), context + ".name_en", false);
        const auto decode = [&](const QString &field, const QStringList &suffix = QStringList{})
        {
            QStringList location = path;
            location.append(field);
            location.append(suffix);
            try
            {
                return document.decode(location, port.type);
            }
            catch (const ContractError &error)
            {
                throw ContractError((field + "：").toStdString() + error.what());
            }
        };
        try
        {
            if (item.contains("type_from"))
            {
                for (const auto name : {"type", "default", "constraints", "ui", "relations", "display"})
                    if (item.contains(name))
                        throw ContractError("type_from 不能同时声明 " + std::string(name));
                const auto block = json::object(item.value("type_from"), context + ".type_from");
                json::fields(block, {"flow", "node", "port"}, context + ".type_from");
                port.typeFrom = PortTypeReference{
                    reference(block.value("flow"), context + ".type_from.flow"),
                    reference(block.value("node"), context + ".type_from.node"),
                    reference(block.value("port"), context + ".type_from.port")};
            }
            else
            {
                port.type = ValueJson::decodeType(json::object(item.value("type"), context + ".type"));
            }
            if (item.contains("default"))
                port.defaultValue = decode("default");
            if (item.contains("constraints"))
            {
                const auto block = json::object(item.value("constraints"), context + ".constraints");
                json::fields(block, {"min", "max", "options"}, context + ".constraints");
                if (block.contains("min"))
                    port.minimum = decode("constraints", {"min"});
                if (block.contains("max"))
                    port.maximum = decode("constraints", {"max"});
                if (block.contains("options"))
                {
                    const auto options = json::array(block.value("options"), context + ".constraints.options");
                    for (qsizetype option = 0; option < options.size(); ++option)
                        port.options.push_back(decode("constraints", {"options", QString::number(option)}));
                }
            }
            if (item.contains("ui"))
            {
                const auto block = json::object(item.value("ui"), context + ".ui");
                json::fields(block, {"editor", "tab", "group", "step", "decimals", "options_provider"},
                             context + ".ui");
                if (block.contains("editor")) port.editor = json::enumeration<EditorKind>(block.value("editor"), EditorKind::Rect, context);
                if (block.contains("tab")) port.tab = json::enumeration<ParameterTab>(block.value("tab"), ParameterTab::Postprocess, context);
                if (block.contains("group")) port.group = json::text(block.value("group"), context);
                if (block.contains("options_provider")) port.optionsProvider = json::enumeration<OptionsProvider>(block.value("options_provider"), OptionsProvider::GpuDevices, context);
                if (block.contains("step"))
                    port.step = decode("ui", {"step"});
                if (block.contains("decimals"))
                {
                    const auto value = document.decode(QStringList(path) << "ui" << "decimals", {Type::Int});
                    if (!std::holds_alternative<std::int64_t>(value) || std::get<std::int64_t>(value) < 0
                        || std::get<std::int64_t>(value) > std::numeric_limits<int>::max())
                        throw ContractError("decimals 必须是非负 int 范围整数");
                    port.decimals = static_cast<int>(std::get<std::int64_t>(value));
                }
            }
            if (item.contains("relations"))
            {
                const auto block = json::object(item.value("relations"), context + ".relations");
                json::fields(block, {"image", "alignment", "same_geometry_as"}, context + ".relations");
                if (block.contains("image"))
                    port.image = reference(block.value("image"), context + ".relations.image");
                if (block.contains("alignment"))
                    port.alignment = reference(block.value("alignment"), context + ".relations.alignment");
                if (block.contains("same_geometry_as"))
                    port.sameGeometryAs
                        = reference(block.value("same_geometry_as"), context + ".relations.same_geometry_as");
            }
            if (item.contains("display"))
            {
                const auto block = json::object(item.value("display"), context + ".display");
                json::fields(block, {"color_order", "visible"}, context + ".display");
                if (block.contains("color_order"))
                    port.colorOrder = json::enumeration<ColorOrder>(block.value("color_order"), ColorOrder::RGBA, context);
                if (block.contains("visible"))
                {
                    if (!block.value("visible").isBool())
                        throw ContractError("display.visible 必须是布尔值");
                    port.imageVisible = block.value("visible").toBool();
                }
            }
            if (output)
            {
                if (item.contains("default"))
                    throw ContractError("default 不属于输出端口");
                if (item.contains("constraints"))
                    throw ContractError("constraints 不属于输出端口");
                if (item.contains("ui"))
                    throw ContractError("ui 不属于输出端口");
            }
        }
        catch (const std::exception &error)
        {
            throw ContractError(context.toStdString() + "（" + portLabel(port.id) + "）：" + error.what());
        }
        result.push_back(std::move(port));
    }
    return result;
}

QJsonValue referenceJson(const PortRef &reference)
{
    return QJsonObject{{"stage", static_cast<int>(reference.stage)}, {"id", static_cast<double>(reference.id)}};
}

QJsonArray encodePorts(const std::vector<PortDefinition> &ports, PortStage stage)
{
    QJsonArray result;
    for (const auto &port : ports)
    {
        if (isReservedPort(stage, port.id))
            continue;
        QJsonObject item{{"id", static_cast<double>(port.id)}, {"name_zh", QString::fromStdString(port.nameZh)}};
        if (!port.nameEn.empty())
            item.insert("name_en", QString::fromStdString(port.nameEn));
        if (port.typeFrom)
            item.insert("type_from", QJsonObject{
                                         {"flow", referenceJson(port.typeFrom->flowParameter)},
                                         {"node", referenceJson(port.typeFrom->nodeParameter)},
                                         {"port", referenceJson(port.typeFrom->portParameter)}
            });
        else
            item.insert("type", ValueJson::encodeType(port.type));
        if (!isMissing(port.defaultValue)
            && (port.typeFrom || !equalValues(port.defaultValue, defaultValue(port.type))))
            item.insert("default", ValueJson::encode(port.defaultValue, port.type));
        QJsonObject constraints;
        if (port.minimum)
            constraints.insert("min", ValueJson::encode(*port.minimum, port.type));
        if (port.maximum)
            constraints.insert("max", ValueJson::encode(*port.maximum, port.type));
        if (!port.options.empty())
        {
            QJsonArray options;
            for (const auto &value : port.options) options.append(ValueJson::encode(value, port.type));
            constraints.insert("options", options);
        }
        if (!constraints.isEmpty())
            item.insert("constraints", constraints);
        QJsonObject ui;
        if (port.editor != EditorKind::Default) ui.insert("editor", static_cast<int>(port.editor));
        if (stage != PortStage::Outputs && port.tab != ParameterTab::BasicInput)
            ui.insert("tab", static_cast<int>(port.tab));
        if (!port.group.empty()) ui.insert("group", QString::fromStdString(port.group));
        if (port.optionsProvider != OptionsProvider::None) ui.insert("options_provider", static_cast<int>(port.optionsProvider));
        if (port.step)
            ui.insert("step", ValueJson::encode(*port.step, port.type));
        if (port.decimals)
            ui.insert("decimals", *port.decimals);
        if (!ui.isEmpty())
            item.insert("ui", ui);
        QJsonObject relations;
        for (const auto &[key, value] : std::initializer_list<std::pair<const char *, const std::optional<PortRef> *>>{
                 {         "image",          &port.image},
                 {     "alignment",      &port.alignment},
                 {"same_geometry_as", &port.sameGeometryAs}
        })
            if (*value)
                relations.insert(key, referenceJson(**value));
        if (!relations.isEmpty())
            item.insert("relations", relations);
        QJsonObject display;
        if (port.type.type == Type::Image)
            display.insert("visible", port.imageVisible);
        if (port.colorOrder != ColorOrder::Automatic)
            display.insert("color_order", static_cast<int>(port.colorOrder));
        if (!display.isEmpty())
            item.insert("display", display);
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
                     {"schema_version", "name_en", "name_zh", "version", "implementation", "ui", "init_params", "inputs", "outputs",
                      "node_model", "interaction", "training"},
                     "工具声明");
        if (root.value("schema_version").toInt() != 1) throw ContractError("工具声明 schema_version 必须为 1");
        ToolDefinition definition;
        definition.nameEn  = json::text(root.value("name_en"), "name_en");
        definition.nameZh  = json::text(root.value("name_zh"), "name_zh");
        definition.version = json::text(root.value("version"), "version");
        definition.source  = std::move(source);
        if (!path.empty())
            definition.definitionPath = std::filesystem::absolute(path).lexically_normal();

        const auto implementation = json::object(root.value("implementation"), "implementation");
        json::fields(implementation, {"kind", "binding", "package", "file", "symbol", "resources"},
                     "implementation");
        const auto kind = json::enumeration<ToolKind>(implementation.value("kind"), ToolKind::Host, "implementation.kind");
        if (kind == ToolKind::Cpp)
        {
            definition.kind = ToolKind::Cpp;
        }
        else if (kind == ToolKind::Host)
        {
            definition.kind = ToolKind::Host;
            definition.hostBinding = json::text(implementation.value("binding"), "implementation.binding");
        }
        else if (kind == ToolKind::PythonFunction || kind == ToolKind::PythonClass)
        {
            definition.kind = kind == ToolKind::PythonFunction ? ToolKind::PythonFunction : ToolKind::PythonClass;
            definition.entry.package = json::relativePath(
                json::text(implementation.value("package"), "implementation.package"), "implementation.package");
            definition.entry.file = json::relativePath(
                json::text(implementation.value("file"), "implementation.file"), "implementation.file");
            definition.entry.symbol = json::text(implementation.value("symbol"), "implementation.symbol");
        }
        else
            throw ContractError("未知 implementation.kind");
        const auto allowedField = [&](const char *field)
        {
            if (kind == ToolKind::Host)
                return std::string_view(field) == "binding";
            if (kind == ToolKind::PythonFunction || kind == ToolKind::PythonClass)
                return std::string_view(field) == "package" || std::string_view(field) == "file"
                       || std::string_view(field) == "symbol";
            return false;
        };
        for (const auto *field : {"binding", "package", "file", "symbol"})
            if (implementation.contains(field) && !allowedField(field))
                throw ContractError("implementation." + std::string(field) + " 不属于所选实现类型");
        if (implementation.contains("resources"))
            for (const auto &value : json::array(implementation.value("resources"), "implementation.resources"))
                definition.resources.push_back(
                    json::relativePath(json::text(value, "implementation.resources"), "implementation.resources"));

        if (root.contains("ui"))
        {
            const auto ui = json::object(root.value("ui"), "ui");
            // The tree groups by declaration directory, so a file never states its own category.
            json::fields(ui, {"visibility"}, "ui");
            if (ui.contains("visibility"))
            {
                definition.visibility = json::enumeration<ToolVisibility>(ui.value("visibility"), ToolVisibility::Assist, "ui.visibility");
            }
        }
        if (root.contains("node_model"))
        {
            definition.nodeModel = json::enumeration<NodeModel>(root.value("node_model"), NodeModel::Select, "node_model");
        }
        if (root.contains("training"))
        {
            const auto item = json::object(root.value("training"), "training");
            json::fields(item, {"kind", "asset_param", "helper"}, "training");
            TrainingSpec spec;
            spec.kind = json::enumeration<TrainingKind>(item.value("kind"), TrainingKind::StraightEdges, "training.kind");
            spec.assetParam = portId(item.value("asset_param"), "training.asset_param");
            if (const auto helper = item.find("helper"); helper != item.end())
                spec.helper = json::text(*helper, "training.helper");
            definition.training = spec;
        }
        if (root.contains("interaction"))
        {
            const auto item = json::object(root.value("interaction"), "interaction");
            json::fields(item, {"helper", "inputs", "proposals", "roi_kinds"}, "interaction");
            InteractionMapping mapping;
            mapping.helperNameEn = json::text(item.value("helper"), "interaction.helper");
            for (const auto *field : {"inputs", "proposals"})
            {
                if (!item.contains(field))
                    continue;
                const auto values = json::array(item.value(field), QString{"interaction."} + field);
                auto      &target = std::string_view(field) == "inputs" ? mapping.inputs : mapping.proposals;
                for (qsizetype index = 0; index < values.size(); ++index)
                {
                    const auto context = QString{"interaction."} + field + "[" + QString::number(index) + "]";
                    const auto entry   = json::object(values[index], context);
                    json::fields(entry, {"helper_port_id", std::string_view(field) == "inputs" ? "source" : "target"},
                                 context);
                    const auto helper = portId(entry.value("helper_port_id"), context + ".helper_port_id");
                    if (helper == 0)
                        throw ContractError((context + ".helper_port_id 必须大于 0").toStdString());
                    target.emplace(helper,
                                   reference(entry.value(std::string_view(field) == "inputs" ? "source" : "target"),
                                             context));
                }
            }
            if (item.contains("roi_kinds"))
            {
                for (const auto &value : json::array(item.value("roi_kinds"), "interaction.roi_kinds"))
                {
                    mapping.roiKinds.push_back(json::enumeration<RoiKind>(value, RoiKind::PointSet, "interaction.roi_kinds"));
                }
            }
            definition.interaction = std::move(mapping);
        }
        definition.initParams = ports(document, root, PortStage::Init);
        definition.inputs     = ports(document, root, PortStage::Inputs);
        definition.outputs    = ports(document, root, PortStage::Outputs);
        for (const auto *list : {&definition.initParams, &definition.inputs, &definition.outputs})
            for (const auto &port : *list)
                if (isReservedPort(list == &definition.inputs   ? PortStage::Inputs
                                   : list == &definition.outputs ? PortStage::Outputs
                                                                 : PortStage::Init,
                                   port.id))
                    throw ContractError("工具声明不得定义宿主公共端口：" + portLabel(port.id));
        normalizeToolDefinition(definition);
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
    auto definition = parseToolDefinitionJson(json::read(path).toStdString(), path, std::move(source));
    // The declaration's folder inside its root is the tool's category, so the tree groups by the
    // file system and a folder rename regroups a tool without touching its declaration.
    const auto &root = definition.source.roots.declarations;
    if (!root.empty())
    {
        for (const auto &component : path.lexically_relative(root).parent_path())
        {
            if (component.empty() || component == ".")
                continue;
            const auto utf8 = component.u8string();
            definition.categories.emplace_back(reinterpret_cast<const char *>(utf8.data()), utf8.size());
        }
    }
    return definition;
}

std::string emitNormalizedToolJson(const ToolDefinition &source)
{
    auto definition = source;
    normalizeToolDefinition(definition);
    validateToolDefinition(definition);
    QJsonObject implementation{{"kind", static_cast<int>(definition.kind)}};
    switch (definition.kind)
    {
    case ToolKind::Cpp:
        break;
    case ToolKind::Host:
        implementation.insert("binding", QString::fromStdString(definition.hostBinding));
        break;
    case ToolKind::PythonFunction:
    case ToolKind::PythonClass:
        implementation.insert("package", json::pathText(definition.entry.package));
        implementation.insert("file", json::pathText(definition.entry.file));
        implementation.insert("symbol", QString::fromStdString(definition.entry.symbol));
        break;
    }
    if (!definition.resources.empty())
    {
        QJsonArray resources;
        for (const auto &path : definition.resources) resources.append(json::pathText(path));
        implementation.insert("resources", resources);
    }
    QJsonObject root{
        {"schema_version", 1},
        {"name_en", QString::fromStdString(definition.nameEn)},
        {"name_zh", QString::fromStdString(definition.nameZh)},
        {"version", QString::fromStdString(definition.version)},
        {"implementation", implementation}
    };
    if (definition.visibility != ToolVisibility::Normal)
        root.insert("ui", QJsonObject{{"visibility", static_cast<int>(definition.visibility)}});
    if (auto init = encodePorts(definition.initParams, PortStage::Init); !init.isEmpty())
        root.insert("init_params", init);
    if (auto inputs = encodePorts(definition.inputs, PortStage::Inputs); !inputs.isEmpty())
        root.insert("inputs", inputs);
    if (auto outputs = encodePorts(definition.outputs, PortStage::Outputs); !outputs.isEmpty())
        root.insert("outputs", outputs);
    if (definition.nodeModel != NodeModel::Fixed) root.insert("node_model", static_cast<int>(definition.nodeModel));
    if (definition.training)
    {
        QJsonObject training{{"kind", static_cast<int>(definition.training->kind)},
                             {"asset_param", static_cast<double>(definition.training->assetParam)}};
        if (!definition.training->helper.empty())
            training.insert("helper", QString::fromStdString(definition.training->helper));
        root.insert("training", training);
    }
    if (definition.interaction)
    {
        QJsonObject interaction;
        interaction.insert("helper", QString::fromStdString(definition.interaction->helperNameEn));
        QJsonArray inputs;
        for (const auto &[helper, port] : definition.interaction->inputs)
            inputs.append(QJsonObject{{"helper_port_id", static_cast<double>(helper)}, {"source", referenceJson(port)}});
        if (!inputs.isEmpty())
            interaction.insert("inputs", inputs);
        QJsonArray proposals;
        for (const auto &[helper, port] : definition.interaction->proposals)
            proposals.append(QJsonObject{{"helper_port_id", static_cast<double>(helper)}, {"target", referenceJson(port)}});
        if (!proposals.isEmpty())
            interaction.insert("proposals", proposals);
        if (!definition.interaction->roiKinds.empty())
        {
            QJsonArray kinds;
            for (const auto kind : definition.interaction->roiKinds)
            {
                kinds.append(static_cast<int>(kind));
            }
            interaction.insert("roi_kinds", kinds);
        }
        root.insert("interaction", interaction);
    }
    return QJsonDocument(root).toJson(QJsonDocument::Indented).toStdString();
}

ToolDefinition writeToolDefinitionFile(const std::filesystem::path &path, const ToolDefinition &authored)
{
    if (path.empty())
        throw ContractError("工具声明缺少写入路径");
    ToolDefinition definition = authored;
    normalizeToolDefinition(definition);
    validateToolDefinition(definition);

    const auto content = emitNormalizedToolJson(definition);
    QSaveFile  file(QString::fromStdWString(path.wstring()));
    file.setDirectWriteFallback(false);
    if (!file.open(QIODevice::WriteOnly))
        throw ContractError("无法写入工具声明：" + path.string());
    if (file.write(content.data(), static_cast<qint64>(content.size())) != static_cast<qint64>(content.size()))
    {
        file.cancelWriting();
        throw ContractError("写入工具声明内容失败：" + path.string());
    }
    if (!file.commit())
        throw ContractError("提交工具声明失败：" + path.string());
    definition.definitionPath = path;
    return definition;
}
}
