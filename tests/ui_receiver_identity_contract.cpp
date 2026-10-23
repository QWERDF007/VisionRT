#include "WorkbenchFixtures.h"
#include "core/ProjectEditor.h"
#include "core/ProjectStorage.h"
#include "core/ToolPackage.h"
#include "core/ToolRegistry.h"
#include "ui/AppController.h"
#include "ui/OutputModel.h"
#include "ui/Theme.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QGuiApplication>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJSValue>
#include <QQmlComponent>
#include <QQmlContext>
#include <QQmlEngine>
#include <QQuickItem>
#include <QQuickWindow>
#include <QSignalSpy>
#include <QTest>
#include <iostream>
#include <memory>
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
    const auto                     root  = installation.directory();
    const auto                     write = [&](const QString &relative, const QByteArray &content)
    {
        const auto filename = root + "/tools-package/" + relative;
        require(QDir().mkpath(QFileInfo(filename).absolutePath()), "cannot create identity fixture directory");
        QFile file(filename);
        require(file.open(QIODevice::WriteOnly) && file.write(content) == content.size(),
                "cannot write identity fixture");
    };
    write("tools/custom/identity/source.json", R"json({
        "schema_version":2,"tool_id":"test.identity.source","display_name":"来源","version":"1.0",
        "kind":"python_function","entry":{"package":"identity","file":"source.py","symbol":"run"},
        "init_params":[],"inputs":[{"id":"value","name":"值","type":"Int","default":0}],
        "outputs":[{"id":"value","name":"值","type":"Int"}]
    })json");
    write("python/custom/identity/source.py",
          "import threading\n"
          "calls_lock = threading.Lock()\n"
          "def run(inputs):\n"
          "    with calls_lock, open(__file__ + '.calls', 'a') as calls:\n"
          "        calls.write(str(inputs['value']) + '\\n')\n"
          "    return {'__status_code': 0, '__status_message': '', 'value': inputs['value']}\n");
    const auto   tools = std::filesystem::path(root.toStdU16String()) / "tools-package";
    ToolRegistry registry;
    registry.registerPackage(readToolsPackage(tools));
    const auto *source   = registry.find("test.identity.source");
    const auto *receiver = registry.find("visionrt.flow.receive");
    require(source && receiver, "identity tools were not discovered from JSON");
    ProjectDefinition seed;
    seed.nextNodeId = 9'007'199'254'740'993ULL;
    ProjectEditor editor(std::move(seed));
    const auto    sourceFlow = editor.addFlow("来源流程");
    const auto    first      = editor.addNode(sourceFlow, *source, {});
    const auto    second     = editor.addNode(sourceFlow, *source, {1, 0});
    const auto    target     = editor.addNode("flow_1", *receiver, {});
    editor.setAlias(sourceFlow, first, "同名来源");
    editor.setAlias(sourceFlow, second, "同名来源");
    editor.setLiteral(sourceFlow, first, "value", std::int64_t{17});
    editor.setLiteral(sourceFlow, second, "value", std::int64_t{29});
    editor.setInitParam("flow_1", target, "source_flow_id", sourceFlow);
    editor.setInitParam("flow_1", target, "source_node_seq", std::to_string(first));
    editor.setInitParam("flow_1", target, "source_port_id", std::string("value"));
    editor.setInitParam("flow_1", target, "timeout_ms", std::int64_t{5000});
    ProjectStorage storage(registry);
    auto           project = editor.project();
    const auto     input   = root + "/宽 ID 工程.vrt.json";
    const auto     output  = root + "/已选择来源.vrt.json";
    storage.save(project, std::filesystem::path(input.toStdU16String()));
    AppController app(2, root);
    const auto    idle = [&]
    {
        require(QTest::qWaitFor([&] { return !app.busy(); }, 10000), "identity operation did not finish");
    };
    const auto pump = []
    {
        QCoreApplication::processEvents();
        QCoreApplication::processEvents();
    };
    idle();
    QSignalSpy replacements(&app, &AppController::projectReplaced);
    QSignalSpy decisions(&app, &AppController::unsavedDecisionRequired);
    app.requestOpen(input);
    require(decisions.size() == 1, "new unsaved project did not request a discard decision");
    app.resolveUnsaved("discard");
    idle();
    require(replacements.size() == 1, "wide-ID project was not actually opened");
    const auto targetId = QString::number(target);
    app.selectNode(targetId);
    const auto selectedId = app.nodes().front().toMap()["id"];
    require(app.selectedNode() == targetId && selectedId.metaType() == QMetaType::fromType<QString>()
                && selectedId.toString() == targetId,
            "workbench rounded the selected node identity");
    QSignalSpy configurationChanges(&app, &AppController::projectChanged);
    for (const auto &invalidId : {"0" + targetId, "+" + targetId, " " + targetId, targetId + " "})
    {
        app.renameNode(invalidId, "不应修改的名称");
        idle();
        require(configurationChanges.isEmpty(), "UI identity coercion modified a node through a textual alias");
        require(app.diagnostic().contains("节点标识"), "invalid UI identity lost its Chinese field diagnosis");
    }
    QQmlEngine engine;
    engine.addImportPath(QStringLiteral(VISIONRT_BINARY_DIR "/qml"));
    engine.rootContext()->setContextProperty("app", &app);
    engine.rootContext()->setContextProperty("Theme", Theme::instance());
    QQmlComponent component(&engine);
    component.setData(
        "import QtQuick\nimport \".\"\n"
        "ParameterEditor { width: 600; height: 36; "
        "parameter: app.parameters.find(function(row) { return row.id === 'source_node_seq' })"
        " || { id: '', name: '', type: 'String', literal: '', init: false } }",
        QUrl::fromLocalFile(QStringLiteral(VISIONRT_SOURCE_DIR "/src/ui/qml/IdentityContract.qml")));
    std::unique_ptr<QObject> owner(component.create());
    if (!owner)
        throw std::runtime_error(component.errorString().toStdString());
    auto *field = qobject_cast<QQuickItem *>(owner.get());
    require(field, "receiver parameter editor is not an item");
    QQuickWindow window;
    window.resize(600, 80);
    field->setParentItem(window.contentItem());
    window.show();
    require(QTest::qWaitForWindowExposed(&window), "identity editor window was not exposed");
    const auto findChoice = [&]() -> QQuickItem *
    {
        for (auto *item : field->findChildren<QQuickItem *>())
            if (item->property("textRole") == "label" && item->parentItem())
                return item;
        return nullptr;
    };
    auto *choice = findChoice();
    require(choice && choice->property("count").toInt() == 2, "receiver choices did not keep both same-name nodes");
    const auto rawModel = choice->property("model");
    const auto model    = rawModel.metaType() == QMetaType::fromType<QJSValue>()
                            ? rawModel.value<QJSValue>().toVariant().toList()
                            : rawModel.toList();
    require(model.size() == 2, "QML receiver option model is not a readable sequence");
    require(model[0].toMap()["label"].toString().startsWith("同名来源-")
                && model[1].toMap()["label"].toString().startsWith("同名来源-")
                && model[0].toMap()["value"].metaType() == QMetaType::fromType<QString>()
                && model[1].toMap()["value"].metaType() == QMetaType::fromType<QString>()
                && model[0].toMap()["value"].toString() == QString::number(first)
                && model[1].toMap()["value"].toString() == QString::number(second),
            "QML choices rounded identities or confused display names with IDs");
    auto *outputs = qobject_cast<OutputModel *>(app.outputModel());
    require(outputs, "receiver output model is missing");
    QSignalSpy activations(choice, SIGNAL(activated(int)));
    require(activations.isValid(), "receiver selector activation signal is missing");
    const auto run = [&](const QString &expected)
    {
        app.runOnce();
        idle();
        const auto status = outputs->capture("__status_code");
        const auto value  = outputs->capture("value");
        require(status && value, "receiver result ports are missing");
        if (OutputModel::fullText(*status) != "0" || OutputModel::fullText(*value) != expected)
            throw std::runtime_error("receiver expected=" + expected.toStdString()
                                     + " status=" + OutputModel::fullText(*status).toStdString()
                                     + " value=" + OutputModel::fullText(*value).toStdString()
                                     + " result=" + OutputModel::diagnosticText(*status).toStdString()
                                     + " diagnostic=" + app.diagnosticsJson().toStdString());
    };
    for (const auto key : {Qt::Key_Down, Qt::Key_Up, Qt::Key_Down})
    {
        choice->forceActiveFocus();
        QTest::keyClick(&window, key);
        idle();
        const auto expectedId = key == Qt::Key_Down ? second : first;
        require(field->property("valueText").toString() == QString::number(expectedId),
                "keyboard selection did not commit the exact uint64 identity");
        run(key == Qt::Key_Down ? "29" : "17");
    }
    require(activations.size() == 3, "receiver selector did not receive three actual keyboard activations");
    require(app.save(output), "wide-ID project save failed");
    const auto restored = storage.load(std::filesystem::path(output.toStdU16String()));
    require(restored.diagnostics.empty() && restored.project.flows[0].nodes[0].id == target
                && std::get<std::string>(restored.project.flows[0].nodes[0].initParams.at("source_node_seq"))
                       == std::to_string(second),
            "saved receiver identity changed");
    app.requestOpen(output);
    idle();
    require(replacements.size() == 2, "saved receiver project was not actually reopened");
    app.selectNode(targetId);
    choice = findChoice();
    require(choice, "receiver selector was not recreated for the reopened project");
    require(field->property("valueText").toString() == QString::number(second)
                && choice->property("currentIndex").toInt() == 1,
            "save/reopen did not restore the selected same-name source");
    run("29");
    QFile calls(root + "/tools-package/python/custom/identity/source.py.calls");
    require(calls.open(QIODevice::ReadOnly | QIODevice::Text), "real Python execution evidence is missing");
    auto lines = calls.readAll().trimmed().split('\n');
    if (lines.size() != 8 || lines.count("17") != 4 || lines.count("29") != 4)
        throw std::runtime_error("unexpected source executions: " + lines.join(',').toStdString());
    QFile saved(output);
    require(saved.open(QIODevice::ReadOnly), "cannot read receiver project for the unresolved-reference case");
    auto document   = QJsonDocument::fromJson(saved.readAll()).object();
    auto flows      = document["flows"].toArray();
    auto flow       = flows[0].toObject();
    auto nodes      = flow["nodes"].toArray();
    auto node       = nodes[0].toObject();
    auto parameters = node["init_params"].toObject();
    parameters.remove("source_flow_id");
    parameters.remove("source_node_seq");
    parameters.insert("source_flow_name", "来源流程");
    parameters.insert("source_node_name", "同名来源");
    auto types     = node["type_snapshot"].toObject();
    auto initTypes = types["init_params"].toObject();
    initTypes.remove("source_flow_id");
    initTypes.remove("source_node_seq");
    initTypes.insert("source_flow_name", QJsonObject{
                                             {"type", "String"}
    });
    initTypes.insert("source_node_name", QJsonObject{
                                             {"type", "String"}
    });
    types.insert("init_params", initTypes);
    node.insert("init_params", parameters);
    node.insert("type_snapshot", types);
    nodes[0] = node;
    flow.insert("nodes", nodes);
    flows[0] = flow;
    document.insert("flows", flows);
    const auto legacyPath = root + "/待重新选择的来源.vrt.json";
    QFile      legacy(legacyPath);
    const auto bytes = QJsonDocument(document).toJson();
    require(legacy.open(QIODevice::WriteOnly) && legacy.write(bytes) == bytes.size(),
            "cannot write unresolved-reference project");
    legacy.close();
    app.requestOpen(legacyPath);
    idle();
    require(replacements.size() == 3, "name-based source project was not actually opened");
    app.selectNode(targetId);
    require(app.diagnosticsJson().contains("重新选择"), "name-based source did not explain the required repair");
    app.runOnce();
    idle();
    require(calls.seek(0) && calls.readAll().trimmed().split('\n').size() == 8,
            "an unresolved name-based reference started the graph");
    require(app.setParameter("source_flow_id", true, QString::fromStdString(sourceFlow)),
            "cannot repair source flow identity");
    idle();
    require(app.setParameter("source_node_seq", true, QString::number(first)), "cannot repair source node identity");
    idle();
    run("17");
    require(calls.seek(0), "cannot reread Python execution evidence");
    lines = calls.readAll().trimmed().split('\n');
    require(lines.size() == 10 && lines.count("17") == 5 && lines.count("29") == 5,
            "repair did not enable exactly one real graph execution");
    require(app.save(legacyPath), "repaired receiver project save failed");
    QFile repaired(legacyPath);
    require(repaired.open(QIODevice::ReadOnly), "cannot reopen the repaired receiver project");
    const auto repairedNode = QJsonDocument::fromJson(repaired.readAll())
                                  .object()["flows"]
                                  .toArray()[0]
                                  .toObject()["nodes"]
                                  .toArray()[0]
                                  .toObject();
    const auto repairedParameters = repairedNode["init_params"].toObject();
    require(repairedParameters["source_flow_name"] == "来源流程" && repairedParameters["source_node_name"] == "同名来源"
                && repairedParameters["source_flow_id"].toString() == QString::fromStdString(sourceFlow)
                && repairedParameters["source_node_seq"].toString() == QString::number(first),
            "repair/save replaced the original names or changed the selected stable IDs");
    std::cout << "RECEIVER_IDENTITY_PASS first=" << first << " second=" << second << " target=" << target
              << " keyboard=3 source_executions=10 restored=29 repaired=17 rejected_execution=0\n";
    app.renameFlow(QString::fromStdString(sourceFlow), "已改名的流程");
    idle();
    app.selectFlow(QString::fromStdString(sourceFlow));
    app.renameNode(QString::number(first), "已改名的来源");
    idle();
    app.selectFlow("flow_1");
    app.selectNode(targetId);
    pump();
    auto *renamedChoice = findChoice();
    require(field->property("valueText").toString() == QString::number(first) && renamedChoice
                && renamedChoice->property("displayText").toString().startsWith("已改名的来源-"),
            "source rename changed the receiver identity or left its selector label stale");
    run("17");
    const auto renamedPath = root + "/改名后的稳定引用.vrt.json";
    require(app.save(renamedPath), "renamed receiver project save failed");
    const auto renamed = storage.load(std::filesystem::path(renamedPath.toStdU16String())).project;
    const auto &renamedReceiver = renamed.flows[0].nodes[0];
    require(renamed.flows[1].id == sourceFlow && renamed.flows[1].name == "已改名的流程"
                && renamed.flows[1].nodes[0].alias == "已改名的来源"
                && std::get<std::string>(renamedReceiver.initParams.at("source_flow_id")) == sourceFlow
                && std::get<std::string>(renamedReceiver.initParams.at("source_node_seq")) == std::to_string(first)
                && renamedReceiver.signatures.outputs.at("value").type == Type::Int,
            "rename/save changed stable receiver references or their resolved type");
    require(calls.seek(0) && calls.readAll().trimmed().split('\n').size() == 12,
            "renamed reference did not execute exactly one graph");
    app.selectFlow(QString::fromStdString(sourceFlow));
    app.deleteNodes({QString::number(first)});
    idle();
    require(app.diagnosticsJson().contains("源节点不存在") && app.diagnosticsJson().contains(QString::number(first)),
            "source deletion did not immediately diagnose the dangling stable reference");
    app.selectFlow("flow_1");
    app.selectNode(targetId);
    pump();
    auto *unavailableChoice = findChoice();
    require(unavailableChoice && unavailableChoice->property("currentIndex").toInt() == -1
                && field->property("valueText").toString() == QString::number(first),
            "deleted reference was silently rebound to a different source");
    app.runOnce();
    idle();
    require(calls.seek(0) && calls.readAll().trimmed().split('\n').size() == 12,
            "a dangling receiver reference started source tools");
    app.selectFlow(QString::fromStdString(sourceFlow));
    app.addNode("test.identity.source", 3, 0);
    idle();
    const auto replacement = app.selectedNode();
    require(!replacement.isEmpty() && replacement != QString::number(first), "replacement source reused the deleted ID");
    require(app.setParameter("value", false, "41"), "replacement source value could not be configured");
    idle();
    app.renameNode(replacement, "已改名的来源");
    idle();
    app.selectFlow("flow_1");
    app.selectNode(targetId);
    app.runOnce();
    idle();
    require(calls.seek(0) && calls.readAll().trimmed().split('\n').size() == 12
                && field->property("valueText").toString() == QString::number(first),
            "same-name replacement guessed or executed the missing source relationship");
    require(app.setParameter("source_node_seq", true, replacement), "replacement source identity could not be selected");
    idle();
    run("41");
    require(calls.seek(0), "cannot read repaired reference execution evidence");
    lines = calls.readAll().trimmed().split('\n');
    require(lines.size() == 14 && lines.count("17") == 6 && lines.count("29") == 7 && lines.count("41") == 1,
            "source deletion/replacement did not preserve exact execution counts");
    app.setFlowSelected(QString::fromStdString(sourceFlow), false);
    idle();
    app.runOnce();
    idle();
    require(app.diagnosticsJson().contains("接收源未在本轮执行") && calls.seek(0)
                && calls.readAll().trimmed().split('\n').size() == 14,
            "an unselected source flow did not reject the receiver before waiting");
    app.setFlowSelected(QString::fromStdString(sourceFlow), true);
    idle();
    std::cout << "RECEIVER_REFERENCES_PASS renamed=17 deleted_execution=0 lookalike_execution=0 repaired=41"
              << " source_executions=14 replacement=" << replacement.toStdString() << " unselected_rejected=1\n";
}
catch (const std::exception &error)
{
    std::cerr << error.what() << '\n';
    return 1;
}
