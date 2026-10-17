#include "core/ToolRegistry.h"
#include "core/ValueYaml.h"
#include "plugins/PluginLoader.h"

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
bool cppKind(ToolKind kind) { return kind == ToolKind::CppClass || kind == ToolKind::CppFunction; }

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
        fields(item, {"id", "name", "type", "dtype", "rank", "layout", "default", "editor", "min", "max",
            "step", "decimals", "options", "options_provider", "tab", "group", "image_input", "image_port", "display"}, direction);
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
                for (const auto& option : item["options"]) port.options.push_back(decodeValue(option, port.type));
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
void validateDefinition(ToolDefinition& definition) {
    systemOutputs(definition);
    validateToolDefinition(definition);
}
void align(const ToolDefinition& definition, const ToolDefinition& contract) {
    if (definition.kind != contract.kind || definition.registryName != contract.registryName)
        throw ContractError("C++ tool kind/entry does not match registered contract: " + definition.id);
    const auto portsMatch = [](const auto& actual, const auto& expected, const char* direction) {
        if (actual.size() != expected.size()) throw ContractError(std::string("C++ contract port count differs: ") + direction);
        for (const auto& port : actual) {
            const auto* other = portById(expected, port.id);
            if (!other || !compatible(port.type, other->type))
                throw ContractError(std::string("C++ contract differs at ") + direction + "." + port.id);
        }
    };
    portsMatch(definition.initParams, contract.initParams, "init_params");
    portsMatch(definition.inputs, contract.inputs, "inputs");
    portsMatch(definition.outputs, contract.outputs, "outputs");
}
using OrderedPath = std::pair<std::u8string, std::filesystem::path>;
void collect(const std::filesystem::path& directory, int depth, std::vector<OrderedPath>& paths,
             std::vector<RegistryDiagnostic>& diagnostics) {
    std::error_code error;
    std::filesystem::directory_iterator iterator(directory, error), end;
    if (error) {
        if (error != std::errc::no_such_file_or_directory) diagnostics.push_back({directory, error.message(), {}});
        return;
    }
    for (; iterator != end; iterator.increment(error)) {
        if (error) { diagnostics.push_back({directory, error.message(), {}}); break; }
        const auto entry = *iterator;
        if (entry.is_directory(error)) {
            if (depth < 2 && !entry.is_symlink(error)) collect(entry.path(), depth + 1, paths, diagnostics);
        } else if (!error && entry.is_regular_file(error)) {
            auto extension = entry.path().extension().string();
            std::transform(extension.begin(), extension.end(), extension.begin(), [](unsigned char c) {
                return c >= 'A' && c <= 'Z' ? static_cast<char>(c + ('a' - 'A')) : static_cast<char>(c);
            });
            if (extension == ".yaml" || extension == ".yml") {
                if (depth == 2) {
                    auto path = entry.path().lexically_normal();
                    paths.emplace_back(path.generic_u8string(), std::move(path));
                } else diagnostics.push_back({entry.path(), "Tool YAML requires category/subcategory directories", {}});
            }
        }
        if (error) { diagnostics.push_back({entry.path(), error.message(), {}}); error.clear(); }
    }
    if (error) diagnostics.push_back({directory, error.message(), {}});
}
} // namespace

