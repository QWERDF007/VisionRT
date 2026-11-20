#pragma once

#include "core/Export.h"

#include "core/ToolDefinition.h"

namespace visionrt {
VISIONRT_CORE_API ToolDefinition parseToolDefinitionJson(const std::string &content, const std::filesystem::path &definitionPath = {},
                                       ToolSource source = {});
VISIONRT_CORE_API ToolDefinition parseToolDefinitionFile(const std::filesystem::path &path, ToolSource source = {});
VISIONRT_CORE_API std::string    emitNormalizedToolJson(const ToolDefinition &definition);
// Writes one declaration at the path it was loaded from, replacing the file only after the whole
// document is written. The returned copy is the normalised contract that was written; a declaration
// that cannot be normalised or validated is not written at all.
VISIONRT_CORE_API ToolDefinition writeToolDefinitionFile(const std::filesystem::path &path, const ToolDefinition &definition);
}
