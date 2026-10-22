#pragma once

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
    std::string module;
    std::string definition;
};

struct ModuleIndex
{
    int                                   schemaVersion = 1;
    std::string                           variant;
    std::map<std::string, ModuleEntry>    modules;
    std::map<std::string, ToolIndexEntry> tools;
    std::vector<std::string>              runtimeFiles;
};

ModuleIndex parseModuleIndexJson(const std::string &content);
ModuleIndex parseModuleIndexFile(const std::filesystem::path &path);
}
