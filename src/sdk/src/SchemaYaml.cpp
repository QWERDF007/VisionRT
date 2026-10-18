#include "visionrt/sdk/SchemaYaml.h"
#include "visionrt/sdk/ValueYaml.h"

#include <algorithm>
#include <cmath>
#include <fstream>
#include <limits>
#include <set>
#include <sstream>
#include <unordered_set>
#include <yaml-cpp/parser.h>
#include <yaml-cpp/eventhandler.h>

namespace visionrt {

namespace {

class DuplicateKeyDetector : public YAML::EventHandler {
public:
    enum class ContainerType { Sequence, Map };
    struct Frame {
        ContainerType type;
        bool isKey{true};
        std::set<std::string> keys;
    };
    std::vector<Frame> stack;

    void OnDocumentStart(const YAML::Mark&) override {}
    void OnDocumentEnd() override {}
    void OnNull(const YAML::Mark&, YAML::anchor_t) override { advance(); }
    void OnAlias(const YAML::Mark&, YAML::anchor_t) override { advance(); }

    void OnScalar(const YAML::Mark&, const std::string&, YAML::anchor_t, const std::string& value) override {
        if (!stack.empty() && stack.back().type == ContainerType::Map && stack.back().isKey) {
            if (stack.back().keys.contains(value)) {
                throw ContractError("Duplicate key detected in YAML: " + value);
            }
            stack.back().keys.insert(value);
            stack.back().isKey = false;
        } else {
            advance();
        }
    }

    void OnSequenceStart(const YAML::Mark&, const std::string&, YAML::anchor_t, YAML::EmitterStyle::value) override {
        stack.push_back({ContainerType::Sequence, false, {}});
    }
    void OnSequenceEnd() override {
        if (!stack.empty()) stack.pop_back();
        advance();
    }

