#include "WorkbenchFixtures.h"
#include "core/DefinitionJson.h"
#include "ui/AppController.h"
#include "ui/Theme.h"
#include <quickui/Color.h>

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QGuiApplication>
#include <QQmlComponent>
#include <QQmlContext>
#include <QQmlEngine>
#include <QQuickItem>
#include <QQuickWindow>
#include <QSGRendererInterface>
#include <QScopeGuard>
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

QQuickItem *findItem(QQuickItem *parent, const QString &name)
{
    for (auto *child : parent->childItems())
    {
        if (child->objectName() == name)
            return child;
        if (auto *found = findItem(child, name))
            return found;
    }
    return nullptr;
}
}

int main(int argc, char **argv)
try
{
    QGuiApplication application(argc, argv);
    registerStyleModule();
    QQuickWindow::setGraphicsApi(QSGRendererInterface::Software);
    testing::WorkbenchInstallation installation;
    const auto                     root  = installation.directory();
    const auto                     write = [&](const QString &path, const QByteArray &bytes)
    {
        require(QDir().mkpath(QFileInfo(path).absolutePath()), "status fixture parent unavailable");
        QFile file(path);
        require(file.open(QIODevice::WriteOnly) && file.write(bytes) == bytes.size(), "status fixture write failed");
    };
    const auto package    = root + "/tools-package/";
    const auto script     = package + "python/custom/status/tool.py";
    const auto permission = QFileInfo(script).absolutePath() + "/allow";
    ToolDefinition definition;
    definition.id          = "test.status";
    definition.displayName = "状态工具";
    definition.kind        = ToolKind::PythonFunction;
    definition.entry       = {"status", "tool.py", "run"};
    definition.inputs      = {
        { "mode", "模式", {Type::String}, std::string("success")}
    };
    definition.outputs = {
        {  "value",  "值", {Type::Int}},
        { "flag", "标志", {Type::Bool}}
    };
    write(package + "tools/custom/status/source.json", QByteArray::fromStdString(emitNormalizedToolJson(definition)));
    write(script, R"py(import time
from pathlib import Path

root = Path(__file__).parent

def run(inputs):
    mode = inputs.get('mode', 'success')
    if mode == 'wait':
        (root / 'entered').write_text('1', encoding='utf-8')
        while not (root / 'allow').exists():
            time.sleep(0.002)
    if mode == 'fail':
        return {'status': {'code': 1, 'message': '受控失败', 'detail': ''},
                'outputs': {'value': None, 'flag': None}}
    return {'status': {'code': 0, 'message': '', 'detail': ''},
            'outputs': {'value': 7, 'flag': mode != 'gate_false'}}
)py");
    AppController app(2, root);
    const auto    releaseOnExit = qScopeGuard([&] { write(permission, "allow"); });
    require(QTest::qWaitFor([&] { return !app.busy(); }, 5000), "status catalog did not become ready");
    QQmlEngine engine;
    quickui::QuiColor::getInstance()->setThemeMode(quickui::QuiColor::Light);
    engine.addImportPath(QStringLiteral(VISIONRT_BINARY_DIR "/qml"));
    engine.rootContext()->setContextProperty("app", &app);
    QQmlComponent component(&engine,
                            QUrl::fromLocalFile(QStringLiteral(VISIONRT_SOURCE_DIR "/src/ui/qml/FlowCanvas.qml")));
    std::unique_ptr<QObject> owner(component.create());
    if (!owner)
        throw std::runtime_error(component.errorString().toStdString());
    auto *canvas = qobject_cast<QQuickItem *>(owner.get());
    require(canvas, "status canvas root is not an item");
    QQuickWindow window;
    window.resize(1100, 760);
    canvas->setParentItem(window.contentItem());
    canvas->setSize(QSizeF(1100, 760));
    window.show();
    require(QTest::qWaitForWindowExposed(&window), "status window was not exposed");
    window.requestActivate();
    require(QTest::qWaitForWindowActive(&window), "status window was not activated");
    const auto idle = [&]
    {
        require(QTest::qWaitFor([&] { return !app.busy(); }, 10000), "status operation did not finish");
    };
    const auto stateOf = [&](const QString &id)
    {
        for (const auto &value : app.nodes())
        {
            const auto row = value.toMap();
            if (row.value("id").toString() == id)
                return row;
        }
        return QVariantMap{};
    };
    const auto requireColor = [&](QQuickItem *item, const char *property, const QColor &expected, const char *message)
    {
        require(item && item->property(property).value<QColor>() == expected, message);
    };
    const auto sourceFlow = app.currentFlow();
    app.addNode("test.status", 0, 0);
    idle();
    const auto source = app.selectedNode();
    require(!source.isEmpty(), "status source node is missing");
    app.addNode("test.status", 4, 0);
    idle();
    const auto neighbour = app.selectedNode();
    require(!neighbour.isEmpty() && neighbour != source, "status neighbour node is missing");
    app.selectNode(source);
    require(app.setParameter("mode", false, "wait"), "status source could not enter wait mode");
    idle();
    app.runNode();
    require(QTest::qWaitFor([&] { return stateOf(source).value("state").toString() == "running"; }, 5000),
            "status source did not enter running state");
    auto *sourceItem = findItem(canvas, "flowNode_" + source);
    require(sourceItem, "status source delegate is missing");
    require(QTest::qWaitFor([&] { return sourceItem->property("nodeState").toString() == "running"; }, 2000),
            "status source delegate did not reach running state");
    const auto *theme = Theme::instance();
    requireColor(sourceItem, "statusDotColor", theme->nodeStateGreen(), "running status dot is not green");
    requireColor(sourceItem, "headerBg", theme->nodeHeaderGreen(), "running header is not green");
    requireColor(sourceItem, "headerText", theme->nodeHeaderTextGreen(), "running header text is not green");
    app.selectNode(neighbour);
    require(QTest::qWaitFor([&] { return !sourceItem->property("selected").toBool(); }, 2000),
            "selection independence failed for the running node");
    requireColor(sourceItem, "statusDotColor", theme->nodeStateGreen(), "selection changed the running color");
    auto *neighbourItem = findItem(canvas, "flowNode_" + neighbour);
    require(neighbourItem && neighbourItem->property("selected").toBool(), "neighbour node was not selected");
    write(permission, "allow");
    idle();
    require(stateOf(source).value("state").toString() == "succeeded", "status source did not succeed");
    requireColor(sourceItem, "statusDotColor", theme->nodeStateGray(), "succeeded status dot is not neutral");
    requireColor(sourceItem, "headerBg", theme->nodeHeaderGray(), "succeeded header is not neutral");
    require(!stateOf(source).value("timingText").toString().isEmpty()
                && stateOf(source).value("timingText").toString() != QStringLiteral("运行中"),
            "succeeded node has no timing");
    require(QFile::remove(permission), "status permission file could not be removed");
    app.selectNode(source);
    require(app.setParameter("mode", false, "fail"), "status source could not enter failure mode");
    idle();
    app.runNode();
    idle();
    require(stateOf(source).value("state").toString() == "failed", "status source did not fail");
    require(stateOf(source).value("statusMessage").toString().contains("受控失败"), "failure message is missing");
    requireColor(sourceItem, "statusDotColor", theme->nodeStateRed(), "failed status dot is not red");
    requireColor(sourceItem, "headerBg", theme->nodeHeaderRed(), "failed header is not red");
    requireColor(sourceItem, "headerText", theme->nodeHeaderTextRed(), "failed header text is not red");
    require(app.setParameter("mode", false, "wait"), "status source could not return to wait mode");
    idle();
    app.addFlow("接收流程");
    idle();
    const auto receiverFlow = app.currentFlow();
    require(receiverFlow != sourceFlow, "status receiver flow was not created");
    app.addNode("visionrt.flow.receive", 0, 0);
    idle();
    const auto receiver = app.selectedNode();
    require(!receiver.isEmpty(), "status receiver node is missing");
    require(app.setParameter("source_flow_id", true, sourceFlow), "receiver source flow could not be set");
    idle();
    require(app.setParameter("source_node_seq", true, source), "receiver source node could not be set");
    idle();
    require(app.setParameter("source_port_id", true, "value"), "receiver source port could not be set");
    idle();
    require(app.setParameter("timeout_ms", true, "10000"), "receiver timeout could not be set");
    idle();
    idle();
    app.selectFlow(receiverFlow);
    app.selectNode(receiver);
    idle();
    app.toggleLoop();
    require(QTest::qWaitFor([&] { return stateOf(receiver).value("state").toString() == "running"; }, 5000),
            "status receiver did not enter its waiting state");
    auto *receiverItem = findItem(canvas, "flowNode_" + receiver);
    require(receiverItem, "status receiver delegate is missing");
    requireColor(receiverItem, "statusDotColor", theme->nodeStateGreen(), "waiting receiver is not green");
    app.toggleLoop();
    write(permission, "allow");
    idle();
    require(stateOf(receiver).value("state").toString() == "cancelled", "status receiver was not cancelled");
    require(stateOf(receiver).value("statusCode").toInt() == -5
                && stateOf(receiver).value("statusMessage").toString().contains("运行已取消"),
            "cancelled receiver lost its message");
    requireColor(receiverItem, "statusDotColor", theme->nodeStateGray(), "cancelled status dot is not neutral");
    requireColor(receiverItem, "headerBg", theme->nodeHeaderGray(), "cancelled header is not neutral");

    // Gated nodes never ran the algorithm: they must show 跳过/阻塞, never a green success and
    // never a fabricated zero-millisecond algorithm time.
    app.addFlow("门控流程");
    idle();
    app.addNode("test.status", 0, 0);
    idle();
    const auto gate = app.selectedNode();
    require(!gate.isEmpty(), "gate node is missing");
    require(app.setParameter("mode", false, "gate_false"), "gate node could not produce a false flag");
    idle();
    // Execution conditions belong to the logical node models only.
    app.addNode("visiontools.logic.expression", 4, 8);
    idle();
    const auto gated = app.selectedNode();
    require(!gated.isEmpty() && app.applyExecutionGuard(gated, gate, "flag"), "gate guard could not be applied");
    idle();
    app.runOnce();
    idle();
    auto gatedRow = stateOf(gated);
    require(gatedRow.value("state").toString() == "skipped", "false branch must be reported as skipped");
    require(gatedRow.value("disposition").toString() == "skipped", "skipped disposition is missing");
    require(gatedRow.value("timingText").toString().isEmpty()
                && gatedRow.value("timingPhases").toList().isEmpty(),
            "a skipped node must not report algorithm time");
    require(!gatedRow.value("executed").toBool(), "a skipped node must not be marked as executed");
    auto *gatedItem = findItem(canvas, "flowNode_" + gated);
    require(gatedItem, "gated delegate is missing");
    require(gatedItem->property("nodeState").toString() == "skipped", "gated delegate did not reach skipped state");
    requireColor(gatedItem, "statusDotColor", theme->nodeStateGray(), "skipped status dot is not neutral");
    auto *skipBadge = findItem(gatedItem, "nodeBadgeSkip_" + gated);
    auto *blockBadge = findItem(gatedItem, "nodeBadgeBlock_" + gated);
    require(skipBadge && skipBadge->isVisible(), "skipped badge is missing");
    require(blockBadge && !blockBadge->isVisible(), "skipped node must not show a blocked badge");
    require(gatedItem->property("statusDotColor").value<QColor>() != theme->nodeStateGreen(),
            "skipped node must not borrow the running green");

    // A failing upstream blocks the guarded node instead of running it.
    app.selectNode(gate);
    require(app.setParameter("mode", false, "fail"), "gate node could not enter failure mode");
    idle();
    app.runOnce();
    idle();
    gatedRow = stateOf(gated);
    require(gatedRow.value("state").toString() == "blocked", "failed upstream must block the guarded node");
    require(gatedRow.value("disposition").toString() == "blocked", "blocked disposition is missing");
    require(gatedRow.value("timingText").toString().isEmpty()
                && gatedRow.value("timingPhases").toList().isEmpty(),
            "a blocked node must not report algorithm time");
    require(gatedRow.value("statusMessage").toString().contains(QStringLiteral("未执行")),
            "blocked reason must stay separate from tool errors");
    requireColor(gatedItem, "statusDotColor", theme->nodeStateOrange(), "blocked status dot is not orange");
    require(blockBadge && blockBadge->isVisible(), "blocked badge is missing");
    require(skipBadge && !skipBadge->isVisible(), "blocked node must not show a skipped badge");

    std::cout << "WORKBENCH_STATUS_PASS running=green succeeded=neutral failed=red cancelled=neutral "
                 "skipped=neutral blocked=orange timing=1\n";
    window.hide();
    return 0;
}
catch (const std::exception &error)
{
    std::cerr << error.what() << '\n';
    return 1;
}