ToolDefinition parseDefinitionNode(const YAML::Node& node, const std::filesystem::path& path, bool builtin) {
    try {
        fields(node, {"schema_version", "tool_id", "display_name", "version", "kind", "entry", "init_params", "inputs", "outputs"}, "Tool definition");
        if (parseInt64(text(node["schema_version"], "schema_version")) != 1) throw ContractError("Unsupported schema_version");
        ToolDefinition definition;
        definition.id = text(node["tool_id"], "tool_id", true);
        definition.displayName = text(node["display_name"], "display_name", true);
        definition.version = text(node["version"], "version", true);
        definition.builtin = builtin;
        definition.definitionPath = path.lexically_normal();
        definition.subcategory = pathText(path.parent_path().filename());
        definition.category = pathText(path.parent_path().parent_path().filename());
        const auto kind = text(node["kind"], "kind");
        if (kind == "cpp_function") definition.kind = ToolKind::CppFunction;
        else if (kind == "cpp_class") definition.kind = ToolKind::CppClass;
        else if (kind == "python_function") definition.kind = ToolKind::PythonFunction;
        else if (kind == "python_class") definition.kind = ToolKind::PythonClass;
        else throw ContractError("Unknown tool kind: " + kind);
        const auto entry = node["entry"];
        if (cppKind(definition.kind)) {
            fields(entry, {"registry_name"}, "C++ entry");
            definition.registryName = text(entry["registry_name"], "registry_name", true);
        } else {
            fields(entry, {"file", "symbol"}, "Python entry");
            const auto file = std::filesystem::u8path(text(entry["file"], "entry.file", true));
            if (file.is_absolute() || file.has_root_name()) throw ContractError("Python entry.file must be relative to its YAML");
            definition.script = (path.parent_path() / file).lexically_normal();
            definition.symbol = text(entry["symbol"], "entry.symbol", true);
        }
        definition.initParams = parsePorts(node["init_params"], "init_params");
        definition.inputs = parsePorts(node["inputs"], "inputs");
        definition.outputs = parsePorts(node["outputs"], "outputs");
        validateDefinition(definition);
        return definition;
    } catch (const std::exception& error) {
        throw ContractError(pathText(path) + ": " + error.what());
    }
}

