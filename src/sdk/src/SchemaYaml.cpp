#include "visionrt/sdk/SchemaYaml.h"
#include "visionrt/sdk/ValueYaml.h"

#include <algorithm>
#include <cmath>
#include <fstream>
#include <limits>
#include <set>

namespace visionrt {

namespace {

std::string pathText(const std::filesystem::path& path) {
    const auto text = path.generic_u8string();
    return {reinterpret_cast<const char*>(text.data()), text.size()};
}

void fields(const YAML::Node& node, std::initializer_list<std::string_view> allowed,
            const std::string& context) {
    if (!node.IsMap()) throw ContractError(context + " must be a mapping");
    std::set<std::string> seen;
    for (const auto& field : node) {
        if (!field.first.IsScalar()) throw ContractError(context + " has a non-scalar key");
        const auto key = field.first.Scalar();
        if (!seen.insert(key).second) throw ContractError(context + " has duplicate key: " + key);
        if (std::find(allowed.begin(), allowed.end(), key) == allowed.end())
            throw ContractError(context + " has unknown field: " + key);
    }
}

std::string text(const YAML::Node& node, const std::string& context, bool nonempty = false) {
    if (!node.IsScalar()) throw ContractError(context + " must be a string scalar");
    auto value = node.Scalar();
    validateUtf8(value);
    if (nonempty && value.empty()) throw ContractError(context + " must not be empty");
    return value;
}


std::vector<PortDefinition> parsePorts(const YAML::Node& node, const std::string& direction) {
    if (!node.IsSequence()) throw ContractError(direction + " must be a sequence");
    const bool output = direction == "outputs";
    std::vector<PortDefinition> ports;
    ports.reserve(node.size());
    for (const auto& item : node) {
        if (output) {
            fields(item, {"id", "name", "type", "dtype", "rank", "layout", "image_port", "display"}, direction);
        } else {
            fields(item, {"id", "name", "type", "dtype", "rank", "layout", "default", "editor", "min", "max",
                "step", "decimals", "options", "options_provider", "tab", "group", "image_input"}, direction);
        }
        PortDefinition port;
        port.id = text(item["id"], direction + ".id", false);
        const auto context = direction + "." + port.id;
        try {
            port.name = text(item["name"], "name", false);
            port.type = decodeTypeSpec(item);
            if (item["default"]) port.defaultValue = decodeValue(item["default"], port.type);
            if (item["editor"]) port.editor = text(item["editor"], "editor");
            if (item["tab"]) port.tab = text(item["tab"], "tab");
            if (item["group"]) port.group = text(item["group"], "group");
            if (item["min"]) port.minimum = decodeValue(item["min"], port.type);
            if (item["max"]) port.maximum = decodeValue(item["max"], port.type);
            if (item["step"]) port.step = decodeValue(item["step"], port.type);
            if (item["decimals"]) {
                const auto decimals = parseInt64(text(item["decimals"], "decimals"));
                if (decimals < std::numeric_limits<int>::min() || decimals > std::numeric_limits<int>::max())
                    throw ContractError("Invalid decimals");
                port.decimals = static_cast<int>(decimals);
            }
            if (item["options"]) {
                if (!item["options"].IsSequence()) throw ContractError("options must be a sequence");
                port.options.reserve(item["options"].size());
                for (const auto& option : item["options"]) {
                    port.options.push_back(decodeValue(option, port.type));
                }
            }
            if (item["options_provider"]) port.optionsProvider = text(item["options_provider"], "options_provider");
            if (item["image_input"]) port.imageInput = text(item["image_input"], "image_input", false);
            if (item["image_port"]) port.imagePort = text(item["image_port"], "image_port", false);
            if (item["display"]) {
                const auto display = item["display"];
                fields(display, {"color_order", "same_geometry_as_input"}, "display");
                if (display["color_order"]) port.colorOrder = text(display["color_order"], "color_order");
                if (display["same_geometry_as_input"])
                    port.sameGeometryAsInput = text(display["same_geometry_as_input"], "same_geometry_as_input", false);
            }
        } catch (const std::exception& error) {
            throw ContractError(context + ": " + error.what());
        }
        ports.push_back(std::move(port));
    }
    return ports;
}

const PortDefinition* portById(const std::vector<PortDefinition>& ports, std::string_view id) {
    const auto found = std::find_if(ports.begin(), ports.end(), [&](const auto& port) { return port.id == id; });
    return found == ports.end() ? nullptr : &*found;
}

void systemOutputs(ToolDefinition& definition) {
    const auto* code = portById(definition.outputs, "__status_code");
    const auto* message = portById(definition.outputs, "__status_message");
    if (code || message) {
        if (definition.outputs.size() < 2 || definition.outputs[0].id != "__status_code" ||
            definition.outputs[1].id != "__status_message" || code->type != TypeSpec{Type::Int} ||
            message->type != TypeSpec{Type::String}) throw ContractError("Invalid automatic system outputs");
        return;
    }
    PortDefinition statusCode;
    statusCode.id = "__status_code";
    statusCode.name = "状态码";
    statusCode.type = {Type::Int};
    PortDefinition statusMessage;
    statusMessage.id = "__status_message";
    statusMessage.name = "状态消息";
    statusMessage.type = {Type::String};
    definition.outputs.insert(definition.outputs.begin(), {std::move(statusCode), std::move(statusMessage)});
}

ToolDefinition parseDefinitionNode(const YAML::Node& node, const std::filesystem::path& path, bool builtin) {
    try {
        fields(node, {"schema_version", "tool_id", "display_name", "version", "kind", "entry", "init_params", "inputs", "outputs"}, "Tool definition");
        if (parseInt64(text(node["schema_version"], "schema_version")) != 1) throw ContractError("Unsupported schema_version");
        ToolDefinition definition;
        definition.id = text(node["tool_id"], "tool_id", true);
        definition.displayName = text(node["display_name"], "display_name", true);
        definition.version = text(node["version"], "version", true);
        definition.builtin = builtin;
        if (!path.empty()) {
            definition.definitionPath = path.lexically_normal();
            definition.subcategory = pathText(path.parent_path().filename());
            definition.category = pathText(path.parent_path().parent_path().filename());
        }
        const auto kind = text(node["kind"], "kind");
        if (kind == "cpp_function") definition.kind = ToolKind::CppFunction;
        else if (kind == "cpp_class") definition.kind = ToolKind::CppClass;
        else if (kind == "python_function") definition.kind = ToolKind::PythonFunction;
        else if (kind == "python_class") definition.kind = ToolKind::PythonClass;
        else throw ContractError("Unknown tool kind: " + kind);

        const auto entry = node["entry"];
        if (definition.kind == ToolKind::CppClass || definition.kind == ToolKind::CppFunction) {
            fields(entry, {"registry_name"}, "C++ entry");
            definition.registryName = text(entry["registry_name"], "registry_name", true);
        } else {
            fields(entry, {"file", "symbol"}, "Python entry");
            const auto fileStr = text(entry["file"], "entry.file", true);
            const auto file = std::filesystem::path(std::u8string_view(
                reinterpret_cast<const char8_t*>(fileStr.data()), fileStr.size()));
            if (file.is_absolute() || file.has_root_name()) throw ContractError("Python entry.file must be relative to its YAML");
            if (!path.empty()) {
                definition.script = (path.parent_path() / file).lexically_normal();
            } else {
                definition.script = file;
            }
            definition.symbol = text(entry["symbol"], "entry.symbol", true);
        }
        definition.initParams = parsePorts(node["init_params"], "init_params");
        definition.inputs = parsePorts(node["inputs"], "inputs");
        definition.outputs = parsePorts(node["outputs"], "outputs");

        systemOutputs(definition);
        validateToolDefinition(definition);
        return definition;
    } catch (const std::exception& error) {
        const auto prefix = path.empty() ? std::string{} : (pathText(path) + ": ");
        throw ContractError(prefix + error.what());
    }
}

} // namespace

ToolDefinition parseToolDefinitionYaml(const std::string& yamlContent,
                                       const std::filesystem::path& definitionPath,
                                       bool builtin) {
    YAML::Node node;
    try {
        node = YAML::Load(yamlContent);
    } catch (const std::exception& error) {
        throw ContractError("Invalid YAML: " + std::string(error.what()));
    }
    return parseDefinitionNode(node, definitionPath, builtin);
}

ToolDefinition parseToolDefinitionFile(const std::filesystem::path& path, bool builtin) {
    YAML::Node node;
    try {
        std::ifstream input(path, std::ios::binary);
        if (!input) throw ContractError("Cannot open tool definition");
        node = YAML::Load(input);
    } catch (const std::exception& error) {
        throw ContractError(pathText(path) + ": " + error.what());
    }
    return parseDefinitionNode(node, path, builtin);
}

void validateToolYaml(const std::string& yamlContent,
                      const std::filesystem::path& definitionPath,
                      bool builtin) {
    parseToolDefinitionYaml(yamlContent, definitionPath, builtin);
}

} // namespace visionrt
