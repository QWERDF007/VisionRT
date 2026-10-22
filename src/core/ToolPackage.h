#pragma once

#include "core/ToolDeclarationYaml.h"

#include <filesystem>
#include <string>
#include <vector>

namespace visionrt {

// One tool resolved from a tools package index: the parsed declaration plus the
// implementation location the host needs to create the tool.
struct PackageTool {
    std::string toolId;                   // stable identity from the declaration
    ToolDefinition definition;            // parsed and validated declaration
    std::string registryName;             // C++ factory locator reported by the declaration
    std::filesystem::path moduleLibrary;  // absolute module path; empty for non-C++ implementations
    std::string hostBinding;              // host platform receiver binding; empty for domain/python tools
};

// Strong-typed description of a complete tools package, produced by reading the
// package index once and resolving every referenced declaration and module.
struct ToolsPackage {
    std::filesystem::path root;  // absolute, normalized package root
    ModuleIndexData index;       // parsed final index
    std::vector<PackageTool> tools;  // ordered by declaration path for deterministic listings
};

// Reads and fully validates an installable tools package directory:
// index presence and schema, declaration existence and identity, module libraries and
// runtime files, duplicate tool identity. Throws ContractError on the first violation.
ToolsPackage readToolsPackage(const std::filesystem::path& packageRoot);

// Validates a tools package without retaining the description.
void validateToolsPackage(const std::filesystem::path& packageRoot);

} // namespace visionrt