ToolDefinition parseDefinition(const std::filesystem::path& path, bool builtin) {
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

void ToolRegistry::scan(const std::filesystem::path& toolsRoot) {
    scanDefinitions(toolsRoot, false);
}
void ToolRegistry::scanPython(const std::filesystem::path& toolsRoot) {
    scanDefinitions(toolsRoot, true);
}
void ToolRegistry::scanDefinitions(const std::filesystem::path& toolsRoot, bool pythonOnly) {
    std::set<std::filesystem::path> cppPaths;
    std::map<std::filesystem::path, std::string> oldPython;
    if (pythonOnly) {
        std::erase_if(definitions_, [&](const auto& definition) {
            if (cppKind(definition.kind)) {
                cppPaths.insert(definition.definitionPath);
                return false;
            }
            oldPython.emplace(definition.definitionPath, definition.id);
            return true;
        });
    } else {
        definitions_.clear();
    }
    index_.clear();
    for (std::size_t i = 0; i < definitions_.size(); ++i) index_.emplace(definitions_[i].id, i);
    diagnostics_.clear();
    python_.clear();
    for (const bool builtin : {true, false}) {
        std::vector<OrderedPath> paths;
        const auto root = (toolsRoot / (builtin ? "builtin" : "custom")).lexically_normal();
        collect(root, 0, paths, diagnostics_);
        // The common root prefix preserves normalized relative-path ordering.
        std::sort(paths.begin(), paths.end(), [](const auto& left, const auto& right) {
            return left.first < right.first;
        });
        for (const auto& candidate : paths) {
            const auto& path = candidate.second;
            if (pythonOnly && cppPaths.contains(path)) continue;
            oldPython.erase(path);
            try {
                auto definition = parseDefinition(path, builtin);
                if (pythonOnly && cppKind(definition.kind)) {
                    diagnostics_.push_back({path, "New C++ definitions require application restart", {}});
                    continue;
                }
                if (const auto* previous = find(definition.id)) {
                    diagnostics_.push_back({path, "Duplicate tool_id " + definition.id + "; first definition: " + pathText(previous->definitionPath), previous->definitionPath});
                    continue;
                }
                addDefinition(std::move(definition));
            } catch (const std::exception& error) {
                diagnostics_.push_back({path, error.what(), {}});
            }
        }
    }
    for (const auto& [path, id] : oldPython)
        diagnostics_.push_back({path, "Python definition missing after refresh: " + id, {}});
}
void ToolRegistry::addDefinition(ToolDefinition definition) {
    validateDefinition(definition);
    if (const auto* previous = find(definition.id))
        throw ContractError("Duplicate tool_id " + definition.id + ": " + pathText(previous->definitionPath) + " and " + pathText(definition.definitionPath));
    validateContract(definition);
    const auto index = definitions_.size();
    const auto id = definition.id;
    definitions_.push_back(std::move(definition));
    index_.emplace(id, index);
}
void ToolRegistry::replaceDefinition(ToolDefinition definition) {
    const auto existing = index_.find(definition.id);
    if (existing == index_.end()) {
        addDefinition(std::move(definition));
        return;
    }
    validateDefinition(definition);
    validateContract(definition);
    definitions_[existing->second] = std::move(definition);
    python_.erase(existing->first);
}
void ToolRegistry::registerCpp(std::string registryName, ToolDefinition contract, ToolFactory factory) {
    if (registryName.empty() || !factory || !cppKind(contract.kind)) throw ContractError("Invalid C++ factory registration");
    if (contract.registryName.empty()) contract.registryName = registryName;
    if (contract.registryName != registryName) throw ContractError("C++ registry_name disagrees with contract");
    if (cpp_.contains(registryName)) throw ContractError("Duplicate C++ registry_name: " + registryName);
    validateDefinition(contract);
    for (const auto& definition : definitions_)
        if (cppKind(definition.kind) && definition.registryName == registryName) align(definition, contract);
    cpp_.emplace(std::move(registryName), CppImplementation{std::move(contract), std::move(factory)});
}
void ToolRegistry::validateContract(const ToolDefinition& definition) const {
    if (cppKind(definition.kind)) {
        const auto implementation = cpp_.find(definition.registryName);
        if (implementation != cpp_.end()) align(definition, implementation->second.contract);
    }
}
std::vector<const ToolDefinition*> ToolRegistry::cppContracts() const {
    std::vector<const ToolDefinition*> contracts;
    contracts.reserve(cpp_.size());
    for (const auto& [name, implementation] : cpp_) contracts.push_back(&implementation.contract);
    return contracts;
}
void ToolRegistry::setPythonImplementation(std::string toolId, ToolFactory factory, ToolStatus status) {
    const auto* definition = find(toolId);
    if (!definition || cppKind(definition->kind)) throw ContractError("Python implementation requires a loaded Python definition: " + toolId);
    python_.insert_or_assign(std::move(toolId), PythonImplementation{std::move(factory), std::move(status)});
}
const ToolDefinition* ToolRegistry::find(std::string_view id) const {
    const auto found = index_.find(id);
    return found == index_.end() ? nullptr : &definitions_[found->second];
}
Availability ToolRegistry::availability(std::string_view id) const {
    const auto* definition = find(id);
    if (!definition) return {ToolAvailability::DefinitionMissing, "Tool definition missing: " + std::string(id)};
    if (cppKind(definition->kind)) {
        if (cpp_.contains(definition->registryName)) return {ToolAvailability::Available, {}};
        if (pluginLoader_ && pluginLoader_->hasModule(definition->registryName)) return {ToolAvailability::Available, {}};
        return {ToolAvailability::ImplementationUnavailable, "C++ implementation unavailable: " + definition->registryName + " (" + pathText(definition->definitionPath) + ")"};
    }
    const auto implementation = python_.find(id);
    if (implementation != python_.end() && implementation->second.factory && implementation->second.status)
        return {ToolAvailability::Available, {}};
    return {ToolAvailability::ImplementationUnavailable, "Python implementation unavailable: " + std::string(id) +
        (implementation != python_.end() && !implementation->second.status.message.empty() ? ": " + implementation->second.status.message : " (not loaded)")};
}
std::unique_ptr<IToolInstance> ToolRegistry::create(std::string_view id, const PlatformToolServices& services) const {
    const auto state = availability(id);
    if (state.state != ToolAvailability::Available) throw RegistryError(state.state, state.message);
    const auto& definition = *find(id);
    if (cppKind(definition.kind)) {
        const auto it = cpp_.find(definition.registryName);
        if (it != cpp_.end() && it->second.factory) {
            auto instance = it->second.factory(services);
            if (!instance) throw RegistryError(ToolAvailability::ImplementationUnavailable, "Tool factory returned no instance: " + std::string(id));
            return instance;
        }
        if (pluginLoader_ && pluginLoader_->hasModule(definition.registryName)) {
            auto instance = pluginLoader_->createAdapter(definition.registryName, services);
            if (!instance) throw RegistryError(ToolAvailability::ImplementationUnavailable, "Plugin loader returned no adapter: " + std::string(id));
            return instance;
        }
    }
    const auto& factory = python_.find(id)->second.factory;
    auto instance = factory(services);
    if (!instance) throw RegistryError(ToolAvailability::ImplementationUnavailable, "Tool factory returned no instance: " + std::string(id));
    return instance;
}

} // namespace visionrt
