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
#include <QQuickWindow>
#include <QSGRendererInterface>
#include <QScopeGuard>
#include <QSignalSpy>
#include <QTest>
#include <cstdint>
#include <exception>
#include <iostream>
#include <stdexcept>

#ifdef _WIN32
#    ifndef NOMINMAX
#        define NOMINMAX
#    endif
#    include <windows.h>
#else
#    include <sys/mman.h>
#    include <unistd.h>
#endif

using namespace visionrt;

namespace {
void require(bool condition, const char *message)
{
    if (!condition)
        throw std::runtime_error(message);
}

class PixelReadGuard final
{
public:
    explicit PixelReadGuard(const cv::Mat &image)
        : data_(image.data)
        , bytes_(image.total() * image.elemSize())
    {
        require(image.isContinuous() && bytes_ > 0, "pixel guard needs a contiguous mapped image");
#ifdef _WIN32
        SYSTEM_INFO system{};
        GetSystemInfo(&system);
        const auto               page = system.dwPageSize;
        MEMORY_BASIC_INFORMATION mapping{};
        require(VirtualQuery(data_, &mapping, sizeof(mapping)) == sizeof(mapping) && mapping.AllocationBase == data_
                    && mapping.Type == MEM_MAPPED,
                "pixel guard image does not own its mapped pages");
#else
        const auto page = static_cast<std::size_t>(sysconf(_SC_PAGESIZE));
#endif
        require(reinterpret_cast<std::uintptr_t>(data_) % page == 0 && bytes_ % page == 0,
                "pixel guard image is not page aligned");
#ifdef _WIN32
        require(VirtualProtect(data_, bytes_, PAGE_NOACCESS, &original_), "pixel pages could not be protected");
#else
        require(mprotect(data_, bytes_, PROT_NONE) == 0, "pixel pages could not be protected");
#endif
    }

    ~PixelReadGuard()
    {
#ifdef _WIN32
        DWORD ignored = 0;
        if (!VirtualProtect(data_, bytes_, original_, &ignored))
            std::terminate();
#else
        if (mprotect(data_, bytes_, PROT_READ | PROT_WRITE) != 0)
            std::terminate();
#endif
    }

