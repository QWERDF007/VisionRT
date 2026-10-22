#pragma once

#include "core/ToolDefinition.h"
#include <filesystem>
#include <string>
#include <unordered_map>
#include <vector>

namespace visionrt {

// Parses and validates a tool definition from a YAML string.
// Unknown fields, schema violations, or invalid constraints throw ContractError.
ToolDefinition parseToolDefinitionYaml(
    const std::string& yamlContent,
    const std::filesystem::path& definitionPath = {},
    bool builtin = false);

// Loads, parses, and validates a tool definition from a YAML file.
ToolDefinition parseToolDefinitionFile(
    const std::filesystem::path& path,
    bool builtin = false);

// Validates tool YAML content without retaining the definition.
void validateToolYaml(
    const std::string& yamlContent,
    const std::filesystem::path& definitionPath = {},
    bool builtin = false);

// Emits the canonical YAML model for a parsed definition. Relative entry and
// resource references stay relative to the declaration file.
std::string emitNormalizedToolYaml(const ToolDefinition& definition);

struct ModuleEntry {
    std::string library;
};

struct ToolIndexEntry {
    std::string module;       // domain DLL module reference (mutually exclusive with hostBinding)
    std::string definition;   // relative path to tool declaration YAML
    std::string hostBinding;  // host platform receiver binding (mutually exclusive with module)
};

struct ModuleIndexData {
    int schemaVersion = 2;
    std::string variant;
    std::unordered_map<std::string, ModuleEntry> modules;
    std::unordered_map<std::string, ToolIndexEntry> tools; // key is tool_id
    std::vector<std::string> runtimeFiles;
};

using ModuleIndex = ModuleIndexData;

ModuleIndexData parseModuleIndexYaml(const std::string& yamlContent);
ModuleIndexData parseModuleIndexFile(const std::filesystem::path& path);
void validateModuleIndexYaml(const std::string& yamlContent);
void validateModuleIndexFile(const std::filesystem::path& path);

} // namespace visionrt
