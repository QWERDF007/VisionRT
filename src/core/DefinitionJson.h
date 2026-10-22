#pragma once

#include "core/ToolDefinition.h"

namespace visionrt {
ToolDefinition parseToolDefinitionJson(const std::string &content, const std::filesystem::path &definitionPath = {},
                                       bool builtin = false);
ToolDefinition parseToolDefinitionFile(const std::filesystem::path &path, bool builtin = false);
std::string    emitNormalizedToolJson(const ToolDefinition &definition);
}
