#include "core/ModuleIndex.h"

#include "core/JsonFields.h"
#include "core/ValueJson.h"

#include <algorithm>

namespace visionrt {
namespace {
std::string packagePath(const QJsonValue &value, const QString &context)
{
    const auto path = json::relativePath(json::text(value, context), context);
    if (*path.begin() == "..")
        throw ContractError((context + " 超出包根目录").toStdString());
    return json::pathText(path).toStdString();
}
}

ModuleIndex parseModuleIndexJson(const std::string &content)
{
    const ValueJson::Document document(QByteArray::fromStdString(content));
    if (!document.json().isObject())
        throw ContractError("模块索引必须是 JSON 对象");
    const auto root = document.json().object();
    json::fields(root, {"schema_version", "variant", "modules", "tools", "runtime_files"}, "模块索引");
    if (!equalValues(document.decode({"schema_version"}, {Type::Int}), PortValue{std::int64_t{1}}))
        throw ContractError("schema_version 必须是整数 1");
    ModuleIndex result;
    result.variant = json::text(root.value("variant"), "variant");
    for (const auto &value : json::array(root.value("modules"), "modules"))
    {
        const auto item = json::object(value, "modules");
        json::fields(item, {"id", "library"}, "modules");
        const auto id = json::text(item.value("id"), "modules.id");
        if (!result.modules.emplace(id, ModuleEntry{packagePath(item.value("library"), "modules.library")}).second)
            throw ContractError("重复模块 id：" + id);
    }
    for (const auto &value : json::array(root.value("tools"), "tools"))
    {
        const auto item = json::object(value, "tools");
        json::fields(item, {"tool_id", "definition", "module"}, "tools");
        const auto     id = json::text(item.value("tool_id"), "tools.tool_id");
        ToolIndexEntry entry;
        entry.definition = packagePath(item.value("definition"), "tools.definition");
        if (item.contains("module"))
        {
            entry.module = json::text(item.value("module"), "tools.module");
            if (!result.modules.contains(entry.module))
                throw ContractError("工具引用未知模块：" + id);
        }
        if (!result.tools.emplace(id, std::move(entry)).second)
            throw ContractError("重复 tool_id：" + id);
    }
    for (const auto &[id, module] : result.modules)
        if (std::ranges::none_of(result.tools, [&](const auto &tool) { return tool.second.module == id; }))
            throw ContractError("模块没有对应工具：" + id);
    for (const auto &value : json::array(root.value("runtime_files"), "runtime_files"))
        result.runtimeFiles.push_back(packagePath(value, "runtime_files"));
    return result;
}

ModuleIndex parseModuleIndexFile(const std::filesystem::path &path)
{
    if (path.extension() != ".json")
        throw ContractError("模块索引仅支持 JSON");
    try
    {
        return parseModuleIndexJson(json::read(path).toStdString());
    }
    catch (const ContractError &error)
    {
        throw ContractError(json::pathText(path).toStdString() + ": " + error.what());
    }
}
}
