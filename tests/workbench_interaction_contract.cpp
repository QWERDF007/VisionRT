// Real workbench contract for the runtime interaction rules (U02 / F01).
// While a run owns the workspace every user entry is unavailable except stop and close; the run
// itself keeps publishing results, and a save requested during a run neither writes nor queues.
#include "WorkbenchFixtures.h"
#include "core/DefinitionJson.h"
#include "ui/AppController.h"
#include "ui/ImageViewport.h"
#include "ui/Theme.h"
#include <quickui/Color.h>

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QGuiApplication>
#include <QQmlApplicationEngine>
#include <QQmlContext>
#include <QQuickItem>
#include <QQuickWindow>
#include <QScopeGuard>
#include <QSGRendererInterface>
#include <QTest>
#include <functional>
#include <iostream>
#include <stdexcept>

using namespace visionrt;

namespace {
void require(bool condition, const std::string &message)
{
    if (!condition)
        throw std::runtime_error(message);
}

QQuickItem *toolbarButton(QQuickWindow *window, const QStringList &texts)
{
    for (auto *item : window->findChildren<QQuickItem *>())
        if (item->isVisible() && texts.contains(item->property("text").toString()))
            return item;
    return nullptr;
}

bool waitFor(const std::function<bool()> &condition, int milliseconds = 10000)
{
    return QTest::qWaitFor(condition, milliseconds);
}
} // namespace

