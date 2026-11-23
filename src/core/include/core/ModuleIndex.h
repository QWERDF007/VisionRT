#pragma once

#include "core/Export.h"
#include "core/ToolIntegrations.h"

#include <filesystem>
#include <map>
#include <string>
#include <vector>

namespace visionrt {
struct ModuleEntry
{
    std::string library;
};

struct ToolIndexEntry
{
    std::string      module;
    std::string      definition;
    ToolIntegrations integrations;
};

struct ModuleIndex
{
    int                                   schemaVersion = 1;
    std::string                           variant;
    std::map<std::string, ModuleEntry>    modules;
    std::map<std::string, ToolIndexEntry> tools;
    std::vector<std::string>              runtimeFiles;
};

VISIONRT_CORE_API ModuleIndex parseModuleIndexJson(const std::string &content);
VISIONRT_CORE_API ModuleIndex parseModuleIndexFile(const std::filesystem::path &path);
} // namespace visionrt
