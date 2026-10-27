#include "core/ToolPackage.h"
#include "core/ToolRegistry.h"

#include <QDir>
#include <QCoreApplication>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QProcess>
#include <QTemporaryDir>
#include <algorithm>
#include <cassert>
#include <iostream>

using namespace visionrt;

namespace {
void linkDirectory(const QString &link, const QString &target)
{
    assert(QDir().mkpath(QFileInfo(link).absolutePath()));
#ifdef _WIN32
    QProcess command;
    command.start("cmd.exe",
                  {"/d", "/c", "mklink", "/J", QDir::toNativeSeparators(link), QDir::toNativeSeparators(target)});
    assert(command.waitForFinished(30000));
    if (command.exitCode() != 0)
        std::cerr << command.readAllStandardError().toStdString();
    assert(command.exitStatus() == QProcess::NormalExit && command.exitCode() == 0);
#else
    std::filesystem::create_directory_symlink(std::filesystem::path(target.toStdWString()),
                                              std::filesystem::path(link.toStdWString()));
#endif
}

void write(const QString &path, const QByteArray &bytes)
{
    assert(QDir().mkpath(QFileInfo(path).absolutePath()));
    QFile file(path);
    assert(file.open(QIODevice::WriteOnly));
    assert(file.write(bytes) == bytes.size());
}

QByteArray declaration(const QString &id)
{
    return QJsonDocument(QJsonObject{
                             {"schema_version",                                                                                  4},
                             {       "tool_id",                                                                                 id},
                             {  "display_name",                                                                     "同名工具"},
                             {       "version",                                                                                "1"},
                             {          "kind",                                                                  "python_function"},
                             {         "entry", QJsonObject{{"package", "示例 包"}, {"file", "tool.py"}, {"symbol", "execute"}}},
                             {   "init_params",                                                                       QJsonArray{}},
                             {        "inputs",                                                                       QJsonArray{}},
                             {       "outputs",                                                                       QJsonArray{}}
    })
        .toJson();
}
}

