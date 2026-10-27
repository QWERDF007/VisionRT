#include "core/ToolPackage.h"

#include "core/JsonFields.h"
#include "core/ToolPaths.h"

#include <algorithm>
#include <array>

namespace visionrt {
namespace {
void requireFile(const std::filesystem::path &path)
{
    if (!std::filesystem::is_regular_file(path))
        throw ContractError("工具包缺少文件：" + json::pathText(path).toStdString());
}

std::filesystem::path resourcePath(const ToolDefinition &definition, const std::filesystem::path &resource)
{
    if (definition.kind == ToolKind::PythonFunction || definition.kind == ToolKind::PythonClass)
        return resolveToolPath(definition.pythonPackage(), resource);
    const auto &root = definition.source.roots.declarations;
    return resolveToolPath(root, (definition.definitionPath.parent_path() / resource).lexically_relative(root));
}
}

ToolsPackage readToolsPackage(const std::filesystem::path &packageRoot, std::optional<ToolRoots> customRoots)
{
    if (!std::filesystem::is_directory(packageRoot))
        throw ContractError("工具包目录不存在：" + json::pathText(packageRoot).toStdString());
    ToolsPackage package;
    package.root  = std::filesystem::canonical(packageRoot);
    package.index = parseModuleIndexFile(package.root / "module_index.json");
    for (const auto &[id, module] : package.index.modules)
        (void)resolveToolPath(package.root, json::relativePath(module.library, "modules.library"));
    for (const auto &file : package.index.runtimeFiles)
        (void)resolveToolPath(package.root, json::relativePath(file, "runtime_files"));
    if (customRoots && (!customRoots->declarations.is_absolute() || !customRoots->python.is_absolute()))
        throw ContractError("自定义声明根与 Python 根必须同时配置为绝对路径");
    const std::array<ToolSource, 2> sources{
        {{ToolOrigin::Builtin,
          {resolveToolPath(package.root, "tools/builtin"), resolveToolPath(package.root, "python/builtin")}},
         {ToolOrigin::Custom, customRoots ? *customRoots
                                          : ToolRoots{resolveToolPath(package.root, "tools/custom"),
                                                      resolveToolPath(package.root, "python/custom")}}}
    };
    std::map<std::string, std::filesystem::path, std::less<>> identities;
    for (auto source : sources)
    {
        source.roots.declarations = std::filesystem::weakly_canonical(source.roots.declarations);
        source.roots.python       = std::filesystem::weakly_canonical(source.roots.python);
        if (!std::filesystem::exists(source.roots.declarations))
            continue;
        if (!std::filesystem::is_directory(source.roots.declarations))
            throw ContractError("声明根不是目录：" + json::pathText(source.roots.declarations).toStdString());
        for (const auto &file : std::filesystem::recursive_directory_iterator(source.roots.declarations))
        {
            if (!file.is_regular_file() || file.path().extension() != ".json")
                continue;
            (void)resolveToolPath(source.roots.declarations, file.path().lexically_relative(source.roots.declarations));
            PackageTool tool;
            tool.definition                 = parseToolDefinitionFile(file.path(), source);
            const auto [previous, inserted] = identities.emplace(tool.definition.id, tool.definition.definitionPath);
            if (!inserted)
                throw ContractError("tool_id 重复：" + tool.definition.id + "："
                                    + json::pathText(previous->second).toStdString() + " / "
                                    + json::pathText(tool.definition.definitionPath).toStdString());
            const auto indexed = package.index.tools.find(tool.definition.id);
            if (source.origin == ToolOrigin::Builtin && indexed == package.index.tools.end())
                throw ContractError("内建声明缺少发行索引：" + tool.definition.id);
            if (tool.definition.kind == ToolKind::Cpp)
            {
                if (indexed == package.index.tools.end() || indexed->second.module.empty())
                    throw ContractError("C++ 工具必须引用索引 module：" + tool.definition.id);
                tool.moduleLibrary = resolveToolPath(
                    package.root,
                    json::relativePath(package.index.modules.at(indexed->second.module).library, "modules.library"));
            }
            else if (indexed != package.index.tools.end() && !indexed->second.module.empty())
                throw ContractError("Python/host 工具不能引用 module：" + tool.definition.id);
            if (tool.definition.kind == ToolKind::PythonFunction || tool.definition.kind == ToolKind::PythonClass)
                (void)tool.definition.pythonScript();
            for (const auto &resource : tool.definition.resources)
                (void)resourcePath(tool.definition, resource);
            package.tools.push_back(std::move(tool));
        }
    }
    const auto defaultCustomRoot = resolveToolPath(package.root, "tools/custom");
    const bool replacedCustomRoot
        = customRoots && std::filesystem::weakly_canonical(customRoots->declarations) != defaultCustomRoot;
    for (const auto &[id, entry] : package.index.tools)
    {
        const auto expected = resolveToolPath(package.root, json::relativePath(entry.definition, "tools.definition"));
        if (replacedCustomRoot && entry.module.empty())
        {
            const auto relative = expected.lexically_relative(defaultCustomRoot);
            if (!relative.empty() && *relative.begin() != ".." && !relative.is_absolute())
                continue;
        }
        const auto actual = identities.find(id);
        if (actual == identities.end() || std::filesystem::weakly_canonical(actual->second) != expected)
            throw ContractError("发行索引与扫描声明不一致：" + id + " / " + json::pathText(expected).toStdString());
    }
    std::sort(package.tools.begin(), package.tools.end(), [](const auto &left, const auto &right)
              { return left.definition.definitionPath < right.definition.definitionPath; });
    return package;
}

void validateToolsPackage(const std::filesystem::path &root)
{
    const auto package = readToolsPackage(root);
    for (const auto &[id, module] : package.index.modules)
        requireFile(resolveToolPath(package.root, json::relativePath(module.library, "modules.library")));
    for (const auto &file : package.index.runtimeFiles)
        requireFile(resolveToolPath(package.root, json::relativePath(file, "runtime_files")));
    for (const auto &tool : package.tools)
    {
        if (tool.definition.kind == ToolKind::PythonFunction || tool.definition.kind == ToolKind::PythonClass)
            requireFile(tool.definition.pythonScript());
        for (const auto &resource : tool.definition.resources)
        {
            const auto path = resourcePath(tool.definition, resource);
            if (!std::filesystem::exists(path))
                throw ContractError("工具包缺少资源：" + json::pathText(path).toStdString());
        }
    }
}
}