    PixelReadGuard(const PixelReadGuard &)            = delete;
    PixelReadGuard &operator=(const PixelReadGuard &) = delete;

private:
    void       *data_;
    std::size_t bytes_;
#ifdef _WIN32
    DWORD original_ = 0;
#endif
};

void observeQuiet(AppController &app, ImageViewport &viewport, bool running)
{
    const auto phase = running ? "running" : "idle";
    QSignalSpy runtime(&app, &AppController::runtimeChanged);
    QSignalSpy nodes(&app, &AppController::nodesChanged);
    QSignalSpy results(&app, &AppController::resultsChanged);
    QSignalSpy parameters(&app, &AppController::parametersChanged);
    QSignalSpy pixels(&viewport, &ImageViewport::imageChanged);
    QSignalSpy frames(&viewport, &ImageViewport::imagePresented);
    std::cout << "QUIET_BEGIN " << phase << std::endl;
    QTest::qWait(5000);
    std::cout << "QUIET_END " << phase << " runtime=" << runtime.count() << " nodes=" << nodes.count()
              << " results=" << results.count() << " parameters=" << parameters.count() << " pixels=" << pixels.count()
              << " frames=" << frames.count() << std::endl;
    require(app.busy() == running && runtime.isEmpty() && nodes.isEmpty() && results.isEmpty() && parameters.isEmpty()
                && pixels.isEmpty() && frames.isEmpty(),
            "unchanged runtime fabricated notifications or display work");
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
    const auto                     script     = package + "python/custom/notifications/tool.py";
    const auto                     permission = QFileInfo(script).absolutePath() + "/allow";
    const auto                     entered    = QFileInfo(script).absolutePath() + "/entered";
    const auto                     write      = [&](const QString &path, const QByteArray &bytes)
    {
        require(QDir().mkpath(QFileInfo(path).absolutePath()), "notification fixture parent unavailable");
        QFile file(path);
        require(file.open(QIODevice::WriteOnly) && file.write(bytes) == bytes.size(),
                "notification fixture write failed");
    };
    ToolDefinition definition;
    definition.id          = "test.notifications";
    definition.displayName = "静默轮询";
    definition.kind        = ToolKind::PythonFunction;
    definition.entry       = {"notifications", "tool.py", "source"};
    definition.outputs     = {
        { "value", "次数",   {Type::Int}},
        {"images", "图像", {Type::Image}}
    };
    definition.outputs.back().colorOrder = "BGR";
    write(package + "tools/custom/notifications/source.json",
          QByteArray::fromStdString(emitNormalizedToolJson(definition)));
    write(script, R"py(import mmap
import time
from pathlib import Path
import numpy as np

root = Path(__file__).parent
storage = mmap.mmap(-1, 64 * 64 * 3)
image = np.ndarray((64, 64, 3), dtype=np.uint8, buffer=storage)
calls = 0

def source(inputs):
    global calls
    calls += 1
    if calls > 1:
        (root / 'entered').write_text('entered', encoding='utf-8')
        while not (root / 'allow').exists():
            time.sleep(0.002)
    image.fill(17 if calls == 1 else 37)
    return {'status': {'code': 0, 'message': '', 'detail': ''},
            'outputs': {'value': calls, 'images': [image]}}
)py");
    QQuickWindow window;
    window.resize(320, 240);
    ImageViewport viewport(window.contentItem());
    viewport.setSize({320, 240});
    window.show();
    require(QTest::qWaitForWindowExposed(&window), "notification window was not exposed");
    AppController app(2, root);
    const auto    releaseOnExit = qScopeGuard([&] { write(permission, "allow"); });
    const auto    idle          = [&]
    {
        require(QTest::qWaitFor([&] { return !app.busy(); }, 10000), "notification operation did not finish");
    };
    idle();
    app.attachViewport(&viewport);
    auto *outputs = qobject_cast<OutputModel *>(app.outputModel());
    require(outputs, "notification output model unavailable");
    app.addNode("test.notifications", 0, 0);
    idle();
    QSignalSpy shown(&viewport, &ImageViewport::imagePresented);
    app.runNode();
    idle();
    require(QTest::qWaitFor([&] { return !shown.isEmpty() && viewport.hasImage(); }, 5000),
            "notification initial frame was not presented");
    const auto frame = [&](int pixel)
    {
        require(QTest::qWaitFor(
                    [&]
                    {
                        const auto image = window.grabWindow();
                        return !image.isNull()
                            && image.pixelColor(image.width() / 2, image.height() / 2) == QColor(pixel, pixel, pixel);
                    },
                    5000),
                "notification cached frame has incorrect pixels");
    };
    frame(17);
    QElapsedTimer settled;
    settled.start();
    auto presented = shown.count();
    require(QTest::qWaitFor(
                [&]
                {
                    if (shown.count() != presented)
                    {
                        presented = shown.count();
                        settled.restart();
                    }
                    return settled.elapsed() >= 250;
                },
                5000),
            "notification initial display work did not settle");
    const auto original = outputs->capture("images");
    require(original && std::holds_alternative<DataRef<Image>>(original->value),
            "notification source image unavailable");
    const auto &images = std::get<DataRef<Image>>(original->value).get();
    require(images.size() == 1, "notification source image batch is incomplete");
    {
        PixelReadGuard protectedPixels(images[0]);
        if (application.arguments().contains("--probe-pixel-read"))
        {
            std::cout << "PIXEL_GUARD_PROBE" << std::endl;
            const auto *pixel = static_cast<volatile const unsigned char *>(images[0].data);
            std::cout << "UNEXPECTED_PIXEL_READ " << static_cast<int>(*pixel) << std::endl;
            return 1;
        }
        observeQuiet(app, viewport, false);
        frame(17);
        app.runNode();
        require(QTest::qWaitFor(
                    [&]
                    {
                        return QFileInfo::exists(entered) && !app.nodes().isEmpty()
                            && app.nodes().front().toMap().value("state").toString() == "running";
                    },
                    5000),
                "notification tool did not enter its controlled active state");
        observeQuiet(app, viewport, true);
        frame(17);
        const auto current = outputs->capture("images");
        require(current && current->publication == original->publication,
                "quiet active polling fabricated a new image publication");
    }
    write(permission, "allow");
    idle();
    const auto value = outputs->capture("value");
    const auto image = outputs->capture("images");
    require(value && std::get<std::int64_t>(value->value) == 2 && image && image->publication != original->publication,
            "notification writer did not resume with its final publication");
    frame(37);
    std::cout << "NOTIFICATION_CONTRACT_PASS source_pages=12288 phases=2 final_pixel=37" << std::endl;
    window.hide();
    return 0;
}
catch (const std::exception &error)
{
    std::cerr << error.what() << '\n';
    return 1;
}
