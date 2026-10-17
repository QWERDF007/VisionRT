#pragma once

#include "visionrt/sdk/Schema.h"
#include <filesystem>
#include <string>

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

} // namespace visionrt
