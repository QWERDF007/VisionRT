#include "core/ToolPackage.h"

#include "core/JsonFields.h"

#include <algorithm>

namespace visionrt {
namespace {
std::filesystem::path inside(const std::filesystem::path &root, const std::filesystem::path &path)
{
    const auto resolved = std::filesystem::weakly_canonical(path);
    auto       part     = resolved.begin();
    for (const auto &rootPart : root)
    {
        if (part == resolved.end() || *part != rootPart)
            throw ContractError("路径超出工具包根目录：" + json::pathText(path).toStdString());
        ++part;
    }
    return resolved;
}

void requireFile(const std::filesystem::path &path)
{
    if (!std::filesystem::is_regular_file(path))
        throw ContractError("工具包缺少文件：" + json::pathText(path).toStdString());
}
}

ToolsPackage readToolsPackage(const std::filesystem::path &packageRoot)
{
    if (!std::filesystem::is_directory(packageRoot))
        throw ContractError("工具包目录不存在：" + json::pathText(packageRoot).toStdString());
    ToolsPackage package;
    package.root  = std::filesystem::canonical(packageRoot);
    package.index = parseModuleIndexFile(package.root / "module_index.json");
    for (const auto &[id, module] : package.index.modules)
        inside(package.root, package.root / json::relativePath(module.library, "modules.library"));
    for (const auto &file : package.index.runtimeFiles)
        inside(package.root, package.root / json::relativePath(file, "runtime_files"));
    for (const auto &[id, entry] : package.index.tools)
    {
        PackageTool tool;
        const auto file = inside(package.root, package.root / json::relativePath(entry.definition, "tools.definition"));
        tool.definition = parseToolDefinitionFile(file, true);
        if (tool.definition.id != id)
            throw ContractError("索引 tool_id 与声明不一致：" + id + " / " + tool.definition.id);
        if (tool.definition.kind == ToolKind::Cpp)
        {
            if (entry.module.empty())
                throw ContractError("C++ 工具必须引用 module：" + id);
            tool.moduleLibrary = inside(
                package.root,
                package.root / json::relativePath(package.index.modules.at(entry.module).library, "modules.library"));
        }
        else if (!entry.module.empty())
            throw ContractError("Python/host 工具不能引用 module：" + id);
        if (!tool.definition.script.empty())
            tool.definition.script = inside(package.root, tool.definition.script);
        for (auto &resource : tool.definition.resources) resource = inside(package.root, resource);
        package.tools.push_back(std::move(tool));
    }
    std::sort(package.tools.begin(), package.tools.end(), [](const auto &left, const auto &right)
              { return left.definition.definitionPath < right.definition.definitionPath; });
    return package;
}

void validateToolsPackage(const std::filesystem::path &root)
{
    const auto package = readToolsPackage(root);
    for (const auto &[id, module] : package.index.modules)
        requireFile(package.root / json::relativePath(module.library, "modules.library"));
    for (const auto &file : package.index.runtimeFiles)
        requireFile(package.root / json::relativePath(file, "runtime_files"));
    for (const auto &tool : package.tools)
    {
        if (!tool.definition.script.empty())
            requireFile(tool.definition.script);
        for (const auto &resource : tool.definition.resources)
            if (!std::filesystem::exists(resource))
                throw ContractError("工具包缺少资源：" + json::pathText(resource).toStdString());
    }
}
}
