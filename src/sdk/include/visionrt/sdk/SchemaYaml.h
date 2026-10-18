#pragma once

#include "visionrt/sdk/Schema.h"
#include <filesystem>
#include <string>
#include <unordered_map>
#include <vector>

namespace visionrt {

// Parses and validates a tool definition from a YAML string.
// Unknown fields, schema violations, or invalid constraints throw ContractError.
VT_EXPORT ToolDefinition parseToolDefinitionYaml(
    const std::string& yamlContent,
    const std::filesystem::path& definitionPath = {},
    bool builtin = false);

// Loads, parses, and validates a tool definition from a YAML file.
VT_EXPORT ToolDefinition parseToolDefinitionFile(
    const std::filesystem::path& path,
    bool builtin = false);

// Validates tool YAML content without retaining the definition.
VT_EXPORT void validateToolYaml(
    const std::string& yamlContent,
    const std::filesystem::path& definitionPath = {},
    bool builtin = false);

struct ModuleEntry {
    std::string library;
};

struct ToolIndexEntry {
    std::string module;
    std::string definition;
};

struct ModuleIndexData {
    std::string variant;
    std::unordered_map<std::string, ModuleEntry> modules;
    std::unordered_map<std::string, ToolIndexEntry> tools;
    std::vector<std::string> runtimeFiles;
};

using ModuleIndex = ModuleIndexData;

VT_EXPORT ModuleIndexData parseModuleIndexYaml(const std::string& yamlContent);
VT_EXPORT ModuleIndexData parseModuleIndexFile(const std::filesystem::path& path);
VT_EXPORT void validateModuleIndexYaml(const std::string& yamlContent);
VT_EXPORT void validateModuleIndexFile(const std::filesystem::path& path);

} // namespace visionrt