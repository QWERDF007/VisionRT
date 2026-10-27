#pragma once

#include "core/ToolDefinition.h"

namespace visionrt {
ToolDefinition parseToolDefinitionJson(const std::string &content, const std::filesystem::path &definitionPath = {},
                                       ToolSource source = {});
ToolDefinition parseToolDefinitionFile(const std::filesystem::path &path, ToolSource source = {});
std::string    emitNormalizedToolJson(const ToolDefinition &definition);
}
