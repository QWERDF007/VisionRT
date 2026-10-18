#pragma once

#include "visionrt/sdk/Schema.h"
#include <filesystem>
#include <string>
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

struct ModuleIndexItem {
    std::string registryName;
    std::string path;
};

using ModuleIndex = std::vector<ModuleIndexItem>;

VT_EXPORT ModuleIndex parseModuleIndexYaml(const std::string& yamlContent);
VT_EXPORT ModuleIndex parseModuleIndexFile(const std::filesystem::path& path);
VT_EXPORT void validateModuleIndexYaml(const std::string& yamlContent);
VT_EXPORT void validateModuleIndexFile(const std::filesystem::path& path);

} // namespace visionrt