    void OnMapStart(const YAML::Mark&, const std::string&, YAML::anchor_t, YAML::EmitterStyle::value) override {
        stack.push_back({ContainerType::Map, true, {}});
    }
    void OnMapEnd() override {
        if (!stack.empty()) stack.pop_back();
        advance();
    }

private:
    void advance() {
        if (!stack.empty() && stack.back().type == ContainerType::Map && !stack.back().isKey) {
            stack.back().isKey = true;
        }
    }
};

void checkDuplicateKeys(std::istream& stream) {
    try {
        YAML::Parser parser(stream);
        DuplicateKeyDetector detector;
        while (parser.HandleNextDocument(detector)) {}
    } catch (const ContractError&) {
        throw;
    } catch (const std::exception& error) {
        throw ContractError("YAML parse error: " + std::string(error.what()));
    }
}

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
    {
        std::istringstream stream(yamlContent);
        checkDuplicateKeys(stream);
    }
    YAML::Node node;
    try {
        node = YAML::Load(yamlContent);
    } catch (const std::exception& error) {
        throw ContractError("Invalid YAML: " + std::string(error.what()));
    }
    return parseDefinitionNode(node, definitionPath, builtin);
}

ToolDefinition parseToolDefinitionFile(const std::filesystem::path& path, bool builtin) {
    std::string content;
    {
        std::ifstream input(path, std::ios::binary);
        if (!input) throw ContractError("Cannot open tool definition");
        std::stringstream buffer;
        buffer << input.rdbuf();
        content = buffer.str();
    }
    {
        std::istringstream stream(content);
        checkDuplicateKeys(stream);
    }
    YAML::Node node;
    try {
        node = YAML::Load(content);
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

ModuleIndexData parseModuleIndexNode(const YAML::Node& node, const std::filesystem::path& path = {}) {
    if (!node.IsMap()) throw ContractError("Module index YAML root must be a mapping");
    fields(node, {"variant", "modules", "tools", "runtime_files"}, "module index root");

    ModuleIndexData result;

    if (!node["variant"]) throw ContractError("Missing required field 'variant'");
    result.variant = text(node["variant"], "variant", true);
    if (result.variant.empty()) throw ContractError("variant must not be empty");

    if (!node["modules"]) throw ContractError("Missing required field 'modules'");
    if (!node["modules"].IsMap()) throw ContractError("modules must be a mapping");
    if (node["modules"].size() == 0) throw ContractError("modules must not be empty");

    for (const auto& modField : node["modules"]) {
        if (!modField.first.IsScalar()) throw ContractError("Module name in 'modules' must be a scalar");
        const auto modName = modField.first.as<std::string>();
        if (modName.empty()) throw ContractError("Module name in 'modules' must not be empty");
        const auto& modNode = modField.second;
        if (!modNode.IsMap()) throw ContractError("Module '" + modName + "' entry must be a mapping");
        fields(modNode, {"library"}, "Module '" + modName + "'");

        if (!modNode["library"]) throw ContractError("Module '" + modName + "' missing required field 'library'");
        const auto libStr = text(modNode["library"], "library", true);
        if (libStr.empty()) throw ContractError("Module '" + modName + "' library path must not be empty");

        const auto libPath = std::filesystem::path(std::u8string(reinterpret_cast<const char8_t*>(libStr.data()), libStr.size()));
        if (libPath.is_absolute() || libPath.has_root_name()) {
            throw ContractError("Module '" + modName + "' library path must be relative: " + libStr);
        }
        const auto normLib = libPath.lexically_normal();
        if (!normLib.empty() && normLib.begin()->string() == "..") {
            throw ContractError("Module '" + modName + "' library path escapes root: " + libStr);
        }

        result.modules[modName] = ModuleEntry{libStr};
    }

    if (!node["tools"]) throw ContractError("Missing required field 'tools'");
    if (!node["tools"].IsMap()) throw ContractError("tools must be a mapping");
    if (node["tools"].size() == 0) throw ContractError("tools must not be empty");

    for (const auto& toolField : node["tools"]) {
        if (!toolField.first.IsScalar()) throw ContractError("Tool registry name in 'tools' must be a scalar");
        const auto regName = toolField.first.as<std::string>();
        if (regName.empty()) throw ContractError("Tool registry name in 'tools' must not be empty");
        const auto& toolNode = toolField.second;
        if (!toolNode.IsMap()) throw ContractError("Tool '" + regName + "' entry must be a mapping");
        fields(toolNode, {"module", "definition"}, "Tool '" + regName + "'");

        if (!toolNode["module"]) throw ContractError("Tool '" + regName + "' missing required field 'module'");
        const auto modRef = text(toolNode["module"], "module", true);
        if (modRef.empty()) throw ContractError("Tool '" + regName + "' module reference must not be empty");

        if (!result.modules.contains(modRef)) {
            throw ContractError("Tool '" + regName + "' references undeclared module: '" + modRef + "'");
        }

        if (!toolNode["definition"]) throw ContractError("Tool '" + regName + "' missing required field 'definition'");
        const auto defStr = text(toolNode["definition"], "definition", true);
        if (defStr.empty()) throw ContractError("Tool '" + regName + "' definition path must not be empty");

        const auto defPath = std::filesystem::path(std::u8string(reinterpret_cast<const char8_t*>(defStr.data()), defStr.size()));
        if (defPath.is_absolute() || defPath.has_root_name()) {
            throw ContractError("Tool '" + regName + "' definition path must be relative: " + defStr);
        }
        const auto normDef = defPath.lexically_normal();
        if (!normDef.empty() && normDef.begin()->string() == "..") {
            throw ContractError("Tool '" + regName + "' definition path escapes root: " + defStr);
        }

        result.tools[regName] = ToolIndexEntry{modRef, defStr};
    }

    std::unordered_set<std::string> referencedModules;
    for (const auto& [toolName, toolEntry] : result.tools) {
        referencedModules.insert(toolEntry.module);
    }
    for (const auto& [modName, modEntry] : result.modules) {
        if (!referencedModules.contains(modName)) {
            throw ContractError("Module '" + modName + "' is not referenced by any tool");
        }
    }

    if (node["runtime_files"]) {
        if (!node["runtime_files"].IsSequence()) {
            throw ContractError("runtime_files must be a sequence");
        }
        for (std::size_t i = 0; i < node["runtime_files"].size(); ++i) {
            const auto rfNode = node["runtime_files"][i];
            const auto rfStr = text(rfNode, "runtime_files[" + std::to_string(i) + "]", true);
            if (rfStr.empty()) throw ContractError("runtime_files entry must not be empty");
            const auto rfPath = std::filesystem::path(std::u8string(reinterpret_cast<const char8_t*>(rfStr.data()), rfStr.size()));
            if (rfPath.is_absolute() || rfPath.has_root_name()) {
                throw ContractError("runtime_files entry must be relative: " + rfStr);
            }
            const auto normRf = rfPath.lexically_normal();
            if (!normRf.empty() && normRf.begin()->string() == "..") {
                throw ContractError("runtime_files entry escapes root: " + rfStr);
            }
            result.runtimeFiles.push_back(rfStr);
        }
    }

    return result;
}

ModuleIndexData parseModuleIndexYaml(const std::string& yamlContent) {
    {
        std::istringstream stream(yamlContent);
        checkDuplicateKeys(stream);
    }
    YAML::Node node;
    try {
        node = YAML::Load(yamlContent);
    } catch (const std::exception& error) {
        throw ContractError("Invalid YAML: " + std::string(error.what()));
    }
    return parseModuleIndexNode(node);
}

ModuleIndexData parseModuleIndexFile(const std::filesystem::path& path) {
    std::string content;
    {
        std::ifstream input(path, std::ios::binary);
        if (!input) throw ContractError("Cannot open module index file: " + pathText(path));
        std::stringstream buffer;
        buffer << input.rdbuf();
        content = buffer.str();
    }
    {
        std::istringstream stream(content);
        checkDuplicateKeys(stream);
    }
    YAML::Node node;
    try {
        node = YAML::Load(content);
    } catch (const std::exception& error) {
        throw ContractError(pathText(path) + ": " + error.what());
    }
    return parseModuleIndexNode(node, path);
}

void validateModuleIndexYaml(const std::string& yamlContent) {
    parseModuleIndexYaml(yamlContent);
}

void validateModuleIndexFile(const std::filesystem::path& path) {
    parseModuleIndexFile(path);
}

void validateToolsPackage(const std::filesystem::path& packageRoot) {
    std::error_code ec;
    if (!std::filesystem::is_directory(packageRoot, ec) || ec) {
        throw ContractError("Package root is not a valid directory: " + pathText(packageRoot));
    }
    const auto indexFile = packageRoot / "module_index.yaml";
    if (!std::filesystem::is_regular_file(indexFile, ec) || ec) {
        throw ContractError("Missing module_index.yaml in package: " + pathText(packageRoot));
    }

    const auto index = parseModuleIndexFile(indexFile);

    auto toPath = [](std::string_view utf8Str) {
        return std::filesystem::path(std::u8string_view(reinterpret_cast<const char8_t*>(utf8Str.data()), utf8Str.size()));
    };

    // Verify module libraries exist
    for (const auto& [modName, modEntry] : index.modules) {
        const auto libPath = (packageRoot / toPath(modEntry.library)).lexically_normal();
        if (!std::filesystem::is_regular_file(libPath, ec) || ec) {
            throw ContractError("Module '" + modName + "' library file does not exist: " + pathText(libPath));
        }
    }

    // Verify tools and definitions exist and match
    for (const auto& [regName, toolEntry] : index.tools) {
        const auto defPath = (packageRoot / toPath(toolEntry.definition)).lexically_normal();
        if (!std::filesystem::is_regular_file(defPath, ec) || ec) {
            throw ContractError("Tool '" + regName + "' definition file does not exist: " + pathText(defPath));
        }
        const auto definition = parseToolDefinitionFile(defPath);
        if (definition.registryName != regName) {
            throw ContractError("Tool '" + regName + "' definition registry_name '" + definition.registryName + "' does not match index tool key");
        }
    }

    // Verify runtime files exist
    for (const auto& rf : index.runtimeFiles) {
        const auto rfPath = (packageRoot / toPath(rf)).lexically_normal();
        if (!std::filesystem::is_regular_file(rfPath, ec) || ec) {
            throw ContractError("runtime_files entry does not exist: " + pathText(rfPath));
        }
    }
}

} // namespace visionrt