int main(int argc, char **argv)
{
    QCoreApplication application(argc, argv);
    assert(argc == 2);
    QTemporaryDir directory;
    assert(directory.isValid());
    const std::filesystem::path root(directory.path().toStdWString());
    const auto                  builtin = directory.filePath("tools/builtin/内建.json");
    const auto                  custom  = directory.filePath("tools/custom/nested/自定义.json");
    write(builtin, declaration("test.builtin"));
    write(custom, declaration("test.custom"));
    write(directory.filePath("python/builtin/示例 包/tool.py"),
          "raise RuntimeError('must not import during discovery')\n");
    write(directory.filePath("python/builtin/示例 包/resource.json"), "{}\n");
    write(directory.filePath("module_index.json"),
          QJsonDocument(QJsonObject{
                            {"schema_version",                                                                                                         1},
                            {       "variant",                                                                                                     "cpu"},
                            {       "modules",                                                                                              QJsonArray{}},
                            {         "tools", QJsonArray{QJsonObject{{"tool_id", "test.builtin"},
 {"definition", "tools/builtin/内建.json"}}}                                                            },
                            { "runtime_files",                                                                                              QJsonArray{}}
    })
              .toJson());
    const auto package = readToolsPackage(root);
    assert(package.tools.size() == 2);
    const auto &first  = package.tools[0].definition;
    const auto &second = package.tools[1].definition;
    assert(first.id == "test.builtin" && first.source.origin == ToolOrigin::Builtin);
    assert(second.id == "test.custom" && second.source.origin == ToolOrigin::Custom);
    assert(first.pythonScript()
           == std::filesystem::weakly_canonical(root / "python/builtin" / first.entry.package / "tool.py"));
    assert(second.pythonScript()
           == std::filesystem::weakly_canonical(root / "python/custom" / second.entry.package / "tool.py"));
    assert(!std::filesystem::exists(second.pythonScript()));
    assert(first.entry.package == second.entry.package);
    assert(!QJsonDocument::fromJson(QByteArray::fromStdString(emitNormalizedToolJson(first)))
                .object()
                .contains("builtin"));
    ToolRegistry catalog;
    catalog.registerPackage(package);
    assert(catalog.definitions().size() == 2);
    write(custom, declaration("test.builtin"));
    bool rejected = false;
    try
    {
        catalog.registerPackage(readToolsPackage(root));
    }
    catch (const ContractError &error)
    {
        const auto message = QString::fromUtf8(error.what());
        rejected           = message.contains("内建.json") && message.contains("自定义.json");
    }
    assert(rejected && catalog.definitions().size() == 2 && catalog.find("test.custom"));
    write(custom, declaration("test.custom"));
    const auto validCustom = QJsonDocument::fromJson(declaration("test.custom")).object();
    for (const auto field : {"package", "file"})
    {
        auto invalid     = validCustom;
        auto entry       = invalid["entry"].toObject();
        entry[field]     = "../outside.py";
        invalid["entry"] = entry;
        write(custom, QJsonDocument(invalid).toJson());
        bool escaped = false;
        try
        {
            (void)readToolsPackage(root);
        }
        catch (const ContractError &)
        {
            escaped = true;
        }
        assert(escaped);
    }
    auto invalidResource         = validCustom;
    invalidResource["resources"] = QJsonArray{"../outside.json"};
    write(custom, QJsonDocument(invalidResource).toJson());
    bool escapedResource = false;
    try
    {
        (void)readToolsPackage(root);
    }
    catch (const ContractError &)
    {
        escapedResource = true;
    }
    assert(escapedResource);
    write(custom, declaration("test.custom"));
    bool missingScript = false;
    try
    {
        validateToolsPackage(root);
    }
    catch (const ContractError &error)
    {
        missingScript = QString::fromUtf8(error.what()).contains("tool.py");
    }
    assert(missingScript);
    write(directory.filePath("python/custom/示例 包/tool.py"),
          "raise RuntimeError('metadata-only CLI must not import')\n");
    for (const auto &options : {
             QStringList{"--package", directory.path()},
             QStringList{"--package", directory.path(), "--load"}
    })
    {
        QProcess validator;
        validator.start(QString::fromLocal8Bit(argv[1]), options);
        assert(validator.waitForFinished(30000));
        if (validator.exitCode() != 0)
            std::cerr << validator.readAllStandardError().toStdString();
        assert(validator.exitStatus() == QProcess::NormalExit && validator.exitCode() == 0);
        assert(!validator.readAllStandardOutput().contains("Native factory available"));
    }
    QTemporaryDir external;
    assert(external.isValid());
    const ToolRoots customRoots{std::filesystem::path(external.filePath("declarations").toStdWString()),
                                std::filesystem::path(external.filePath("code").toStdWString())};
    write(external.filePath("declarations/external.json"), declaration("test.external"));
    const auto overridden = readToolsPackage(root, customRoots);
    assert(overridden.tools.size() == 2);
    const auto externalTool = std::find_if(overridden.tools.begin(), overridden.tools.end(),
                                           [](const auto &tool) { return tool.definition.id == "test.external"; });
    assert(externalTool != overridden.tools.end());
    assert(externalTool->definition.pythonPackage()
           == std::filesystem::weakly_canonical(customRoots.python / first.entry.package));
    bool incompleteRoots = false;
    try
    {
        (void)readToolsPackage(root, ToolRoots{customRoots.declarations, {}});
    }
    catch (const ContractError &)
    {
        incompleteRoots = true;
    }
    assert(incompleteRoots);
    QTemporaryDir resourceExternal;
    assert(resourceExternal.isValid());
    const ToolRoots resourceRoots{std::filesystem::path(resourceExternal.filePath("declarations").toStdWString()),
                                  std::filesystem::path(resourceExternal.filePath("code").toStdWString())};
    const auto hostResource = QJsonObject{
        {"schema_version", 4},
        {"tool_id", "test.host.resource"},
        {"display_name", "资源宿主"},
        {"version", "1"},
        {"kind", "host"},
        {"entry", QJsonObject{{"binding", "visionrt.receiver"}}},
        {"init_params", QJsonArray{}},
        {"inputs", QJsonArray{}},
        {"outputs", QJsonArray{}},
        {"resources", QJsonArray{"data.bin"}}
    };
    write(resourceExternal.filePath("declarations/host.json"), QJsonDocument(hostResource).toJson());
    write(resourceExternal.filePath("declarations/data.bin"), "data");
    const auto withResource = readToolsPackage(root, resourceRoots);
    assert(withResource.tools.size() == 2);
    assert(std::any_of(withResource.tools.begin(), withResource.tools.end(), [](const auto &tool)
                       { return tool.definition.id == "test.host.resource"; }));
    auto escapedHost       = hostResource;
    escapedHost["resources"] = QJsonArray{"../outside.bin"};
    write(resourceExternal.filePath("declarations/host.json"), QJsonDocument(escapedHost).toJson());
    bool escapedHostResource = false;
    try
    {
        (void)readToolsPackage(root, resourceRoots);
    }
    catch (const ContractError &)
    {
        escapedHostResource = true;
    }
    assert(escapedHostResource);
    QTemporaryDir overridePackage;
    assert(overridePackage.isValid());
    write(overridePackage.filePath("module_index.json"),
          R"({"schema_version":1,"variant":"cpu","modules":[],"tools":[
              {"tool_id":"custom.indexed","definition":"tools/custom/indexed.json"}],
              "runtime_files":[]})");
    QTemporaryDir overrideExternal;
    assert(overrideExternal.isValid());
    const ToolRoots overrideRoots{std::filesystem::path(overrideExternal.filePath("declarations").toStdWString()),
                                  std::filesystem::path(overrideExternal.filePath("code").toStdWString())};
    write(overrideExternal.filePath("declarations/external.json"), declaration("test.external"));
    const auto overriddenIndex = readToolsPackage(std::filesystem::path(overridePackage.path().toStdWString()),
                                                  overrideRoots);
    assert(overriddenIndex.tools.size() == 1 && overriddenIndex.tools.front().definition.id == "test.external");
    QTemporaryDir linked;
    assert(linked.isValid());
    write(linked.filePath("module_index.json"),
          R"({"schema_version":1,"variant":"cpu","modules":[],"tools":[],"runtime_files":[]})");
    const auto codeRootLink = linked.filePath("python/custom");
    linkDirectory(codeRootLink, external.path());
    bool escapedCodeRoot = false;
    try
    {
        (void)readToolsPackage(std::filesystem::path(linked.path().toStdWString()));
    }
    catch (const ContractError &)
    {
        escapedCodeRoot = true;
    }
    assert(std::filesystem::remove(std::filesystem::path(codeRootLink.toStdWString())));
    assert(escapedCodeRoot);
    const auto entryLink = directory.filePath("python/builtin/示例 包/escape");
    linkDirectory(entryLink, external.path());
    for (const bool resource : {false, true})
    {
        auto escaped = QJsonDocument::fromJson(declaration("test.builtin")).object();
        if (resource)
            escaped["resources"] = QJsonArray{"escape/resource.json"};
        else
        {
            auto entry       = escaped["entry"].toObject();
            entry["file"]    = "escape/tool.py";
            escaped["entry"] = entry;
        }
        write(builtin, QJsonDocument(escaped).toJson());
        bool rejectedLink = false;
        try
        {
            (void)readToolsPackage(root);
        }
        catch (const ContractError &)
        {
            rejectedLink = true;
        }
        assert(rejectedLink);
    }
    write(builtin, declaration("test.builtin"));
    assert(std::filesystem::remove(std::filesystem::path(entryLink.toStdWString())));
    std::cout << "JSON discovery separates builtin/custom code roots without importing Python and rejects duplicate "
                 "identities atomically\n";
}
