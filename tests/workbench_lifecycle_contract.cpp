#include "WorkbenchFixtures.h"
#include "core/DefinitionJson.h"
#include "ui/AppController.h"
#include "ui/ImageViewport.h"
#include "ui/OutputModel.h"
#include "ui/Theme.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QGuiApplication>
#include <QQmlApplicationEngine>
#include <QQmlContext>
#include <QQuickItem>
#include <QQuickWindow>
#include <QScopeGuard>
#include <QSet>
#include <QSignalSpy>
#include <QTest>
#include <iostream>
#include <stdexcept>


using namespace visionrt;

namespace {
void require(bool condition, const std::string &message)
{
    if (!condition)
        throw std::runtime_error(message);
}
}

int main(int argc, char **argv)
try
{
    QGuiApplication application(argc, argv);
    registerStyleModule();
    testing::WorkbenchInstallation installation;
    const auto                     root       = installation.directory();
    const auto                     script     = root + "/tools-package/python/custom/lifecycle/tool.py";
    const auto                     log        = QFileInfo(script).absolutePath() + "/events";
    const auto                     permission = QFileInfo(script).absolutePath() + "/allow";
    const auto                     write      = [&](const QString &path, const QByteArray &bytes)
    {
        require(QDir().mkpath(QFileInfo(path).absolutePath()), "Cannot create lifecycle fixture directory");
        QFile file(path);
        require(file.open(QIODevice::WriteOnly) && file.write(bytes) == bytes.size(), "Cannot write lifecycle fixture");
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
    gate.id          = "test.lifecycle.gate";
    gate.displayName = "Lifecycle gate";
    gate.kind        = ToolKind::PythonClass;
    gate.entry       = {"lifecycle", "tool.py", "Gate"};
    gate.outputs     = {
        {"value", "Value", {Type::Int}},
        {"image", "Image", {Type::Image}}
    };
    write(root + "/tools-package/tools/custom/lifecycle.json",
          QByteArray::fromStdString(emitNormalizedToolJson(gate)));
    write(script, R"py(from pathlib import Path
import itertools
import time

root = Path(__file__).parent
serial = itertools.count(1)

def record(event):
    with (root / 'events').open('a', encoding='utf-8') as stream:
        stream.write(event + '\n')

import numpy as np

class Gate:
    def __init__(self):
        self.serial = next(serial)
        record(f'construct:{self.serial}')

    def initialize(self, inputs):
        record(f'initialize:{self.serial}')
        return {'code': 0, 'message': '', 'detail': ''}

    def execute(self, inputs):
        record(f'execute:{self.serial}')
        deadline = time.monotonic() + 10
        while not (root / 'allow').exists():
            if time.monotonic() >= deadline:
                raise RuntimeError('Lifecycle gate deadline exceeded')
            time.sleep(0.001)
        record(f'return:{self.serial}')
        img = np.zeros((10, 10), dtype=np.uint8)
        return {'status': {'code': 0, 'message': '', 'detail': ''},
                'outputs': {'value': 1, 'image': [img]}}

    def release(self):
        record(f'release:{self.serial}')
        return {'code': 0, 'message': '', 'detail': ''}
)py");
    AppController app(1, root);
    const bool ready = QTest::qWaitFor([&] { return !app.busy(); }, 15000);
    require(ready, ("lifecycle catalog did not become ready: " + app.runState() + " "
                    + app.diagnostics().join(QStringLiteral(" | ")))
                       .toStdString());
    app.addNode("test.lifecycle.gate", 0, 0);
    require(QTest::qWaitFor([&] { return !app.busy() && app.nodes().size() == 1; }, 5000),
            "lifecycle node was not created");
    qmlRegisterType<ImageViewport>("VisionRT.View", 1, 0, "ImageViewport");
    QQmlApplicationEngine engine;
    engine.addImportPath(QStringLiteral(VISIONRT_BINARY_DIR "/qml"));
    engine.addImportPath(QStringLiteral(VISIONRT_SOURCE_DIR "/src/ui/qml"));
    engine.addImportPath(QStringLiteral(VISIONRT_SOURCE_DIR "/3rdparty/QuickUI/quickui"));
    engine.rootContext()->setContextProperty("app", &app);
    engine.rootContext()->setContextProperty("demoMenu", false);
    engine.rootContext()->setContextProperty("demoSelect", false);
    engine.load(QUrl::fromLocalFile(QStringLiteral(VISIONRT_SOURCE_DIR "/src/ui/qml/Main.qml")));
    require(!engine.rootObjects().isEmpty(), "lifecycle Main.qml did not load");
    auto *window = qobject_cast<QQuickWindow *>(engine.rootObjects().first());
    require(window && QTest::qWaitForWindowExposed(window), "lifecycle window was not exposed");
    const auto releaseOnExit = qScopeGuard([&] { write(permission, "allow"); });
    QQuickItem *loop         = nullptr;
    for (auto *item : window->findChildren<QQuickItem *>())
        if (item->isVisible() && item->isEnabled() && item->property("text").toString() == "循环")
            loop = item;
    require(loop != nullptr, "lifecycle loop button was not found");
    const auto hasPrefix = [&](const QByteArray &prefix)
    {
        for (const auto &event : events())
            if (event.startsWith(prefix))
                return true;
        return false;
    };
    const auto countPrefix = [&](const QByteArray &prefix)
    {
        int count = 0;
        for (const auto &event : events())
            if (event.startsWith(prefix))
                ++count;
        return count;
    };
    QSignalSpy closes(&app, &AppController::closeReady);
    int closeReadySyncInvocations = 0;
    int constructsAtCloseReady = 0;
    int initsAtCloseReady = 0;
    QObject::connect(&app, &AppController::closeReady, [&] {
        ++closeReadySyncInvocations;
        constructsAtCloseReady = countPrefix("construct:");
        initsAtCloseReady = countPrefix("initialize:");
        require(app.isClosing(), "app must report isClosing() in closeReady synchronous slot");
        app.runOnce();
        app.refreshPython();
        const int flowCount = app.flows().size();
        app.addFlow("rejected_flow");
        require(app.flows().size() == flowCount, "editing must be rejected during ClosePhase::Ready");
        require(app.runState() == "Idle", "runState must remain Idle and not Running");
    });
    QTest::mouseClick(window, Qt::LeftButton, Qt::NoModifier,
                      loop->mapToScene(QPointF(loop->width() / 2, loop->height() / 2)).toPoint(), 0);
    require(QTest::qWaitFor([&] { return hasPrefix("execute:"); }, 5000),
            "lifecycle loop did not start the gate");
    QTest::mouseClick(window, Qt::LeftButton, Qt::NoModifier,
                      loop->mapToScene(QPointF(loop->width() / 2, loop->height() / 2)).toPoint(), 0);
    require(QTest::qWaitFor([&] { return app.runState() == "Stopping"; }, 2000),
            "lifecycle stop did not reach Stopping");
    write(permission, "allow");
    require(QTest::qWaitFor([&] { return !app.busy() && app.runState() == "Idle"; }, 5000),
            "lifecycle loop stop did not return to Idle; state=" + app.runState().toStdString() + "; events="
                + events().join(',').toStdString());

    const auto nodeId = app.nodes().first().toMap().value("id").toString();
    app.selectNode(nodeId);
    auto *outputModel = qobject_cast<OutputModel *>(app.outputModel());
    require(outputModel != nullptr, "lifecycle outputModel is null");
    auto captured = outputModel->capture(QStringLiteral("image"));
    require(captured.has_value(), "lifecycle image output was not captured");
    require(std::holds_alternative<DataRef<Image>>(captured->value), "captured value is not DataRef<Image>");

    require(app.save(root + "/lifecycle.vrt.json"), "lifecycle project could not be saved");
    require(QTest::qWaitFor([&] { return !app.busy() && !app.dirty(); }, 5000),
            "lifecycle project did not become clean");

    // Close directly with active Python output and external owner held (no prior resetRuntime)
    app.requestClose();
    app.requestClose();
    QTest::qWait(100);
    require(closes.size() == 0, "closeReady must not be emitted while external owner is held");
    require(window->isVisible(), "window must remain visible while awaiting owners");

    // T-D01: While in AwaitOwners, save attempts and Ctrl+S must be rejected without disk writes or re-synchronization
    require(app.isClosing(), "app must report isClosing() while in AwaitOwners");
    const int constructsBeforeSave = countPrefix("construct:");
    const int initsBeforeSave = countPrefix("initialize:");
    const bool directSaveResult = app.save(root + "/illegal_save.vrt.json");
    require(!directSaveResult, "app.save() with path must be rejected during AwaitOwners");
    require(!QFile::exists(root + "/illegal_save.vrt.json"), "save during AwaitOwners must not create file");

    const bool emptySaveResult = app.save();
    require(!emptySaveResult, "app.save() without path must be rejected during AwaitOwners");

    QTest::keyClick(window, Qt::Key_S, Qt::ControlModifier);
    QTest::qWait(100);
    require(countPrefix("construct:") == constructsBeforeSave, "instance was constructed during AwaitOwners");
    require(countPrefix("initialize:") == initsBeforeSave, "instance was initialized during AwaitOwners");
    require(closes.size() == 0, "closeReady must not be emitted during save attempts");

    // Release external owner - close should now proceed automatically
    captured.reset();
    require(QTest::qWaitFor([&] { return closes.size() == 1; }, 5000),
            "lifecycle close did not finish after owner was released; state=" + app.runState().toStdString()
                + "; events=" + events().join(',').toStdString());
    require(QTest::qWaitFor([&] { return !window->isVisible(); }, 5000),
            "lifecycle window did not hide after close");
    QTest::qWait(200);
    require(closes.size() == 1, "repeated stop/reset/close produced duplicate completion");
    require(closeReadySyncInvocations == 1, "closeReady slot must be called exactly once");
    require(countPrefix("construct:") == constructsAtCloseReady, "instances must not be constructed during Ready");
    require(countPrefix("initialize:") == initsAtCloseReady, "instances must not be initialized during Ready");
    app.runOnce();
    app.refreshPython();
    const int postFlowCount = app.flows().size();
    app.addFlow("rejected_flow_post");
    require(app.flows().size() == postFlowCount, "editing must be rejected after closeReady");
    require(countPrefix("construct:") == constructsAtCloseReady, "instances must not be constructed after Ready");
    require(countPrefix("initialize:") == initsAtCloseReady, "instances must not be initialized after Ready");
    QTest::qWait(500);
    require(countPrefix("execute:") == 1 && countPrefix("return:") == 1,
            "lifecycle stop started another round: " + events().join(',').toStdString());
    QSet<QByteArray> initialized;
    QSet<QByteArray> released;
    for (const auto &event : events())
    {
        if (event.startsWith("initialize:"))
            initialized.insert(event.mid(11));
        else if (event.startsWith("release:"))
        {
            const auto serial = event.mid(8);
            require(!released.contains(serial), "lifecycle released the same instance twice: "
                                                    + events().join(',').toStdString());
            released.insert(serial);
        }
    }
    require(!initialized.isEmpty(), "lifecycle never initialized its instance");
    for (const auto &serial : initialized)
        require(released.contains(serial), "lifecycle left an initialized instance unreleased");
    std::cout << "WORKBENCH_LIFECYCLE_PASS stop_reset_close=1 completions=1 next_round=0 initialized="
              << initialized.size() << " releases=" << released.size() << '\n';
    return 0;
}
catch (const std::exception &error)
{
    std::cerr << error.what() << '\n';
    return 1;
}
