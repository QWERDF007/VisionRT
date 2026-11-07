#pragma once

#include "core/Tool.h"

#include <filesystem>
#include <string>
#include <vector>

namespace visionrt
{

struct PythonImplementation
{
    std::string toolId;
    ToolStatus  status;
};

// Facts reported by the selected interpreter itself. The host never guesses a prefix or a search
// path: the probe process is the single source, and the bridge starts CPython from these values
// instead of re-deriving them from an environment variable or a compile-time default.
struct PythonEnvironment
{
    std::filesystem::path              executable;
    std::filesystem::path              prefix;
    std::filesystem::path              basePrefix;
    // Absolute path of the CPython DLL that the running interpreter itself loaded.
    std::filesystem::path              dll;
    std::vector<std::filesystem::path> searchPaths;
    std::string                        numpyVersion;
};

} // namespace visionrt
