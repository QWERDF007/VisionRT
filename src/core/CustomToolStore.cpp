#include "core/CustomToolStore.h"

#include "core/DefinitionJson.h"
#include "core/JsonFields.h"
#include "core/ToolPaths.h"

#include <QDir>
#include <QFile>
#include <QJsonDocument>
#include <QSaveFile>
#include <algorithm>
#include <fstream>
#include <stdexcept>

namespace visionrt {

CustomToolStore::CustomToolStore(std::filesystem::path rootDir)
    : rootDir_(rootDir.empty() ? defaultRootDir() : std::filesystem::weakly_canonical(rootDir))
{
}

std::filesystem::path CustomToolStore::defaultRootDir()
{
    // The installation owns its user tools: they sit next to the executable, never in a per-user
    // profile or on the system drive. Deployment replaces program files and preserves this folder.
    return softwareDataDir() / "custom-tools";
}

namespace {

// Both the script and the declaration go through the same writer: a half-written file is never
// visible, whichever of the two a failure interrupts.
void writeAtomically(const std::filesystem::path &path, std::string_view content, std::string_view what)
{
    QSaveFile file(QString::fromStdWString(path.wstring()));
    file.setDirectWriteFallback(false);
    if (!file.open(QIODevice::WriteOnly))
        throw ContractError("无法写入" + std::string(what) + "：" + path.string());
    if (file.write(content.data(), static_cast<qint64>(content.size())) != static_cast<qint64>(content.size()))
    {
        file.cancelWriting();
        throw ContractError("写入" + std::string(what) + "内容失败：" + path.string());
    }
    if (!file.commit())
        throw ContractError("提交" + std::string(what) + "失败：" + path.string());
}

void writeIntegrations(const std::filesystem::path &path, const std::map<std::string, ToolIntegrations> &registrations)
{
    if (registrations.empty())
    {
        std::filesystem::remove(path);
        return;
    }
    QJsonObject entries;
    for (const auto &[id, value] : registrations)
        entries.insert(QString::fromStdString(id), writeToolIntegrations(value));
    writeAtomically(path,
                    QJsonDocument(QJsonObject{
                                      {"schema_version",       1},
                                      {         "tools", entries}
    })
                        .toJson()
                        .toStdString(),
                    "交互登记");
}

// The folder a declaration lives in is its category, so a tool's place in the tree is a directory
// name the store can always reach: no empty part, no separator and no parent reference.
std::filesystem::path declarationFolder(const std::filesystem::path        &root,
                                        const std::vector<std::string> &categories)
{
    auto folder = root;
    for (const auto &name : categories)
    {
        if (name.empty() || name == "." || name == ".." || name.find_first_of("/\\") != std::string::npos)
            throw ContractError("工具分类目录不合法：" + name);
        // Categories are UTF-8 like every other author string; a narrow path would be read as ANSI.
        folder /= std::filesystem::path(
            std::u8string(reinterpret_cast<const char8_t *>(name.data()), name.size()));
    }
    return folder;
}

} // namespace

std::map<std::string, ToolIntegrations> CustomToolStore::integrations() const
{
    const auto path = rootDir_ / "integrations.json";
    if (!std::filesystem::exists(path))
        return {};
    const auto document = QJsonDocument::fromJson(json::read(path));
    if (!document.isObject())
        throw ContractError("自定义交互登记必须是 JSON 对象");
    const auto root = document.object();
    json::fields(root, {"schema_version", "tools"}, "自定义交互登记");
    if (root.value("schema_version").toInteger() != 1)
        throw ContractError("自定义交互登记版本必须为 1");
    std::map<std::string, ToolIntegrations> result;
    const auto                              entries = json::object(root.value("tools"), "自定义交互登记.tools");
    for (auto it = entries.begin(); it != entries.end(); ++it)
    {
        const auto id = it.key().toStdString();
        validateIdentifier(id, false);
        const auto entry = json::object(it.value(), it.key());
        json::fields(entry, {"processing_locator", "training_locator"}, it.key());
        result.emplace(id, readToolIntegrations(entry));
    }
    return result;
}

std::string CustomToolStore::pythonTemplate(const ToolDefinition &definition)
{
    const bool        pythonClass = definition.kind == ToolKind::PythonClass;
    const std::string name        = definition.nameZh.empty() ? "自定义工具" : definition.nameZh;

    // Declared business outputs are listed by number so the author sees the keys the runtime will
    // read; returning them as None keeps the result honest while the algorithm is missing.
    std::string outputs = "{0: 1, 1: \"工具尚未实现\"";
    for (const auto &port : definition.outputs)
        if (!isReservedPort(PortStage::Outputs, port.id))
            outputs += ", " + std::to_string(port.id) + ": None";
    outputs += "}";

    const std::string indent = pythonClass ? "        " : "    ";
    const std::string body   = indent + "upstream_status = inputs.get(0, 0)\n"
                             + indent + "if upstream_status != 0:\n"
                             + indent + "    return {\n"
                             + indent + "        \"outputs\": {0: upstream_status, 1: \"上层工具未正常完成\"},\n"
                             + indent + "        \"detail\": \"\",\n"
                             + indent + "    }\n"
                             + indent + "# 算法尚未实现：业务输出保持缺失，由状态码说明结果。\n"
                             + indent + "return {\n"
                             + indent + "    \"outputs\": " + outputs + ",\n"
                             + indent + "    \"detail\": \"\",\n"
                             + indent + "}\n";

    const std::string header = "# -*- coding: utf-8 -*-\n# " + name
                             + (pythonClass ? "（Python 类）\n" : "（Python 函数）\n")
                             + "# R45 数字参数协议：inputs[0] 为上层状态；outputs[0]=状态码，"
                               "outputs[1]=状态信息，业务输出从 2 起。\n"
                               "# 业务输入按端口编号读取（例如 inputs[1]），不要按参数名取参。\n\n";

    if (!pythonClass)
        return header + "def execute(inputs):\n" + body;

    return header + "class Tool:\n"
                    "    def __init__(self):\n"
                    "        pass\n\n"
                    "    def initialize(self, params):\n"
                    "        return {\"code\": 0, \"message\": \"\", \"detail\": \"\"}\n\n"
                    "    def execute(self, inputs):\n"
           + body + "\n"
                    "    def release(self):\n"
                    "        return {\"code\": 0, \"message\": \"\", \"detail\": \"\"}\n";
}

std::vector<PackageTool> CustomToolStore::discoverCustomTools(std::vector<std::string> *problems) const
{
    std::vector<PackageTool> result;
    const auto declDir = declarationsDir();
    const auto pyDir   = pythonDir();
    const auto               registrations = integrations();

    if (!std::filesystem::exists(declDir) || !std::filesystem::is_directory(declDir))
        return result;

    const ToolSource source{
        .origin = ToolOrigin::Custom,
        .roots = ToolRoots{
            .declarations = std::filesystem::weakly_canonical(declDir),
            .python       = std::filesystem::weakly_canonical(pyDir)
        }
    };

    for (const auto &entry : std::filesystem::recursive_directory_iterator(declDir))
    {
        if (!entry.is_regular_file() || entry.path().extension() != ".json")
            continue;
        try
        {
            PackageTool tool;
            tool.definition = parseToolDefinitionFile(entry.path(), source);
            if (const auto found = registrations.find(tool.definition.nameEn); found != registrations.end())
                tool.definition.integrations = found->second;
            result.push_back(std::move(tool));
        }
        catch (const std::exception &error)
        {
            const auto message = entry.path().string() + "：" + error.what();
            if (!problems)
                throw ContractError("自定义工具声明无法读取：" + message);
            problems->push_back(message);
        }
    }

    std::sort(result.begin(), result.end(), [](const PackageTool &a, const PackageTool &b) {
        return a.definition.nameEn < b.definition.nameEn;
    });

    return result;
}

std::optional<PackageTool> CustomToolStore::findCustomTool(std::string_view toolId) const
{
    for (auto &tool : discoverCustomTools())
        if (tool.definition.nameEn == toolId)
            return std::move(tool);
    return std::nullopt;
}

bool CustomToolStore::isUserTool(const ToolDefinition &def) const
{
    if (def.definitionPath.empty())
        return false;
    const auto canonicalDecl = std::filesystem::weakly_canonical(declarationsDir());
    const auto canonicalPath = std::filesystem::weakly_canonical(def.definitionPath);
    const auto relative      = canonicalPath.lexically_relative(canonicalDecl);
    return !relative.empty() && *relative.begin() != ".." && !relative.is_absolute();
}

ToolDefinition CustomToolStore::saveCustomTool(const ToolDefinition &authored, const std::string &scriptContent)
{
    if (authored.nameEn.empty())
        throw ContractError("工具标识 name_en 不能为空");
    if (authored.nameZh.empty())
        throw ContractError("工具名称 name_zh 不能为空");
    // The id names the declaration file, so it must never be able to leave the store root.
    validateIdentifier(authored.nameEn, false);

    ToolDefinition definition = authored;
    const auto declDir = declarationsDir();
    const auto pyDir   = pythonDir();

    std::filesystem::create_directories(declDir);
    std::filesystem::create_directories(pyDir);

    definition.source.origin = ToolOrigin::Custom;
    definition.source.roots.declarations = std::filesystem::weakly_canonical(declDir);
    definition.source.roots.python       = std::filesystem::weakly_canonical(pyDir);

    const bool pythonTool = definition.kind == ToolKind::PythonFunction || definition.kind == ToolKind::PythonClass;
    if (pythonTool)
    {
        if (definition.entry.package.empty())
        {
            std::string pkg = definition.nameEn;
            std::replace(pkg.begin(), pkg.end(), '.', '/');
            definition.entry.package = pkg;
        }
        if (definition.entry.file.empty())
            definition.entry.file = "tool.py";
        if (definition.entry.symbol.empty())
            definition.entry.symbol = (definition.kind == ToolKind::PythonFunction) ? "execute" : "Tool";
    }

    // The declaration is settled before any directory is created: an unusable package path must fail
    // here, not after a script has already been written outside the store.
    normalizeToolDefinition(definition);
    validateToolDefinition(definition);

    // JSON-last: the script and its resources exist before the declaration that makes them visible.
    if (pythonTool)
    {
        const auto scriptDir  = pyDir / definition.entry.package;
        const auto scriptPath = scriptDir / definition.entry.file;
        std::filesystem::create_directories(scriptDir);

        if (!std::filesystem::exists(scriptPath))
            writeAtomically(scriptPath,
                            scriptContent.empty() ? pythonTemplate(definition) : scriptContent,
                            "Python 脚本");
    }

    const auto jsonDir  = declarationFolder(declDir, definition.categories);
    const auto existing = findCustomTool(definition.nameEn);
    const auto previous = existing ? existing->definition.definitionPath : std::filesystem::path{};
    const auto jsonPath = jsonDir / (existing ? previous.filename() : std::filesystem::path(definition.nameEn + ".json"));
    definition.definitionPath = jsonPath;

    // A tool moved to another folder keeps one declaration: the former file would otherwise be
    // discovered as a second copy of the same id.
    std::filesystem::create_directories(jsonDir);
    const auto registrationPath      = rootDir_ / "integrations.json";
    const bool hadRegistrations      = std::filesystem::exists(registrationPath);
    const auto previousRegistrations = hadRegistrations ? json::read(registrationPath) : QByteArray{};
    auto       registrations         = integrations();
    if (definition.integrations.processing || definition.integrations.training)
        registrations[definition.nameEn] = definition.integrations;
    else
        registrations.erase(definition.nameEn);
    writeIntegrations(registrationPath, registrations);
    try
    {
        writeAtomically(jsonPath, emitNormalizedToolJson(definition), "工具声明");
    }
    catch (...)
    {
        if (hadRegistrations)
            writeAtomically(registrationPath, previousRegistrations.toStdString(), "交互登记");
        else
            std::filesystem::remove(registrationPath);
        throw;
    }
    if (!previous.empty() && previous != jsonPath)
    {
        std::error_code error;
        std::filesystem::remove(previous, error);
        if (error)
            throw ContractError("无法删除旧位置的工具声明：" + previous.string() + "：" + error.message());
    }
    return definition;
}

bool CustomToolStore::deleteCustomTool(const std::string &toolId)
{
    validateIdentifier(toolId, false);

    const auto existing = findCustomTool(toolId);
    if (!existing)
        return false;
    const auto &jsonPath = existing->definition.definitionPath;

    // The declaration owns the code directory, and its package is not necessarily the id spelled
    // with slashes, so read it before the declaration that names it is gone.
    // An unreadable declaration cannot name the code directory it owns, so it is not deleted: a
    // guessed removal could take another tool's resources with it.
    const auto &package = existing->definition.entry.package;

    // The declaration goes first: once it is gone the tool cannot be discovered, so a leftover code
    // directory is an orphan a later pass can clean instead of a half-deleted tool that still runs.
    std::error_code error;
    std::filesystem::remove(jsonPath, error);
    if (error)
        throw ContractError("无法删除工具声明：" + jsonPath.string() + "：" + error.message());

    auto registrations = integrations();
    if (registrations.erase(toolId))
    {
        writeIntegrations(rootDir_ / "integrations.json", registrations);
    }

    if (package.empty())
        return true;

    const auto scriptDir = pythonDir() / package;
    if (!std::filesystem::exists(scriptDir))
        return true;

    // A package another declaration still references is not ours to remove.
    for (const auto &entry : std::filesystem::recursive_directory_iterator(declarationsDir()))
    {
        if (!entry.is_regular_file() || entry.path().extension() != ".json")
            continue;
        try
        {
            if (parseToolDefinitionFile(entry.path()).entry.package == package)
                return true;
        }
        catch (const std::exception &error)
        {
            // Another declaration may point at the same package. Not knowing its answer means the
            // directory is not provably ours, so it stays.
            throw ContractError("无法确认工具包归属：" + entry.path().string() + "：" + error.what());
        }
    }
    std::filesystem::remove_all(scriptDir, error);
    return true;
}

ToolDefinition CustomToolStore::cloneCustomTool(const ToolDefinition &source, const std::string &newToolId, const std::string &newNameZh)
{
    // A clone publishes a new tool. Writing over an existing id would silently replace published
    // ports and bindings with another tool's, so the caller has to pick a free id.
    if (findCustomTool(newToolId))
        throw ContractError("工具标识已存在，不能覆盖：" + newToolId);
    validateIdentifier(newToolId, false);

    // A C++ or host tool is a registered factory, not a declaration: renaming its JSON would publish
    // a tool nothing can instantiate. The editor offers copying only for Python tools.
    if (source.kind != ToolKind::PythonFunction && source.kind != ToolKind::PythonClass)
        throw ContractError("只有 Python 自定义工具可以复制：" + source.nameEn);

    ToolDefinition cloned = source;
    cloned.nameEn         = newToolId;
    cloned.nameZh         = newNameZh;

    // The script comes from the root the definition actually lives in: a copied installed example
    // sits under the install root, while a draft only knows the store it will be saved into.
    const std::filesystem::path oldPackage = source.source.roots.python.empty()
                                                 ? resolveToolPath(pythonDir(), source.entry.package)
                                                 : source.pythonPackage();
    std::string                 newPackage = newToolId;
    std::replace(newPackage.begin(), newPackage.end(), '.', '/');
    cloned.entry.package = newPackage;

    const auto sourceScript = oldPackage / (source.entry.file.empty() ? "tool.py" : source.entry.file);
    std::ifstream in(sourceScript, std::ios::binary);
    if (!in.is_open())
        throw ContractError("源工具缺少 Python 脚本，不能复制：" + sourceScript.string());
    const std::string script((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());

    // The resources are part of the tool: a copy that only renames the id could not run, so they are
    // carried under the same relative names the declaration already lists.
    const auto targetPackage = pythonDir() / newPackage;
    for (const auto &resource : source.resources)
    {
        const auto from = resolveToolPath(oldPackage, resource);
        const auto to   = targetPackage / resource;
        if (!std::filesystem::exists(from))
            throw ContractError("源工具缺少资源文件，不能复制：" + from.string());
        if (!to.parent_path().empty())
            std::filesystem::create_directories(to.parent_path());
        std::filesystem::copy_file(from, to, std::filesystem::copy_options::overwrite_existing);
    }

    return saveCustomTool(cloned, script);
}

} // namespace visionrt