int main(int argc, char **argv)
try
{
    QGuiApplication application(argc, argv);
    registerStyleModule();
    QQuickWindow::setGraphicsApi(QSGRendererInterface::Software);
    testing::WorkbenchInstallation installation;
    const auto                     root       = installation.directory();
    const auto                     package    = root + "/tools-package/";
    const auto                     script     = package + "python/custom/interaction/tool.py";
    const auto                     directory  = QFileInfo(script).absolutePath();
    const auto                     permission = directory + "/allow";
    const auto                     entered    = directory + "/entered";
    const auto                     saveTarget = directory + "/interaction.vrt.json";
    const auto write = [](const QString &path, const QByteArray &bytes)
    {
        require(QDir().mkpath(QFileInfo(path).absolutePath()), "interaction fixture parent unavailable");
        QFile file(path);
        require(file.open(QIODevice::WriteOnly) && file.write(bytes) == bytes.size(),
                "interaction fixture write failed");
    };

    ToolDefinition gate;
    gate.id          = "test.interaction.gate";
    gate.displayName = "交互门控工具";
    gate.kind        = ToolKind::PythonFunction;
    gate.entry       = {"interaction", "tool.py", "run"};
    gate.outputs     = {
        {"value", "值", {Type::Int}}
    };
    write(package + "tools/custom/interaction/source.json", QByteArray::fromStdString(emitNormalizedToolJson(gate)));
    write(script, R"py(import time
from pathlib import Path

root = Path(__file__).parent

def run(inputs):
    (root / 'entered').write_text('1', encoding='utf-8')
    deadline = time.monotonic() + 30
    while not (root / 'allow').exists():
        if time.monotonic() >= deadline:
            raise RuntimeError('interaction gate deadline exceeded')
        time.sleep(0.002)
    return {'outputs': {'value': 1, '__status_code': 0, '__status_message': ''}, 'detail': ''}
)py");

    AppController app(2, root);
    const auto    releaseOnExit = qScopeGuard([&] { write(permission, "allow"); });
    require(waitFor([&] { return !app.busy(); }, 5000), "interaction catalog did not become ready");
    require(app.workspaceEnabled() && !app.stopEnabled(), "idle workspace was not interactive");

    qmlRegisterType<ImageViewport>("VisionRT.View", 1, 0, "ImageViewport");
    QQmlApplicationEngine engine;
    quickui::QuiColor::getInstance()->setThemeMode(quickui::QuiColor::Light);
    engine.addImportPath(QStringLiteral(VISIONRT_BINARY_DIR "/qml"));
    engine.rootContext()->setContextProperty("app", &app);
    engine.rootContext()->setContextProperty("demoMenu", false);
    engine.rootContext()->setContextProperty("demoSelect", false);
    engine.load(QUrl::fromLocalFile(QStringLiteral(VISIONRT_SOURCE_DIR "/src/ui/qml/Main.qml")));
    require(!engine.rootObjects().isEmpty(), "interaction Main.qml did not load");
    auto *window = qobject_cast<QQuickWindow *>(engine.rootObjects().first());
    require(window && QTest::qWaitForWindowExposed(window), "interaction window was not exposed");

    app.addNode(QStringLiteral("test.interaction.gate"), 0, 0);
    require(waitFor([&] { return !app.busy() && app.nodes().size() == 1; }),
            "gate node was not added: nodes=" + std::to_string(app.nodes().size())
                + " diagnostic=" + app.diagnostic().toStdString());
    const auto node = app.selectedNode();
    require(!node.isEmpty(), "gate node was not selected");
    require(QFile::remove(permission) || !QFileInfo::exists(permission), "stale permission file");

    const auto waitEntered = [&]
    {
        return waitFor([&] { return QFileInfo::exists(entered); }, 10000);
    };
    const auto release = [&] { write(permission, "allow"); };
    const auto removeEntered = [&] { QFile::remove(entered); };

    // Single run: stop is the only enabled entry, and a save neither writes nor queues.
    removeEntered();
    app.runOnce();
    require(waitFor([&] { return app.runState() == "RunningOnce"; }, 5000), "single run did not start");
    require(waitEntered(), "single run did not reach the tool");
    require(!app.workspaceEnabled() && app.stopEnabled(), "single run did not lock the workspace");
    require(app.selectedNode() == node, "selection changed during a single run");
    app.selectNode(QString{});
    require(app.selectedNode() == node, "a late selection click moved the running selection");
    const auto nodesBefore = app.nodes().size();
    app.addNode(QStringLiteral("test.interaction.gate"), 2, 0);
    require(app.nodes().size() == nodesBefore, "a node was added during a run");
    require(!app.save(saveTarget), "a save was accepted during a run");
    require(!QFileInfo::exists(saveTarget), "a save wrote the project during a run");
    auto *stopButton = toolbarButton(window, {QStringLiteral("停止运行")});
    require(stopButton && stopButton->isEnabled(), "single run has no enabled stop entry");
    app.stop();
    require(waitFor([&] { return app.runState() == "Stopping"; }, 2000), "single run did not stop");
    require(!app.workspaceEnabled() && !app.stopEnabled(), "stopping kept the workspace interactive");
    require(toolbarButton(window, {QStringLiteral("等待本轮结束")}), "stopping state has no visible entry");
    release();
    require(waitFor([&] { return !app.busy(); }), "stopped single run did not become idle");
    require(app.workspaceEnabled() && !app.stopEnabled(), "single run did not restore the workspace");
    require(!QFileInfo::exists(saveTarget), "a refused save was written after the run finished");
    require(app.save(saveTarget), "idle save failed after the run");
    require(QFileInfo::exists(saveTarget), "idle save did not write the project");
    require(waitFor([&] { return !app.busy(); }), "saved project did not settle");

    // Node run: the same stop entry covers the single-node mode.
    QFile::remove(permission);
    removeEntered();
    app.runNode();
    require(waitFor([&] { return app.runState() == "RunningNode"; }, 5000), "node run did not start");
    require(waitEntered(), "node run did not reach the tool");
    require(!app.workspaceEnabled() && app.stopEnabled(), "node run did not lock the workspace");
    app.stop();
    require(waitFor([&] { return app.runState() == "Stopping"; }, 2000), "node run did not stop");
    release();
    require(waitFor([&] { return !app.busy(); }), "stopped node run did not become idle");

    // Loop run: the loop entry turns into the same stop entry.
    QFile::remove(permission);
    removeEntered();
    app.toggleLoop();
    require(waitFor([&] { return app.runState() == "RunningLoop"; }, 5000), "loop run did not start");
    require(waitEntered(), "loop run did not reach the tool");
    auto *loopStop = toolbarButton(window, {QStringLiteral("停止循环")});
    require(loopStop && loopStop->isEnabled(), "loop run has no enabled stop entry");
    app.toggleLoop();
    require(waitFor([&] { return app.runState() == "Stopping"; }, 2000), "loop run did not stop");
    release();
    require(waitFor([&] { return !app.busy(); }), "stopped loop run did not become idle");
    require(app.workspaceEnabled(), "loop run did not restore the workspace");

    QFile::remove(permission);
    std::cout << "INTERACTION_PASS single=stop node=stop loop=stop save=refused\n";
    return 0;
}
catch (const std::exception &error)
{
    std::cerr << "[Interaction error] " << error.what() << '\n';
    return 1;
}
