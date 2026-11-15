#include "TestPythonConfiguration.h"
#include "WorkbenchFixtures.h"
#include "core/DefinitionJson.h"
#include "core/NodeContracts.h"
#include "ui/AppController.h"
#include "ui/Theme.h"
#include "ui/OutputModel.h"
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

// 夹具声明的端口编号。
constexpr PortId kModeInput         = 1;
constexpr PortId kFlagOutput        = 3;
constexpr PortId kSourceValueOutput = 2;

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
    // R50: the host runs Python only on an interpreter someone selected.
    testsupport::useBuildInterpreter();
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
    definition.nameEn          = "test.status";
    definition.nameZh = "状态工具";
    definition.kind        = ToolKind::PythonFunction;
    definition.entry       = {"status", "tool.py", "run"};
    definition.inputs      = {
        {1, "模式", {Type::String}, std::string("success")}
    };
    definition.outputs = {
        {2, "值", {Type::Int}},
        {3, "标志", {Type::Bool}}
    };
    write(package + "tools/custom/status/source.json", QByteArray::fromStdString(emitNormalizedToolJson(definition)));
    write(script, R"py(import time
from pathlib import Path

root = Path(__file__).parent

def run(inputs):
    mode = inputs.get(1, 'success')
    if mode == 'wait':
        (root / 'entered').write_text('1', encoding='utf-8')
        while not (root / 'allow').exists():
            time.sleep(0.002)
    if mode == 'fail':
        return {'outputs': {2: None, 3: None, 0: 1, 1: '受控失败'},
                'detail': ''}
    return {'outputs': {2: 7, 3: mode != 'gate_false', 0: 0, 1: ''},
            'detail': ''}
)py");
    ToolDefinition initFailure = definition;
    initFailure.nameEn = "test.init_failure";
    initFailure.nameZh = "初始化测试";
    initFailure.kind = ToolKind::PythonClass;
    initFailure.entry.symbol = "InitFailure";
    write(package + "tools/custom/status/init.json", QByteArray::fromStdString(emitNormalizedToolJson(initFailure)));
    {
        QFile file(script);
        require(file.open(QIODevice::Append), "initialization fixture is not writable");
        file.write(R"py(
class InitFailure:
    def initialize(self, parameters):
        return {'code': 17, 'message': '模型路径未填写', 'detail': ''}
    def execute(self, inputs):
        raise AssertionError('execute must not run after initialization fails')
    def release(self):
        return {'code': 0, 'message': '', 'detail': ''}
)py");
    }
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
    app.renameNode(source, "自定义节点名");
    idle();
    require(stateOf(source).value("toolName").toString() == "状态工具", "rename changed the original tool name");
    require(stateOf(source).value("title").toString() == "自定义节点名-" + source, "node alias was not applied");
    auto *nameItem = findItem(canvas, "nodeToolName_" + source);
    require(nameItem && nameItem->property("text").toString() == "状态工具", "card header lost the original tool name");

    app.addNode("test.status", 4, 0);
    idle();
    const auto neighbour = app.selectedNode();
    require(!neighbour.isEmpty() && neighbour != source, "status neighbour node is missing");
    app.selectNode(source);
    require(app.setParameter(kModeInput, false, "wait"), "status source could not enter wait mode");
    idle();
    app.runNode();
    require(QTest::qWaitFor([&] { return stateOf(source).value("state").toString() == "running"; }, 5000),
            "status source did not enter running state");
    auto *sourceItem = findItem(canvas, "flowNode_" + source);
    require(sourceItem, "status source delegate is missing");
    require(QTest::qWaitFor([&] { return sourceItem->property("nodeState").toString() == "running"; }, 2000),
            "status source delegate did not reach running state");
    const auto *theme = themeForEngine(&engine);
    // The status border is its own layer: a selected node keeps it, because selection is drawn as a
    // separate dashed outline and must never hide that the node is running.
    const auto borderOf = [](QQuickItem *item)
    {
        return item ? item->property("border").value<QObject *>() : nullptr;
    };
    const auto requireRunningBorder = [&](QQuickItem *item, const char *message)
    {
        auto *pen = borderOf(item);
        require(pen && pen->property("width").toDouble() == 2
                    && pen->property("color").value<QColor>() == theme->nodeStateGreen(),
                message);
    };
    const auto requireIdleBorder = [&](QQuickItem *item, const char *message)
    {
        auto *pen = borderOf(item);
        require(pen && pen->property("width").toDouble() == 1
                    && pen->property("color").value<QColor>() != theme->nodeStateGreen(),
                message);
    };
    requireColor(sourceItem, "headerBg", theme->nodeHeaderGreen(), "running header is not green");
    requireColor(sourceItem, "headerText", theme->nodeHeaderTextGreen(), "running header text is not green");
    // A run owns the workspace: the selection cannot move while it is active, so the running node
    // keeps its own selection and its status colours.
    app.selectNode(neighbour);
    require(app.selectedNode() == source, "selection changed while a run owned the workspace");
    require(sourceItem->property("selected").toBool(), "running node lost its selection");
    requireRunningBorder(sourceItem, "a selected running node lost its 2px status border");
    write(permission, "allow");
    idle();
    require(stateOf(source).value("state").toString() == "succeeded", "status source did not succeed");
    requireColor(sourceItem, "headerBg", theme->nodeHeaderGray(), "succeeded header is not neutral");
    // The border belongs to the status layer, so it is checked again once selection moved away and
    // the card shows its own neutral outline.
    app.selectNode(neighbour);
    auto *neighbourItem = findItem(canvas, "flowNode_" + neighbour);
    require(neighbourItem && neighbourItem->property("selected").toBool(), "neighbour node was not selected");
    requireIdleBorder(sourceItem, "a finished node did not return to its neutral border");
    require(!stateOf(source).value("timingText").toString().isEmpty()
                && stateOf(source).value("timingText").toString() != QStringLiteral("运行中"),
            "succeeded node has no timing");
    require(QFile::remove(permission), "status permission file could not be removed");
    app.selectNode(source);
    require(app.setParameter(kModeInput, false, "fail"), "status source could not enter failure mode");
    idle();
    app.runNode();
    idle();
    require(stateOf(source).value("state").toString() == "failed", "status source did not fail");
    require(stateOf(source).value("statusMessage").toString().contains("受控失败"), "failure message is missing");
    requireColor(sourceItem, "headerBg", theme->nodeHeaderRed(), "failed header is not red");
    requireColor(sourceItem, "headerText", theme->nodeHeaderTextRed(), "failed header text is not red");
    require(app.setParameter(kModeInput, false, "wait"), "status source could not return to wait mode");
    idle();
    app.addFlow("接收流程");
    idle();
    const auto receiverFlow = app.currentFlow();
    require(receiverFlow != sourceFlow, "status receiver flow was not created");
    app.addNode("ReceiveFlowResult", 0, 0);
    idle();
    const auto receiver = app.selectedNode();
    require(!receiver.isEmpty(), "status receiver node is missing");
    require(app.setParameter(ReceiveInitPorts::SourceFlow, true, sourceFlow), "receiver source flow could not be set");
    idle();
    require(app.setParameter(ReceiveInitPorts::SourceNode, true, source), "receiver source node could not be set");
    idle();
    require(app.setParameter(ReceiveInitPorts::SourcePort, true, QString::number(kSourceValueOutput)), "receiver source port could not be set");
    idle();
    require(app.setParameter(ReceiveInitPorts::Timeout, true, "10000"), "receiver timeout could not be set");
    idle();
    idle();
    app.selectFlow(receiverFlow);
    app.selectNode(receiver);
    idle();
    app.toggleLoop();
    if (!QTest::qWaitFor([&] { return stateOf(receiver).value("state").toString() == "running"; }, 5000))
        throw std::runtime_error("status receiver did not enter its waiting state: "
                                 + stateOf(receiver).value("state").toString().toStdString()
                                 + " diag=" + app.diagnostic().toStdString());
    auto *receiverItem = findItem(canvas, "flowNode_" + receiver);
    require(receiverItem, "status receiver delegate is missing");
    app.toggleLoop();
    write(permission, "allow");
    idle();
    require(stateOf(receiver).value("state").toString() == "cancelled", "status receiver was not cancelled");
    require(stateOf(receiver).value("statusCode").toInt() == -5
                && stateOf(receiver).value("statusMessage").toString().contains("运行已取消"),
            "cancelled receiver lost its message");
    requireColor(receiverItem, "headerBg", theme->nodeHeaderGray(), "cancelled header is not neutral");

    // Gated nodes never ran the algorithm: they must show 跳过/阻塞, never a green success and
    // never a fabricated zero-millisecond algorithm time.
    app.addFlow("门控流程");
    idle();
    app.addNode("test.status", 0, 0);
    idle();
    const auto gate = app.selectedNode();
    require(!gate.isEmpty(), "gate node is missing");
    require(app.setParameter(kModeInput, false, "gate_false"), "gate node could not produce a false flag");
    idle();
    // Execution conditions belong to the logical node models only.
    app.addNode("EvaluateExpression", 4, 8);
    idle();
    const auto gated = app.selectedNode();
    require(!gated.isEmpty() && app.applyExecutionGuard(gated, gate, kFlagOutput), "gate guard could not be applied");
    idle();
    app.runOnce();
    idle();
    auto gatedRow = stateOf(gated);
    require(gatedRow.value("state").toString() == "skipped", "false branch must be reported as skipped");
    require(gatedRow.value("disposition").toString() == "skipped", "skipped disposition is missing");
    require(gatedRow.value("timingText").toString().isEmpty()
                && gatedRow.value("timingPhases").toList().size() == 3,
            "a skipped node must not report algorithm time");
    require(!gatedRow.value("executed").toBool(), "a skipped node must not be marked as executed");
    auto *gatedItem = findItem(canvas, "flowNode_" + gated);
    require(gatedItem, "gated delegate is missing");
    require(gatedItem->property("nodeState").toString() == "skipped", "gated delegate did not reach skipped state");
    auto *skipBadge = findItem(gatedItem, "nodeBadgeSkip_" + gated);
    auto *blockBadge = findItem(gatedItem, "nodeBadgeBlock_" + gated);
    require(!skipBadge, "skipped badge must not appear on the card");
    require(!blockBadge, "blocked badge must not appear on the card");


    // A failing upstream blocks the guarded node instead of running it.
    app.selectNode(gate);
    require(app.setParameter(kModeInput, false, "fail"), "gate node could not enter failure mode");
    idle();
    app.runOnce();
    idle();
    gatedRow = stateOf(gated);
    require(gatedRow.value("state").toString() == "blocked", "failed upstream must block the guarded node");
    require(gatedRow.value("disposition").toString() == "blocked", "blocked disposition is missing");
    require(gatedRow.value("timingText").toString().isEmpty()
                && gatedRow.value("timingPhases").toList().size() == 3,
            "a blocked node must not report algorithm time");
    require(gatedRow.value("statusMessage").toString().contains(QStringLiteral("阻塞")),
            "blocked reason must stay separate from tool errors");
    require(!blockBadge, "blocked badge must not appear on the card");
    require(!skipBadge, "skipped badge must not appear on the card");

    std::cout << "WORKBENCH_STATUS_PASS running=green succeeded=neutral failed=red cancelled=neutral "
                 "skipped=neutral blocked=orange timing=1\n";
    app.selectFlow(sourceFlow);
    app.addNode("test.init_failure", 8, 0);
    idle();
    const auto failedInit = app.selectedNode();
    require(!failedInit.isEmpty(), "initialization fixture node is missing");
    app.runNode();
    idle();
    const auto failedInitRow = stateOf(failedInit);
    require(!failedInitRow.value("implUnavailable").toBool(), "initialization failure was marked missing implementation");
    auto *outputs = qobject_cast<OutputModel *>(app.outputModel());
    require(outputs && std::get<std::int64_t>(outputs->capture(0)->value) == 17,
            "initialization status is absent from output rows");
    require(std::get<std::string>(outputs->capture(1)->value) == "模型路径未填写",
            "initialization message is absent from output rows");
    require(failedInitRow.value("timingPhases").toList().size() == 3, "initialization failure hid the API timing row");
    QMetaObject::invokeMethod(canvas, "centerNode", Q_ARG(QVariant, failedInit));
    QTest::qWait(50);
    auto *failedItem = findItem(canvas, "flowNode_" + failedInit);
    require(failedItem, "initialization failure card is missing");
    QTest::mouseMove(&window, failedItem->mapToScene(QPointF(failedItem->width() / 2, failedItem->height() / 2)).toPoint());
    require(QTest::qWaitFor([&] { return canvas->property("hoverCardVisible").toBool(); }, 2000),
            "initialization failure tooltip did not open");
    auto *tooltip = findItem(canvas, "nodeHoverCard");
    QStringList tooltipText;
    const auto collectText = [&](auto &&self, QQuickItem *parent) -> void {
        for (auto *child : parent->childItems())
        {
            if (child->isVisible() && child->property("text").isValid())
                tooltipText << child->property("text").toString();
            self(self, child);
        }
    };
    collectText(collectText, tooltip);
    require(tooltipText.size() == 6 && tooltipText.at(0) == "预处理" && tooltipText.at(2) == "API"
                && tooltipText.at(4) == "后处理" && tooltipText.at(3) == "0.000 ms"
                && tooltipText.at(5) == "0.000 ms", "tooltip must contain exactly three timing rows");
    QDir().mkpath(QStringLiteral(VISIONRT_BINARY_DIR "/evidence"));
    require(window.grabWindow().save(QStringLiteral(VISIONRT_BINARY_DIR "/evidence/node-status-card.png")),
            "status screenshot could not be saved");
    window.hide();
    return 0;
}
catch (const std::exception &error)
{
    std::cerr << error.what() << '\n';
    return 1;
}
