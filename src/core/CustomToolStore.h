#pragma once

#include "core/DefinitionJson.h"
#include "core/ToolDefinition.h"
#include "core/ToolPackage.h"

#include <filesystem>
#include <optional>
#include <string>
#include <vector>

namespace visionrt {

class CustomToolStore
{
public:
    explicit CustomToolStore(std::filesystem::path rootDir = {});

    const std::filesystem::path &rootDir() const noexcept
    {
        return rootDir_;
    }
    void setRootDir(std::filesystem::path rootDir);

    std::filesystem::path declarationsDir() const
    {
        return rootDir_ / "tools";
    }
    std::filesystem::path pythonDir() const
    {
        return rootDir_ / "python";
    }

    // Default user root directory
    static std::filesystem::path defaultRootDir();

    // The starting script of a new Python tool. It reads the upstream status the way a Fixed tool
    // receives it, returns every declared business output as Missing under a "not implemented"
    // status, and therefore runs honestly before the author writes the algorithm.
    static std::string pythonTemplate(const ToolDefinition &definition);

    // Scans all valid custom tools in the store
    // Every readable declaration is returned. A declaration that cannot be parsed is named in
    // `problems` instead of disappearing; passing nullptr makes the first such file an error.
    std::vector<PackageTool> discoverCustomTools(std::vector<std::string> *problems = nullptr) const;

    // One tool of this store by its tool id; nullopt when the declaration does not exist or cannot
    // be parsed. Used to pick a free clone id without rescanning the whole root at the call site.
    std::optional<PackageTool> findCustomTool(std::string_view toolId) const;

    // Checks whether a tool definition belongs to this store
    bool isUserTool(const ToolDefinition &def) const;

    // Atomically saves a tool definition and its python script (JSON-last) and returns the stored
    // declaration, including its path. The argument is the authored draft: normalisation happens on
    // a copy, so a caller's port list never gains host ports behind its back.
    ToolDefinition saveCustomTool(const ToolDefinition &definition, const std::string &scriptContent = {});

    // Deletes a custom tool declaration and cleans up script if unshared. Returns false when the
    // declaration is not removable, so a caller never reports a deletion that did not happen.
    bool deleteCustomTool(const std::string &toolId);

    // Clones a custom tool, preserving port IDs and next_port_id counters
    ToolDefinition cloneCustomTool(const ToolDefinition &source, const std::string &newToolId, const std::string &newNameZh);

private:
    std::filesystem::path rootDir_;
};

} // namespace visionrt
