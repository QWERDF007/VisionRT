#pragma once

#include "core/DefinitionJson.h"
#include "core/ModuleIndex.h"

#include <filesystem>
#include <string>
#include <vector>

namespace visionrt {

// One tool resolved from a tools package index: the parsed declaration plus the
// implementation location the host needs to create the tool.
struct PackageTool
{
    ToolDefinition        definition;    // parsed and validated declaration
    std::filesystem::path moduleLibrary; // absolute module path; empty for non-C++ implementations
};

// Strong-typed description of a complete tools package, produced by reading the
// package index once and resolving every referenced declaration and module.
struct ToolsPackage
{
    std::filesystem::path    root; // absolute, normalized package root
    ModuleIndex              index;
    std::vector<PackageTool> tools; // ordered by declaration path for deterministic listings
};

ToolsPackage readToolsPackage(const std::filesystem::path &packageRoot);

// Validates a tools package without retaining the description.
void validateToolsPackage(const std::filesystem::path &packageRoot);

} // namespace visionrt
