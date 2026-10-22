#include "WorkbenchFixtures.h"
#include "core/DefinitionJson.h"
#include "core/ProjectEditor.h"
#include "core/ProjectStorage.h"
#include "core/ToolRegistry.h"
#include "core/ToolPackage.h"
#include "ui/AppController.h"
#include "ui/ImageViewport.h"
#include "ui/Theme.h"

#include <QDir>
#include <QElapsedTimer>
#include <QFile>
#include <QFileInfo>
#include <QQmlApplicationEngine>
#include <QQmlContext>
#include <QQuickItem>
#include <QQuickWindow>
#include <QScopeGuard>
#include <QSignalSpy>
#include <QTest>
#include <iostream>
#include <stdexcept>

void cancellationWindowContract()
{
    using namespace visionrt;
    const auto require = [](bool condition, const std::string &message)
    {
        if (!condition)
            throw std::runtime_error(message);
    };
    testing::WorkbenchInstallation installation;
    const auto                     root       = installation.directory();
    const auto                     script     = root + "/tools-package/python/custom/cancellation/tool.py";
    const auto                     log        = QFileInfo(script).absolutePath() + "/events";
    const auto                     permission = QFileInfo(script).absolutePath() + "/allow";
    const auto                     write      = [&](const QString &path, const QByteArray &bytes)
    {
        require(QDir().mkpath(QFileInfo(path).absolutePath()), "Cannot create cancellation fixture directory");
        QFile file(path);
        require(file.open(QIODevice::WriteOnly) && file.write(bytes) == bytes.size(),
                "Cannot write cancellation fixture");
    };
    const auto events = [&]
    {
        QFile file(log);
        auto  result
            = file.open(QIODevice::ReadOnly | QIODevice::Text) ? file.readAll().split('\n') : QList<QByteArray>{};
        result.removeAll(QByteArray{});
        return result;
    };
    ToolDefinition gate;
    gate.id          = "test.cancellation.gate";
    gate.displayName = "Uninterruptible Python gate";
    gate.kind        = ToolKind::PythonClass;
    gate.entry       = {"cancellation", "tool.py", "Gate"};
    gate.outputs     = {
        {"value", "Value", {Type::Int}}
    };
    auto tail         = gate;
    tail.id           = "test.cancellation.tail";
    tail.kind         = ToolKind::PythonFunction;
    tail.entry.symbol = "tail";
    tail.inputs       = gate.outputs;
    write(root + "/tools-package/tools/custom/gate.json", QByteArray::fromStdString(emitNormalizedToolJson(gate)));
    write(root + "/tools-package/tools/custom/tail.json", QByteArray::fromStdString(emitNormalizedToolJson(tail)));
    write(script, R"(
from pathlib import Path
import time
root = Path(__file__).parent

def record(event):
    with (root / 'events').open('a', encoding='utf-8') as stream:
        stream.write(event + '\n')

class Gate:
    def __init__(self):
        self.calls = 0
        record('construct')
    def initialize(self, inputs):
        record('initialize')
        return {'__status_code': 0, '__status_message': ''}
    def execute(self, inputs):
        self.calls += 1
        record('execute:' + str(self.calls))
        deadline = time.monotonic() + 10
        while not (root / 'allow').exists():
            if time.monotonic() >= deadline:
                raise RuntimeError('Cancellation fixture gate deadline exceeded')
            time.sleep(0.001)
        record('return:' + str(self.calls))
        return {'value': self.calls, '__status_code': 0, '__status_message': ''}
    def release(self):
        record('release')
        return {'__status_code': 0, '__status_message': ''}
    def __del__(self):
        record('destroy')

def tail(inputs):
    record('tail')
    return {'value': inputs['value'], '__status_code': 0, '__status_message': ''}
)");
    ToolRegistry definitions;
    definitions.registerPackage(readToolsPackage(std::filesystem::path(root.toStdU16String()) / "tools-package"));
    ProjectEditor editor;
    const auto    flow   = editor.project().flows.front().id;
    const auto    parent = editor.addNode(flow, *definitions.find(gate.id), {0, 0});
    const auto    child  = editor.addNode(flow, *definitions.find(tail.id), {2, 0});
    editor.bindFreeInput(flow, {parent, "value"}, {child, "value"});
    auto project = editor.project();
    project.path = std::filesystem::path(root.toStdU16String()) / "cancellation.vrt.json";
    require(ProjectStorage(definitions).save(project).empty(), "Cannot save cancellation project");
    qmlRegisterType<ImageViewport>("VisionRT.View", 1, 0, "ImageViewport");
    qmlRegisterSingletonInstance<Theme>("VisionRT", 1, 0, "Theme", Theme::instance());
    {
        AppController app(2, root);
        require(QTest::qWaitFor([&] { return !app.busy(); }, 5000), "Cancellation catalog did not become ready");
        app.requestOpen(QString::fromStdU16String(project.path.u16string()));
        app.resolveUnsaved(QStringLiteral("discard"));
        require(QTest::qWaitFor([&] { return !app.busy() && app.nodes().size() == 2; }, 5000),
                "Cancellation project did not open: " + app.diagnostic().toStdString());
        require(!app.dirty(), "Saved cancellation project became dirty after loading");
        QQmlApplicationEngine engine;
        engine.addImportPath(QStringLiteral(VISIONRT_BINARY_DIR "/qml"));
        engine.rootContext()->setContextProperty("app", &app);
        engine.rootContext()->setContextProperty("Theme", Theme::instance());
        engine.rootContext()->setContextProperty("demoMenu", false);
        engine.rootContext()->setContextProperty("demoSelect", false);
        engine.load(QUrl::fromLocalFile(QStringLiteral(VISIONRT_SOURCE_DIR "/src/ui/qml/Main.qml")));
        require(!engine.rootObjects().isEmpty(), "Cancellation Main.qml did not load");
        auto *window = qobject_cast<QQuickWindow *>(engine.rootObjects().first());
        require(window && QTest::qWaitForWindowExposed(window), "Cancellation window was not exposed");
        const auto  releaseOnExit = qScopeGuard([&] { write(permission, "allow"); });
        QQuickItem *loop          = nullptr;
        for (auto *item : window->findChildren<QQuickItem *>())
            if (item->isVisible() && item->isEnabled() && item->property("text").toString() == "循环")
            {
                require(!loop, "Multiple visible loop buttons found");
                loop = item;
            }
        require(loop, "Actual loop toolbar button was not found");
        const auto clickLoop = [&]
        {
            const auto center = loop->mapToScene(QPointF(loop->width() / 2, loop->height() / 2)).toPoint();
            std::cout << "[CANCELLATION] click class=" << loop->metaObject()->className() << " at=" << center.x() << ","
                      << center.y() << " size=" << loop->width() << "," << loop->height()
                      << " state=" << app.runState().toStdString() << std::endl;
            QTest::mouseClick(window, Qt::LeftButton, Qt::NoModifier, center, 0);
        };
        QElapsedTimer stopTimer;
        qint64        signalMilliseconds = -1;
        QObject::connect(&app, &AppController::runtimeChanged, &app,
                         [&]
                         {
                             if (stopTimer.isValid() && app.runState() == "Stopping" && signalMilliseconds < 0)
                                 signalMilliseconds = stopTimer.elapsed();
                         });
        QSignalSpy closes(&app, &AppController::closeReady);
        clickLoop();
        require(QTest::qWaitFor([&] { return events().contains("execute:1"); }, 5000),
                "Toolbar click did not start the actual Python gate; state=" + app.runState().toStdString()
                    + "; diagnostic=" + app.diagnostic().toStdString()
                    + "; events=" + events().join(',').toStdString());
        stopTimer.start();
        clickLoop();
        require(QTest::qWaitFor(
                    [&]
                    {
                        bool stoppingVisible = false;
                        for (auto *item : window->findChildren<QQuickItem *>())
                            stoppingVisible = stoppingVisible
                                           || (item->isVisible() && item->property("text").toString() == "Stopping");
                        return stoppingVisible && app.runState() == "Stopping"
                            && loop->property("text").toString() == "等待本轮结束" && !loop->isEnabled();
                    },
                    1000),
                "Stop did not reach the visible toolbar state");
        const auto displayedMilliseconds = stopTimer.elapsed();
        require(signalMilliseconds >= 0 && signalMilliseconds <= 200 && displayedMilliseconds <= 200,
                "Actual stop click did not reach Stopping within 200ms");
        clickLoop();
        QTest::keyClick(window, Qt::Key_F6);
        QTest::qWait(60);
        require(app.busy() && app.runState() == "Stopping" && !events().contains("return:1")
                    && !events().contains("release") && !events().contains("tail"),
                "Stop destroyed an executing tool, dispatched its successor, or bypassed the gate");
        write(permission, "allow");
        require(QTest::qWaitFor([&] { return !app.busy(); }, 5000), "Stopped round did not become Idle");
        require(events().count("execute:1") == 1 && !events().contains("execute:2") && !events().contains("tail"),
                "Stop started another round or executed the successor");
        const auto stoppedNodes = app.nodes();
        require(stoppedNodes.front().toMap().value("state").toString() == "succeeded"
                    && stoppedNodes.back().toMap().value("state").toString() == "idle",
                "Stop erased a completed result or fabricated a successor execution state");
        std::cout << "[CANCELLATION] mouse_stop signal_ms=" << signalMilliseconds
                  << " displayed_ms=" << displayedMilliseconds << " successor_calls=0\n";
        require(QFile::remove(permission), "Cannot close the gate before the next run");
        clickLoop();
        require(QTest::qWaitFor([&] { return events().contains("execute:2"); }, 5000),
                "Second toolbar click did not reuse the initialized Python tool");
        window->close();
        window->close();
        QTest::qWait(60);
        require(window->isVisible() && app.runState() == "Stopping" && closes.isEmpty()
                    && !events().contains("return:2") && !events().contains("release"),
                "Window closed or released the tool before uninterruptible execute returned");
        write(permission, "allow");
        require(QTest::qWaitFor([&] { return !window->isVisible() && !app.busy(); }, 5000),
                "Window close did not finish after the gate returned; state=" + app.runState().toStdString()
                    + "; diagnostic=" + app.diagnostic().toStdString()
                    + "; events=" + events().join(',').toStdString());
        require(closes.size() == 1 && !events().contains("tail"),
                "Close notification duplicated or a successor ran after closing");
    }
    const QList<QByteArray> expected{
        "construct", "initialize", "execute:1", "return:1", "execute:2", "return:2", "release", "destroy",
    };
    require(events() == expected, "Cancellation lifecycle differs: " + events().join(',').toStdString());
    std::cout << "[CANCELLATION] close waited for return; lifecycle=" << events().join(',').constData() << '\n';
}
