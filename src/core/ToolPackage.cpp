#include "core/ToolPackage.h"

#include <algorithm>
#include <set>
#include <system_error>

namespace visionrt {

namespace {

std::string pathText(const std::filesystem::path& path) {
    const auto text = path.generic_u8string();
    return {reinterpret_cast<const char*>(text.data()), text.size()};
}

std::filesystem::path utf8Path(std::string_view text) {
    const auto view = std::u8string_view(reinterpret_cast<const char8_t*>(text.data()), text.size());
    return std::filesystem::path(view);
}

bool cppKind(ToolKind kind) {
    return kind == ToolKind::CppFunction || kind == ToolKind::CppClass;
}

bool underRoot(const std::filesystem::path& root, const std::filesystem::path& path) {
    auto rootPart = root.begin();
    auto pathPart = path.begin();
    for (; rootPart != root.end(); ++rootPart, ++pathPart) {
        if (pathPart == path.end() || *pathPart != *rootPart) return false;
    }
    return true;
}

// Every package-relative reference resolves inside the installed package; a normalized
// escape (for example "a/../../outside.yaml") is a contract violation, not a lookup hint.
std::filesystem::path resolveInside(const std::filesystem::path& root, const std::string& relative,
                                    const std::string& context) {
    const auto resolved = (root / utf8Path(relative)).lexically_normal();
    if (!underRoot(root, resolved))
        throw ContractError(context + " resolves outside the package root: " + relative);
    return resolved;
}

} // namespace

ToolsPackage readToolsPackage(const std::filesystem::path& packageRoot) {
    std::error_code error;
    if (!std::filesystem::is_directory(packageRoot, error) || error) {
        throw ContractError("Package root is not a valid directory: " + pathText(packageRoot));
    }

    ToolsPackage package;
    package.root = std::filesystem::absolute(packageRoot).lexically_normal();

    const auto indexFile = package.root / "module_index.yaml";
    if (!std::filesystem::is_regular_file(indexFile, error) || error) {
        throw ContractError("Missing module_index.yaml in package: " + pathText(package.root));
    }
    package.index = parseModuleIndexFile(indexFile);

    for (const auto& [moduleName, moduleEntry] : package.index.modules) {
        const auto library = resolveInside(package.root, moduleEntry.library,
                                           "Module '" + moduleName + "' library");
        if (!std::filesystem::is_regular_file(library, error) || error) {
            throw ContractError("Module '" + moduleName + "' library file does not exist: " + pathText(library));
        }
    }

    std::set<std::string> toolIds;
    package.tools.reserve(package.index.tools.size());
    for (const auto& [toolId, toolEntry] : package.index.tools) {
        PackageTool tool;
        tool.toolId = toolId;
        const auto definitionPath = resolveInside(package.root, toolEntry.definition, "Tool '" + toolId + "' definition");
        if (!std::filesystem::is_regular_file(definitionPath, error) || error) {
            throw ContractError("Tool '" + toolId + "' definition file does not exist: " + pathText(definitionPath));
        }
        tool.definition = parseToolDefinitionFile(definitionPath, true);
        if (tool.definition.id != toolId) {
            throw ContractError("Tool definition id '" + tool.definition.id +
                                "' does not match index tool_id '" + toolId + "'");
        }
        tool.registryName = tool.definition.registryName;
        tool.hostBinding = toolEntry.hostBinding;
        if (tool.hostBinding.empty() && std::any_of(tool.definition.outputs.begin(), tool.definition.outputs.end(),
                [](const auto& port) { return port.typeFrom.has_value(); })) {
            throw ContractError("Source-derived output types require host_binding: " + toolId);
        }
        if (!toolIds.insert(tool.toolId).second) {
            throw ContractError("Duplicate tool_id in package: '" + tool.toolId + "'");
        }

        if (!tool.hostBinding.empty()) {
            if (!toolEntry.module.empty()) {
                throw ContractError("Tool '" + toolId + "' cannot specify both module and host_binding");
            }
            if (tool.definition.kind == ToolKind::PythonFunction || tool.definition.kind == ToolKind::PythonClass) {
                throw ContractError("Python tool '" + toolId + "' cannot specify host_binding");
            }
        } else if (cppKind(tool.definition.kind)) {
            if (toolEntry.module.empty()) {
                throw ContractError("C++ domain tool '" + toolId + "' must reference a module");
            }
            tool.moduleLibrary = (package.root /
                utf8Path(package.index.modules.at(toolEntry.module).library)).lexically_normal();
        } else if (tool.definition.kind == ToolKind::PythonFunction || tool.definition.kind == ToolKind::PythonClass) {
            if (!toolEntry.module.empty()) {
                throw ContractError("Python tool '" + toolId + "' cannot reference a module");
            }
            if (tool.definition.script.empty()) {
                throw ContractError("Python tool '" + toolId + "' declaration missing script entry");
            }
            const auto scriptPath = (definitionPath.parent_path() / tool.definition.script).lexically_normal();
            if (!underRoot(package.root, scriptPath))
                throw ContractError("Python tool '" + toolId + "' script resolves outside the package root: " +
                                    pathText(scriptPath));
            if (!std::filesystem::is_regular_file(scriptPath, error) || error) {
                throw ContractError("Python tool '" + toolId + "' script file does not exist: " + pathText(scriptPath));
            }
        }

        for (const auto& resource : tool.definition.resources) {
            if (!underRoot(package.root, resource))
                throw ContractError("Tool '" + toolId + "' resource resolves outside the package root");
            if (!std::filesystem::exists(resource, error) || error)
                throw ContractError("Tool '" + toolId + "' resource file does not exist: " + pathText(resource));
        }
        package.tools.push_back(std::move(tool));
    }

    std::sort(package.tools.begin(), package.tools.end(), [](const PackageTool& a, const PackageTool& b) {
        return a.definition.definitionPath.generic_u8string() < b.definition.definitionPath.generic_u8string();
    });

    for (const auto& runtimeFile : package.index.runtimeFiles) {
        const auto resolved = resolveInside(package.root, runtimeFile, "runtime_files entry");
        if (!std::filesystem::is_regular_file(resolved, error) || error) {
            throw ContractError("runtime_files entry does not exist: " + pathText(resolved));
        }
    }

    return package;
}

void validateToolsPackage(const std::filesystem::path& packageRoot) {
    readToolsPackage(packageRoot);
}

} // namespace visionrt
