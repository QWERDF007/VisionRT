#include "core/ToolPackage.h"

#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QTemporaryDir>
#include <cassert>
#include <iostream>

using namespace visionrt;

namespace {
template<class Action>
void rejects(Action action)
{
    bool rejected = false;
    try
    {
        action();
    }
    catch (const ContractError &)
    {
        rejected = true;
    }
    assert(rejected);
}

void write(const QString &path, const QByteArray &bytes)
{
    QFile file(path);
    assert(file.open(QIODevice::WriteOnly));
    assert(file.write(bytes) == bytes.size());
}

const QByteArray declaration = R"json({
    "schema_version":1,"tool_id":"test.package","display_name":"包工具","version":"1",
    "kind":"cpp","init_params":[],"inputs":[],"outputs":[]
})json";

QJsonObject index()
{
    return {
        {"schema_version",                                                           1                          },
        {       "variant",                                                                                 "cpu"},
        {       "modules",                 QJsonArray{QJsonObject{{"id", "module"}, {"library", "missing.dll"}}}},
        {         "tools",
         QJsonArray{QJsonObject{{"tool_id", "test.package"}, {"definition", "tool.json"}, {"module", "module"}}}},
        { "runtime_files",                                                             QJsonArray{"runtime.dll"}}
    };
}
}

int main()
{
    QTemporaryDir directory;
    assert(directory.isValid());
    const std::filesystem::path root(directory.path().toStdWString());
    const auto                  indexPath = directory.path() + "/module_index.json";
    const auto                  putIndex  = [&](const QJsonObject &value)
    {
        write(indexPath, QJsonDocument(value).toJson());
    };
    write(directory.path() + "/tool.json", declaration);
    write(directory.path() + "/unindexed.json", "not a declaration");
    putIndex(index());
    const auto package = readToolsPackage(root);
    assert(package.index.schemaVersion == 1 && package.index.variant == "cpu");
    assert(package.tools.size() == 1 && package.tools.front().definition.id == "test.package");
    assert(package.tools.front().definition.outputs.size() == 2);
    assert(package.tools.front().moduleLibrary == root / "missing.dll");
    rejects([&] { validateToolsPackage(root); });
    write(directory.path() + "/missing.dll", "fixture");
    rejects([&] { validateToolsPackage(root); });
    write(directory.path() + "/runtime.dll", "fixture");
    validateToolsPackage(root);

    const auto rejectIndex = [&](QJsonObject value)
    {
        putIndex(value);
        rejects([&] { readToolsPackage(root); });
    };
    auto invalid              = index();
    invalid["schema_version"] = 2;
    rejectIndex(invalid);
    invalid            = index();
    invalid["variant"] = false;
    rejectIndex(invalid);
    invalid          = index();
    invalid["tools"] = QJsonObject{};
    rejectIndex(invalid);
    invalid            = index();
    invalid["unknown"] = true;
    rejectIndex(invalid);
    invalid = index();
    invalid.remove("runtime_files");
    rejectIndex(invalid);
    for (const auto field : {"modules", "tools"})
    {
        invalid    = index();
        auto items = invalid[field].toArray();
        items.append(items.first());
        invalid[field] = items;
        rejectIndex(invalid);
    }
    for (const auto library : {"../escape.dll", "C:/escape.dll", "/escape.dll"})
    {
        invalid            = index();
        invalid["modules"] = QJsonArray{
            QJsonObject{{"id", "module"}, {"library", library}}
        };
        rejectIndex(invalid);
    }
    for (const auto field : {"definition", "module", "host_binding", "tool_id"})
    {
        invalid          = index();
        auto tool        = invalid["tools"].toArray().first().toObject();
        tool[field]      = "invalid";
        invalid["tools"] = QJsonArray{tool};
        rejectIndex(invalid);
    }
    putIndex(index());
    auto tool     = QJsonDocument::fromJson(declaration).object();
    tool["kind"]  = "python_function";
    tool["entry"] = QJsonObject{
        {  "file", "script.py"},
        {"symbol",   "execute"}
    };
    write(directory.path() + "/tool.json", QJsonDocument(tool).toJson());
    rejects([&] { readToolsPackage(root); });
    auto pythonIndex = index();
    auto pythonTool  = pythonIndex["tools"].toArray().first().toObject();
    pythonTool.remove("module");
    pythonIndex["tools"]         = QJsonArray{pythonTool};
    pythonIndex["modules"]       = QJsonArray{};
    pythonIndex["runtime_files"] = QJsonArray{};
    putIndex(pythonIndex);
    assert(readToolsPackage(root).tools.front().definition.kind == ToolKind::PythonFunction);
    rejects([&] { validateToolsPackage(root); });
    write(directory.path() + "/script.py", "def execute(inputs): return {}\n");
    validateToolsPackage(root);

    tool["entry"] = QJsonObject{
        {  "file", "../outside.py"},
        {"symbol",       "execute"}
    };
    write(directory.path() + "/tool.json", QJsonDocument(tool).toJson());
    rejects([&] { readToolsPackage(root); });
    std::filesystem::create_directories(root / "nested");
    pythonTool["definition"] = "nested/tool.json";
    pythonIndex["tools"]     = QJsonArray{pythonTool};
    putIndex(pythonIndex);
    tool["entry"] = QJsonObject{
        {  "file", "../script.py"},
        {"symbol",      "execute"}
    };
    tool["resources"] = QJsonArray{"../runtime.dll"};
    write(directory.path() + "/nested/tool.json", QJsonDocument(tool).toJson());
    validateToolsPackage(root);
    assert(readToolsPackage(root).tools.front().definition.script == root / "script.py");

    tool["kind"]  = "host";
    tool["entry"] = QJsonObject{
        {"binding", "visionrt.receiver"}
    };
    write(directory.path() + "/nested/tool.json", QJsonDocument(tool).toJson());
    assert(readToolsPackage(root).tools.front().definition.hostBinding == "visionrt.receiver");
    assert(QFile::remove(indexPath));
    write(directory.path() + "/module_index.yaml", "schema_version: 2");
    rejects([&] { readToolsPackage(root); });
    std::cout << "JSON package identity, isolation, containment and delivery validation verified\n";
}
