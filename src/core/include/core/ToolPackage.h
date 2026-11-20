#pragma once

#include "core/Export.h"

#include "core/DefinitionJson.h"
#include "core/ModuleIndex.h"

#include <filesystem>
#include <optional>
#include <string>
#include <vector>

namespace visionrt {

struct PackageTool
{
    ToolDefinition        definition;    // parsed and validated declaration
    std::filesystem::path moduleLibrary; // absolute module path; empty for non-C++ implementations
};

struct ToolsPackage
{
    std::filesystem::path    root; // absolute, normalized package root
    ModuleIndex              index;
    std::vector<PackageTool> tools; // ordered by declaration path for deterministic listings
};

VISIONRT_CORE_API ToolsPackage readToolsPackage(const std::filesystem::path &packageRoot, std::optional<ToolRoots> customRoots = {});

// Validates a tools package without retaining the description.
VISIONRT_CORE_API void validateToolsPackage(const std::filesystem::path &packageRoot);

} // namespace visionrt
