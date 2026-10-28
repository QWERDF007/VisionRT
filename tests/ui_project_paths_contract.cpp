#include "WorkbenchFixtures.h"
#include "core/DefinitionJson.h"
#include "ui/AppController.h"
#include "ui/OutputModel.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QGuiApplication>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QTemporaryDir>
#include <QSignalSpy>
#include <QTest>
#include <iostream>
#include <stdexcept>

using namespace visionrt;

namespace {
void require(bool condition, const char *message)
{
    if (!condition)
        throw std::runtime_error(message);
}
}

int main(int argc, char **argv)
try
{
    QGuiApplication                application(argc, argv);
    testing::WorkbenchInstallation installation;
    const auto                     root = installation.directory();
    const auto destinationBase = [&]() -> QString
    {
        const auto rootDrive = std::filesystem::path(root.toStdU16String()).root_name().string();
        if (QDir("E:/tmp").exists() && rootDrive != "E:")
            return QStringLiteral("E:/tmp/路径 另存-XXXXXX");
        return QCoreApplication::applicationDirPath() + "/路径 另存-XXXXXX";
    }();
    QTemporaryDir destination(destinationBase);
    require(destination.isValid(), "cannot create project destination beside the test executable");
    const auto write = [&](const QString &path, const QByteArray &bytes)
    {
        require(QDir().mkpath(QFileInfo(path).absolutePath()), "cannot create path fixture parent");
        QFile file(path);
        require(file.open(QIODevice::WriteOnly) && file.write(bytes) == bytes.size(), "path fixture write failed");
    };
    const auto read = [&](const QString &path)
    {
        QFile file(path);
        require(file.open(QIODevice::ReadOnly), "path fixture read failed");
        return file.readAll();
    };
    ToolDefinition definition;
    definition.id          = "test.project_paths";
    definition.displayName = "工程路径实读";
    definition.kind        = ToolKind::PythonClass;
    definition.entry       = {"project_paths", "tool.py", "Reader"};
    for (const auto &id : {"file", "directory", "dynamic", "mode", "empty", "missing", "note"})
    {
        PortDefinition port;
        port.id       = id;
        port.name     = id;
        port.type     = {Type::String};
        port.required = port.id != "missing";
        port.editor   = std::string(id) == "directory" ? "directory" : "file";
        if (port.id == "dynamic")
        {
            port.editor       = "path";
            port.pathModeFrom = "mode";
        }
        if (port.id == "mode" || port.id == "note")
        {
            port.editor       = "text";
            port.defaultValue = std::string(port.id == "mode" ? "file" : "original");
        }
        if (port.id == "empty")
            port.defaultValue = std::string{};
        definition.inputs.push_back(port);
    }
    definition.initParams = definition.inputs;
    for (auto &port : definition.initParams)
    {
        port.id = "init_" + port.id;
        if (!port.pathModeFrom.empty())
            port.pathModeFrom = "init_" + port.pathModeFrom;
    }
    definition.outputs = {
        {"data", "实读内容", {Type::String}}
    };
    write(root + "/tools-package/tools/custom/project_paths/reader.json",
          QByteArray::fromStdString(emitNormalizedToolJson(definition)));
    write(root + "/tools-package/python/custom/project_paths/tool.py", R"py(import json
from pathlib import Path

class Reader:
    def initialize(self, params):
        self.params = {name.removeprefix('init_'): value for name, value in params.items()}
        return {'code': 0, 'message': '', 'detail': ''}

    def release(self):
        return {'code': 0, 'message': '', 'detail': ''}

    def execute(self, inputs):
        result = {}
        for scope, values in [('inputs', inputs), ('init', self.params)]:
            assert values['empty'] == '' and values['missing'] is None
            for name in ['file', 'directory', 'dynamic']:
                path = Path(values[name])
                assert path.is_absolute(), str(path)
                resource = path / '资源 文本.txt' if name == 'directory' or (name == 'dynamic' and values['mode'] == 'directory') else path
                result[scope + '.' + name] = {'path': str(path), 'text': resource.read_text(encoding='utf-8')}
            result[scope + '.note'] = values['note']
        return {'status': {'code': 0, 'message': '', 'detail': ''},
                'outputs': {'data': json.dumps(result, ensure_ascii=False)}}
)py");
    const auto original  = root + "/原始 工程/项目.vrt.json";
    const auto sameDrive = root + "/同盘 另存/另存.vrt.json";
    const auto moved     = destination.filePath("跨盘 工程.vrt.json");
    const auto returned  = root + "/返回 同盘/目录模式.vrt.json";
    const auto folder    = QFileInfo(original).absolutePath() + "/资料 图像";
    const auto resource  = folder + "/资源 文本.txt";
    const auto content   = QStringLiteral("实际资源：雪山与海，9007199254740993\n");
    write(resource, content.toUtf8());
    require(QDir().mkpath(QFileInfo(sameDrive).absolutePath()), "same-drive destination unavailable");
    AppController app(2, root);
    const auto    idle = [&]
    {
        require(QTest::qWaitFor([&] { return !app.busy(); }, 10000), "project path operation did not finish");
    };
    idle();
    app.addNode("test.project_paths", 0, 0);
    idle();
    QSignalSpy replacements(&app, &AppController::projectReplaced);
    const auto nodeId = app.selectedNode();
    require(!nodeId.isEmpty(), "path fixture node was not selected");
    auto *outputs = qobject_cast<OutputModel *>(app.outputModel());
    require(outputs, "path fixture output model unavailable");
    const auto commit = [&](const QString &id, bool initialization, const QString &value)
    {
        require(app.setParameter(initialization ? "init_" + id : id, initialization, value),
                "path parameter was not committed");
        idle();
        if (!initialization)
        {
            require(app.setParameterMode(id, false), "path parameter did not switch to literal mode");
            idle();
        }
    };
    for (bool initialization : {false, true})
    {
        commit("file", initialization, "资料 图像/资源 文本.txt");
        commit("directory", initialization, "资料 图像");
        commit("dynamic", initialization, "资料 图像/资源 文本.txt");
    }
    idle();
    const auto execute = [&](const QString &note, bool directory = false)
    {
        app.runNode();
        idle();
        const auto value = outputs->capture("data");
        if (!value || !std::holds_alternative<std::string>(value->value))
        {
            const auto status = outputs->capture("__status_message");
            throw std::runtime_error(
                "real Python resource read failed: "
                + (status ? OutputModel::diagnosticText(*status) : app.diagnosticsJson()).toStdString());
        }
        const auto actual
            = QJsonDocument::fromJson(QByteArray::fromStdString(std::get<std::string>(value->value))).object();
        require(actual.size() == 8, "Python resource report is incomplete");
        for (const auto &scope : {QStringLiteral("inputs"), QStringLiteral("init")})
        {
            for (const auto &name : {QStringLiteral("file"), QStringLiteral("directory"), QStringLiteral("dynamic")})
            {
                const auto item = actual.value(scope + "." + name).toObject();
                require(item.value("text").toString() == content, "save-as changed actual resource content");
                require(QFileInfo(item.value("path").toString()).canonicalFilePath()
                            == QFileInfo(name == "file" || (name == "dynamic" && !directory) ? resource : folder)
                                   .canonicalFilePath(),
                        "Python received a path to a different resource");
            }
            require(actual.value(scope + ".note").toString() == note, "failed save changed the parameter draft");
        }
    };
    const auto saved = [&](const QString &path)
    {
        return QJsonDocument::fromJson(read(path))
            .object()
            .value("flows")
            .toArray()[0]
            .toObject()
            .value("nodes")
            .toArray()[0]
            .toObject();
    };
    const auto checkPaths = [&](const QString &path, bool absolute, bool dynamicAbsolute = false)
    {
        const auto node = saved(path);
        for (const auto &scope : {QStringLiteral("inputs"), QStringLiteral("init_params")})
        {
            const auto values = node.value(scope).toObject();
            for (const auto &name : {QStringLiteral("file"), QStringLiteral("directory"), QStringLiteral("dynamic"),
                                     QStringLiteral("empty"), QStringLiteral("missing")})
            {
                const auto value
                    = scope == "inputs" ? values.value(name).toObject().value("literal") : values.value("init_" + name);
                if (name == "missing")
                    require(value.isNull(), "save-as replaced Missing path");
                else if (name == "empty")
                    require(value.isString() && value.toString().isEmpty(), "save-as replaced empty path");
                else
                {
                    const auto stored          = value.toString();
                    const bool expectAbsolute  = name == "dynamic" && dynamicAbsolute ? true : absolute;
                    const auto policy = "save-as used an invalid path root policy: " + scope.toStdString() + "." + name.toStdString()
                                        + " stored=" + stored.toStdString()
                                        + " project=" + QFileInfo(path).absolutePath().toStdString();
                    require(QDir::isAbsolutePath(stored) == expectAbsolute, policy.c_str());
                    const auto mode = scope == "inputs" ? values.value("mode").toObject().value("literal").toString()
                                                        : values.value("init_mode").toString();
                    const auto expected = name == "file" || (name == "dynamic" && mode == "file") ? resource : folder;
                    const auto resolved
                        = QFileInfo(QDir(QFileInfo(path).absolutePath()).absoluteFilePath(stored)).canonicalFilePath();
                    const auto resolution = "saved Unicode path no longer resolves to the original resource: "
                                            + scope.toStdString() + "." + name.toStdString()
                                            + " stored=" + stored.toStdString() + " resolved=" + resolved.toStdString()
                                            + " expected=" + QFileInfo(expected).canonicalFilePath().toStdString();
                    require(resolved == QFileInfo(expected).canonicalFilePath(), resolution.c_str());
                }
            }
        }
    };
    app.runNode();
    idle();
    const auto unnamed           = outputs->capture("data");
    const auto unnamedStatus     = outputs->capture("__status_message");
    const auto unnamedDiagnostic = unnamedStatus ? OutputModel::diagnosticText(*unnamedStatus) : app.diagnosticsJson();
    if (!unnamed || !isMissing(unnamed->value)
        || !unnamedDiagnostic.contains(QStringLiteral("相对资源路径需要先保存工程")))
        throw std::runtime_error("unnamed resource rejection is incomplete: " + unnamedDiagnostic.toStdString());
    require(app.save(original), "first project save failed");
    idle();
    execute("original");
    checkPaths(original, false);
    require(app.save(sameDrive), "same-drive save-as failed");
    idle();
    checkPaths(sameDrive, false);
    execute("original");
    require(app.save(moved), "installed-drive save-as failed");
    idle();
    const auto resourceRoot = std::filesystem::path(resource.toStdU16String()).root_name();
    const auto projectRoot  = std::filesystem::path(moved.toStdU16String()).root_name();
    const bool crossDrive   = resourceRoot != projectRoot;
    checkPaths(moved, crossDrive);
    execute("original");
    app.requestOpen(moved);
    idle();
    require(replacements.count() == 1, "cross-drive project was not actually reopened");
    app.selectNode(nodeId);
    execute("original");
    for (bool initialization : {false, true})
    {
        commit("mode", initialization, "directory");
        commit("dynamic", initialization, folder);
        commit("note", initialization, "directory");
    }
    idle();
    require(app.save(), "dynamic directory save failed");
    idle();
    // The last change above was a plain save, and a plain save keeps the literal exactly as it was
    // committed; `dynamic` was entered as an absolute folder after the last save-as, so it stays
    // absolute until the next save-as rebases it.
    checkPaths(moved, crossDrive, true);
    execute("directory", true);
    require(QDir().mkpath(QFileInfo(returned).absolutePath()), "return destination unavailable");
    require(app.save(returned), "cross-drive return save-as failed");
    idle();
    checkPaths(returned, false);
    app.requestOpen(returned);
    idle();
    require(replacements.count() == 2, "returned project was not actually reopened");
    app.selectNode(nodeId);
    execute("directory", true);
    const auto beforeFailure = read(returned);
    write(root + "/阻塞 父位置", "not a directory");
    commit("note", false, "failed-directory");
    commit("note", true, "failed-directory");
    idle();
    require(app.dirty() && !app.save(root + "/阻塞 父位置/失败.vrt.json") && app.dirty(),
            "failed save-as changed dirty state");
    require(app.diagnosticsJson().contains(QStringLiteral("工程保存失败"))
                && app.diagnosticsJson().contains(QStringLiteral("阻塞 父位置")),
            "failed save omitted its actual destination diagnostic");
    require(read(returned) == beforeFailure, "failed save-as changed the original project file");
    execute("failed-directory", true);
    require(app.save(), "failed save-as replaced the current project destination");
    idle();
    checkPaths(returned, false);
    require(saved(returned).value("inputs").toObject().value("note").toObject().value("literal").toString()
                == "failed-directory",
            "saved draft went to a different project");
    std::cout << "Project paths: resource_root=" << resourceRoot.string() << " project_root=" << projectRoot.string()
              << " cross_drive=" << crossDrive
              << " successful_executions=7 resource_reads=42 same_drive=1 reopened=" << replacements.count()
              << " cross_drive_return=" << crossDrive
              << " dynamic_directory=1 unnamed_rejected=1 failed_save_rollback=1\n";
    return 0;
}
catch (const std::exception &error)
{
    std::cerr << error.what() << '\n';
    return 1;
}
