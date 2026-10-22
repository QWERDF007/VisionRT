#include "ui/AppController.h"
#include "ui/ImageViewport.h"
#include "ui/Theme.h"

#include <QQmlApplicationEngine>
#include <QQmlContext>
#include <QQuickItem>
#include <QQuickWindow>
#include <QTest>
#include <iostream>
#include <stdexcept>

void startupProjectContract()
{
    const auto require = [](bool condition, const std::string &message)
    {
        if (!condition)
            throw std::runtime_error(message);
    };
    qmlRegisterType<visionrt::ImageViewport>("VisionRT.View", 1, 0, "ImageViewport");
    qmlRegisterSingletonInstance<visionrt::Theme>("VisionRT", 1, 0, "Theme", visionrt::Theme::instance());
    for (const bool projectReadyBeforeWindow : {false, true})
    {
        visionrt::AppController app(1, QStringLiteral(VISIONRT_BINARY_DIR));
        require(QTest::qWaitFor([&] { return !app.busy(); }, 5000), "startup catalog did not become ready");
        app.requestOpen(QStringLiteral(VISIONRT_SOURCE_DIR "/examples/pipeline_cpu_python.vrt.json"));
        app.resolveUnsaved(QStringLiteral("discard"));
        if (projectReadyBeforeWindow)
            require(QTest::qWaitFor([&] { return !app.busy() && app.nodes().size() == 5; }, 5000),
                    "preloaded startup project did not become ready");

        QQmlApplicationEngine engine;
        engine.addImportPath(QStringLiteral(VISIONRT_BINARY_DIR "/qml"));
        engine.rootContext()->setContextProperty("app", &app);
        engine.rootContext()->setContextProperty("Theme", visionrt::Theme::instance());
        engine.rootContext()->setContextProperty("demoMenu", false);
        engine.rootContext()->setContextProperty("demoSelect", false);
        engine.load(QUrl::fromLocalFile(QStringLiteral(VISIONRT_SOURCE_DIR "/src/ui/qml/Main.qml")));
        require(!engine.rootObjects().isEmpty(), "startup Main.qml did not load");
        auto *window = qobject_cast<QQuickWindow *>(engine.rootObjects().first());
        require(window && QTest::qWaitForWindowExposed(window), "startup main window was not exposed");
        require(QTest::qWaitFor([&] { return !app.busy() && app.nodes().size() == 5; }, 5000),
                "startup main window did not receive the real project");

        const auto findItem = [](auto &&self, QQuickItem *parent, const QString &name) -> QQuickItem *
        {
            for (auto *child : parent->childItems())
            {
                if (child->objectName() == name)
                    return child;
                if (auto *found = self(self, child, name))
                    return found;
            }
            return nullptr;
        };
        auto *canvas = findItem(findItem, window->contentItem(), QStringLiteral("flowCanvas"));
        require(canvas != nullptr, "startup flow canvas is missing");
        auto *surface = findItem(findItem, canvas, QStringLiteral("flowSurface"));
        require(surface != nullptr, "startup flow surface is missing");
        const auto allVisible = [&]
        {
            for (const auto &value : app.nodes())
            {
                const auto id = value.toMap().value("id").toString();
                auto *item = findItem(findItem, canvas, QStringLiteral("flowNode_") + id);
                if (!item || !item->isVisible())
                    return false;
                const auto bounds = item->mapRectToItem(surface, item->boundingRect());
                if (bounds.width() < 70 || bounds.height() <= 0
                    || !surface->boundingRect().adjusted(1, 1, -1, -1).contains(bounds))
                    return false;
            }
            return true;
        };
        const bool visible = QTest::qWaitFor(allVisible, 1500);
        const auto mode = projectReadyBeforeWindow ? "preloaded" : "pending";
        require(window->grabWindow().save(QStringLiteral(VISIONRT_BINARY_DIR "/evidence/startup_%1.png")
                                             .arg(QString::fromLatin1(mode))),
                "cannot save startup main-window evidence");
        if (!visible)
            throw std::runtime_error(std::string("Startup ") + mode + " project is clipped: viewport="
                                     + std::to_string(surface->width()) + "x" + std::to_string(surface->height())
                                     + "; zoom=" + std::to_string(canvas->property("zoom").toDouble())
                                     + "; pan=" + std::to_string(canvas->property("panX").toDouble()) + ","
                                     + std::to_string(canvas->property("panY").toDouble()));
        std::cout << "[Startup Contract Passed] " << mode << " project has five visible, readable nodes.\n";
    }
}
