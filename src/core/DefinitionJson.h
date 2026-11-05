#pragma once

#include "core/ToolDefinition.h"

namespace visionrt {
ToolDefinition parseToolDefinitionJson(const std::string &content, const std::filesystem::path &definitionPath = {},
                                       ToolSource source = {});
ToolDefinition parseToolDefinitionFile(const std::filesystem::path &path, ToolSource source = {});
std::string    emitNormalizedToolJson(const ToolDefinition &definition);
// Writes one declaration at the path it was loaded from, replacing the file only after the whole
// document is written. The returned copy is the normalised contract that was written; a declaration
// that cannot be normalised or validated is not written at all.
ToolDefinition writeToolDefinitionFile(const std::filesystem::path &path, const ToolDefinition &definition);
}
