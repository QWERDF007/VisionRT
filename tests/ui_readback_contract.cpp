#include "WorkbenchFixtures.h"
#include "core/DefinitionJson.h"
#include "ui/AppController.h"
#include "ui/ImageViewport.h"
#include "ui/OutputModel.h"

#include <QDir>
#include <QElapsedTimer>
#include <QFile>
#include <QFileInfo>
#include <QGuiApplication>
#include <QJsonArray>
#include <QJsonDocument>
#include <QQuickWindow>
#include <QSGRendererInterface>
#include <QScopeGuard>
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
    QGuiApplication application(argc, argv);
    QQuickWindow::setGraphicsApi(QSGRendererInterface::Software);
    testing::WorkbenchInstallation installation;
    const auto                     root       = installation.directory();
    const auto                     package    = root + "/tools-package/";
    const auto                     script     = package + "python/custom/readback/tool.py";
    const auto                     permission = QFileInfo(script).absolutePath() + "/allow";
    const auto                     entered    = QFileInfo(script).absolutePath() + "/entered";
    const auto                     write      = [&](const QString &path, const QByteArray &bytes)
    {
        require(QDir().mkpath(QFileInfo(path).absolutePath()), "readback fixture parent unavailable");
        QFile file(path);
        require(file.open(QIODevice::WriteOnly) && file.write(bytes) == bytes.size(), "readback fixture write failed");
    };
    ToolDefinition definition;
    definition.id          = "test.readback";
    definition.displayName = "读取阶段";
    definition.kind        = ToolKind::PythonFunction;
    definition.entry       = {"readback", "tool.py", "run"};
    definition.outputs     = {
        { "values",   "值", {Type::Array, DType::Int64, 1}},
        {"images", "图像",                          {Type::Image}}
    };
    definition.outputs.back().colorOrder = "BGR";
    write(package + "tools/custom/readback/source.json",
          QByteArray::fromStdString(emitNormalizedToolJson(definition)));
    write(script, R"py(import mmap
import time
from pathlib import Path
import numpy as np

root = Path(__file__).parent
storage0 = mmap.mmap(-1, 64 * 64 * 3)
storage1 = mmap.mmap(-1, 64 * 64 * 3)
image0 = np.ndarray((64, 64, 3), dtype=np.uint8, buffer=storage0)
image1 = np.ndarray((64, 64, 3), dtype=np.uint8, buffer=storage1)
calls = 0

def run(inputs):
    global calls
    calls += 1
    if calls > 1:
        (root / 'entered').write_text('entered', encoding='utf-8')
        while not (root / 'allow').exists():
            time.sleep(0.002)
    image0.fill(17 if calls == 1 else 37)
    image1.fill(18 if calls == 1 else 38)
    return {'__status_code': 0, '__status_message': '', 'values': [calls] * 100000, 'images': [image0, image1]}
)py");
    QQuickWindow window;
    window.resize(320, 240);
    ImageViewport viewport(window.contentItem());
    viewport.setSize({320, 240});
    window.show();
    require(QTest::qWaitForWindowExposed(&window), "readback window was not exposed");
    AppController app(2, root);
    const auto    releaseOnExit = qScopeGuard([&] { write(permission, "allow"); });
    const auto    idle          = [&]
    {
        require(QTest::qWaitFor([&] { return !app.busy(); }, 10000), "readback operation did not finish");
    };
    idle();
    app.attachViewport(&viewport);
    auto *outputs = qobject_cast<OutputModel *>(app.outputModel());
    require(outputs, "readback output model unavailable");
    app.addNode("test.readback", 0, 0);
    idle();
    QSignalSpy presented(&viewport, &ImageViewport::imagePresented);
    const auto frame    = [&](int pixel, int timeout = 5000)
    {
        require(QTest::qWaitFor(
                    [&]
                    {
                        const auto image = window.grabWindow();
                        return !image.isNull()
                            && image.pixelColor(image.width() / 2, image.height() / 2) == QColor(pixel, pixel, pixel);
                    },
                    timeout),
                "readback frame has incorrect pixels");
    };
    app.runNode();
    idle();
    require(QTest::qWaitFor([&] { return !presented.isEmpty() && viewport.hasImage(); }, 5000),
            "readback initial frame was not presented");
    frame(17);
    require(app.imageCount() == 2, "readback image batch is incomplete");
    const auto valuesContext = app.portContext("values", true);
    require(!valuesContext.isEmpty(), "readback values context unavailable");
    QSignalSpy text(&app, &AppController::outputTextReady);
    const auto shown = presented.count();
    QElapsedTimer queued;
    queued.start();
    app.runNode();
    require(QTest::qWaitFor(
                [&]
                {
                    return QFileInfo::exists(entered) && !app.nodes().isEmpty()
                        && app.nodes().front().toMap().value("state").toString() == "running";
                },
                5000),
            "readback tool did not enter its controlled active state");
    app.setImageIndex(1);
    QTest::qWait(100);
    require(presented.count() == shown, "active execution presented a new frame");
    frame(17);
    app.requestOutputText(valuesContext, false, false);
    QTest::qWait(300);
    require(text.isEmpty(), "full text was generated during active execution");
    require(app.busy(), "active execution ended before the display read stage");
    const auto queuedMs = queued.elapsed();
    write(permission, "allow");
    idle();
    require(text.size() == 1, "queued full text was dropped after the display read stage");
    const auto values = QJsonDocument::fromJson(text.front().front().toString().toUtf8()).array();
    require(values.size() == 100000 && values.first().toInt() == 2 && values.last().toInt() == 2,
            "queued full text read the wrong publication");
    frame(38);
    // T-C03 / S-C04: Nested management inside outputTextReady preserves draining guard, delivers exactly once, no premature ACK
    {
        int nestedCount = 0;
        const auto conn = QObject::connect(&app, &AppController::outputTextReady, &app,
            [&](const QString &)
            {
                ++nestedCount;
                app.resetRuntime();
            });
        text.clear();
        const auto currentContext = app.portContext("values", true);
        app.requestOutputText(currentContext, false, false);
        require(QTest::qWaitFor([&] { return nestedCount == 1; }, 5000),
                "nested management operation not triggered via outputTextReady");
        idle();
        require(text.size() == 1, "outputTextReady delivered wrong number of times");
        QObject::disconnect(conn);
    }
    std::cout << "READBACK_CONTRACT_PASS queued_ms=" << queuedMs << " values=100000 frame=38 index=1\n";
    window.hide();
    return 0;
}
catch (const std::exception &error)
{
    std::cerr << error.what() << '\n';
    return 1;
}
