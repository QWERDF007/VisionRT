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

void oneOf(const std::string& value, std::initializer_list<std::string_view> choices,
           const std::string& context) {
    if (std::find(choices.begin(), choices.end(), value) == choices.end())
        throw ContractError("Unknown " + context + ": " + value);
}

void identifier(const std::string& value, bool port) {
    const auto letter = [](char c) { return (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z'); };
    if (value.empty() || (!letter(value.front()) && value.front() != '_') ||
        !std::all_of(value.begin(), value.end(), [&](char c) {
            return letter(c) || (c >= '0' && c <= '9') || c == '_' || c == '.' || c == '-';
        })) throw ContractError("ID must be a stable English identifier: " + value);
    if (port && value.starts_with("__")) throw ContractError("Port ID uses reserved __ prefix: " + value);
}

bool lessNumber(const PortValue& left, const PortValue& right, Type type) {
    if (type == Type::Int) return std::get<std::int64_t>(left) < std::get<std::int64_t>(right);
    return std::get<double>(left) < std::get<double>(right);
}

std::vector<PortDefinition> parsePorts(const YAML::Node& node, const std::string& direction) {
    if (!node.IsSequence()) throw ContractError(direction + " must be a sequence");
    const bool output = direction == "outputs";
    std::vector<PortDefinition> ports;
    ports.reserve(node.size());
    std::set<std::string> ids;
    for (const auto& item : node) {
        if (output) {
            fields(item, {"id", "name", "type", "dtype", "rank", "layout", "image_port", "display"}, direction);
        } else {
            fields(item, {"id", "name", "type", "dtype", "rank", "layout", "default", "editor", "min", "max",
                "step", "decimals", "options", "options_provider", "tab", "group", "image_input"}, direction);
        }
        PortDefinition port;
        port.id = text(item["id"], direction + ".id", true);
        identifier(port.id, true);
        const auto context = direction + "." + port.id;
        if (!ids.insert(port.id).second) throw ContractError("Duplicate " + context);
        try {
            port.name = text(item["name"], "name", true);
            port.type = decodeTypeSpec(item);
            if (output) {
                for (const auto key : {"default", "editor", "min", "max", "step", "decimals", "options", "options_provider", "tab", "group", "image_input"})
                    if (item[key]) throw ContractError(std::string(key) + " is not an output field");
            } else if (item["display"] || item["image_port"]) {
                throw ContractError("display and image_port apply only to outputs");
            }
            if (item["default"]) port.defaultValue = decodeValue(item["default"], port.type);
            if (item["editor"]) {
                port.editor = text(item["editor"], "editor");
                oneOf(port.editor, {"number", "text", "bool", "file", "directory", "array", "roi"}, "editor");
            }
            if (item["tab"]) port.tab = text(item["tab"], "tab");
            oneOf(port.tab, {"基本输入", "预处理", "检测", "结果处理"}, "tab");
            if (item["group"]) port.group = text(item["group"], "group");
            const bool numeric = port.type.type == Type::Int || port.type.type == Type::Float;
            const auto bound = [&](const char* key) -> std::optional<PortValue> {
                if (!item[key]) return {};
                if (!numeric) throw ContractError(std::string(key) + " requires an Int or Float port");
                auto value = decodeValue(item[key], port.type);
                if (isMissing(value) || (port.type.type == Type::Float && !std::isfinite(std::get<double>(value))))
                    throw ContractError(std::string(key) + " must be a finite numeric value");
                return value;
            };
            port.minimum = bound("min");
            port.maximum = bound("max");
            port.step = bound("step");
            if (port.minimum && port.maximum && lessNumber(*port.maximum, *port.minimum, port.type.type))
                throw ContractError("min must not exceed max");
            if (port.step) {
                const PortValue zero = port.type.type == Type::Int ? PortValue{std::int64_t{0}} : PortValue{0.0};
                if (!lessNumber(zero, *port.step, port.type.type)) throw ContractError("step must be positive");
            }
            if (item["decimals"]) {
                if (!numeric) throw ContractError("decimals requires an Int or Float port");
                const auto decimals = parseInt64(text(item["decimals"], "decimals"));
                if (decimals < 0 || decimals > std::numeric_limits<int>::max()) throw ContractError("Invalid decimals");
                port.decimals = static_cast<int>(decimals);
            }
            if (!isMissing(port.defaultValue) && numeric) {
                if (port.type.type == Type::Float && !std::isfinite(std::get<double>(port.defaultValue)))
                    throw ContractError("Numeric default must be finite");
                if ((port.minimum && lessNumber(port.defaultValue, *port.minimum, port.type.type)) ||
                    (port.maximum && lessNumber(*port.maximum, port.defaultValue, port.type.type)))
                    throw ContractError("Default is outside min/max");
            }
            if (item["options"]) {
                if (!item["options"].IsSequence()) throw ContractError("options must be a sequence");
                port.options.reserve(item["options"].size());
                for (const auto& option : item["options"]) {
                    auto optVal = decodeValue(option, port.type);
                    if (numeric && port.type.type == Type::Float && !std::isfinite(std::get<double>(optVal)))
                        throw ContractError("option must be a finite numeric value");
                    port.options.push_back(std::move(optVal));
                }
            }
            if (item["options_provider"]) {
                port.optionsProvider = text(item["options_provider"], "options_provider");
                oneOf(port.optionsProvider, {"platform.gpu_devices"}, "options_provider");
            }
            if (item["image_input"]) {
                if (direction != "inputs" || port.type.type != Type::RoiBatch)
                    throw ContractError("image_input requires a RoiBatch input");
                port.imageInput = text(item["image_input"], "image_input", true);
            }
            if (item["image_port"]) {
                if (port.type.type != Type::RoiBatch) throw ContractError("image_port requires a RoiBatch output");
                port.imagePort = text(item["image_port"], "image_port", true);
            }
            if (item["display"]) {
                if (port.type.type != Type::ImageBatch) throw ContractError("display requires an ImageBatch output");
                const auto display = item["display"];
                fields(display, {"color_order", "same_geometry_as_input"}, "display");
                if (display["color_order"]) {
                    port.colorOrder = text(display["color_order"], "color_order");
                    oneOf(port.colorOrder, {"Gray", "BGR", "BGRA", "RGB", "RGBA"}, "color_order");
                }
                if (display["same_geometry_as_input"])
                    port.sameGeometryAsInput = text(display["same_geometry_as_input"], "same_geometry_as_input", true);
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
