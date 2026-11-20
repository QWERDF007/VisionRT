#include "core/DefinitionJson.h"

#include "visionrt/contracts/DeclarationJson.h"

#include <QIODevice>
#include <QSaveFile>

#include <fstream>
#include <sstream>
#include <utility>

namespace visionrt {
namespace {

std::string pathName(const std::filesystem::path &path)
{
    if (path.empty())
        return {};
    const auto text = path.generic_u8string();
    return std::string(reinterpret_cast<const char *>(text.data()), text.size());
}

std::string readText(const std::filesystem::path &path)
{
    std::ifstream stream(path, std::ios::binary);
    if (!stream)
        throw ContractError("无法读取 " + pathName(path));
    std::ostringstream buffer;
    buffer << stream.rdbuf();
    if (stream.bad())
        throw ContractError("读取失败 " + pathName(path));
    return std::move(buffer).str();
}

} // namespace

ToolDefinition parseToolDefinitionJson(const std::string &content, const std::filesystem::path &path, ToolSource source)
{
    ToolDefinition definition;
    static_cast<ToolDeclaration &>(definition) = readToolDeclaration(content, pathName(path));
    definition.source = std::move(source);
    if (!path.empty())
        definition.definitionPath = std::filesystem::absolute(path).lexically_normal();
    normalizeToolDefinition(definition);
    validateToolDefinition(definition);
    return definition;
}

ToolDefinition parseToolDefinitionFile(const std::filesystem::path &path, ToolSource source)
{
    if (path.extension() != ".json")
        throw ContractError("工具声明仅支持 JSON：" + pathName(path));
    auto definition = parseToolDefinitionJson(readText(path), path, std::move(source));
    // The declaration's folder inside its root is the tool's category, so the tree groups by the
    // file system and a folder rename regroups a tool without touching its declaration.
    const auto &root = definition.source.roots.declarations;
    if (!root.empty())
    {
        for (const auto &component : path.lexically_relative(root).parent_path())
        {
            if (component.empty() || component == ".")
                continue;
            const auto utf8 = component.u8string();
            definition.categories.emplace_back(reinterpret_cast<const char *>(utf8.data()), utf8.size());
        }
    }
    return definition;
}

std::string emitNormalizedToolJson(const ToolDefinition &source)
{
    ToolDefinition definition = source;
    normalizeToolDefinition(definition);
    validateToolDefinition(definition);
    return writeToolDeclaration(definition);
}

ToolDefinition writeToolDefinitionFile(const std::filesystem::path &path, const ToolDefinition &authored)
{
    if (path.empty())
        throw ContractError("工具声明缺少写入路径");
    ToolDefinition definition = authored;
    normalizeToolDefinition(definition);
    validateToolDefinition(definition);

    const auto content = writeToolDeclaration(definition);
    QSaveFile  file(QString::fromStdWString(path.wstring()));
    file.setDirectWriteFallback(false);
    if (!file.open(QIODevice::WriteOnly))
        throw ContractError("无法写入工具声明：" + pathName(path));
    if (file.write(content.data(), static_cast<qint64>(content.size())) != static_cast<qint64>(content.size()))
    {
        file.cancelWriting();
        throw ContractError("写入工具声明内容失败：" + pathName(path));
    }
    if (!file.commit())
        throw ContractError("提交工具声明失败：" + pathName(path));
    definition.definitionPath = path;
    return definition;
}
}
