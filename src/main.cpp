#include "common/Version.h"
#include "ui/AppController.h"
#include "ui/ImageViewport.h"
#include "ui/Theme.h"

#include <quickui/Color.h>

#include <QCommandLineParser>
#include <QDir>
#include <QFileInfo>
#include <QGuiApplication>
#include <QImage>
#include <QQmlApplicationEngine>
#include <QQmlContext>
#include <QQuickStyle>
#include <QQuickWindow>
#include <QTextStream>
#include <QTimer>
#include <algorithm>
#include <charconv>
#include <limits>
#include <thread>

int main(int argc, char *argv[])
{
    QGuiApplication application(argc, argv);
    QCoreApplication::setApplicationName("VisionRT");
    QCoreApplication::setOrganizationName("VisionRT");
    QCoreApplication::setApplicationVersion(QString::fromStdString(visionrt::GetVersionString()));
    QQuickStyle::setStyle("Basic");
    QCommandLineParser parser;
    parser.setApplicationDescription("VisionRT visual workflow workbench");
    parser.addHelpOption();
    parser.addVersionOption();
    parser.addOption({"workers", "Taskflow worker count (positive integer)", "count"});
    parser.addOption({"python-home", "Matching CPython runtime directory", "directory"});
    parser.addOption({"project", "Project file to open on startup", "file"});
    parser.addOption({"select-node", "Node to select on startup", "nodeId"});
    parser.addOption({"screenshot", "Capture UI screenshot to file and exit", "path"});
    parser.addOption({"screenshot-delay", "Delay before screenshot in ms (default 1000)", "ms"});
    parser.addOption({"demo-menu", "Open tools menu on startup for verification"});
    parser.addOption({"demo-select", "Select first category on startup for verification"});
    parser.process(application);
    std::size_t workers = std::max(2u, std::thread::hardware_concurrency());
    if (parser.isSet("workers"))
    {
        const auto text         = parser.value("workers").toLatin1();
        const auto [end, error] = std::from_chars(text.constData(), text.constData() + text.size(), workers);
        if (error != std::errc{} || end != text.constData() + text.size() || workers == 0)
        {
            QTextStream(stderr) << "--workers requires a positive integer\n";
            return 2;
        }
    }
    if (parser.isSet("python-home"))
        qputenv("VISIONRT_PYTHON_HOME", parser.value("python-home").toUtf8());
    try
    {
        QMetaObject::Connection startupConnection;
        visionrt::AppController controller(workers, QCoreApplication::applicationDirPath());
        if (parser.isSet("project"))
        {
            const QString projectPath = parser.value("project");
            const QString selectNode  = parser.value("select-node");
            const auto openProject = [&controller, &startupConnection, projectPath, selectNode]()
            {
                if (controller.busy())
                    return;
                QObject::disconnect(startupConnection);
                QObject::connect(&controller, &visionrt::AppController::projectReplaced, &controller,
                                 [&controller, selectNode]()
                                 {
                                     const auto nodes = controller.nodes();
                                     if (!selectNode.isEmpty())
                                         controller.selectNode(selectNode);
                                     else if (!nodes.isEmpty())
                                         controller.selectNode(nodes.first().toMap().value("id").toString());
                                 }, Qt::SingleShotConnection);
                controller.requestOpen(projectPath);
                controller.resolveUnsaved("discard");
            };
            startupConnection = QObject::connect(&controller, &visionrt::AppController::runtimeChanged,
                                                 &controller, openProject);
            openProject();
        }
        qmlRegisterType<visionrt::ImageViewport>("VisionRT.View", 1, 0, "ImageViewport");
        auto *theme = visionrt::Theme::instance();
        qmlRegisterSingletonInstance<visionrt::Theme>("VisionRT", 1, 0, "Theme", theme);
        (void)quickui::QuiColor::getInstance();
        QQmlApplicationEngine engine;
        engine.addImportPath(QCoreApplication::applicationDirPath());
        engine.addImportPath(QCoreApplication::applicationDirPath() + "/qml");
        engine.rootContext()->setContextProperty("app", &controller);
        engine.rootContext()->setContextProperty("Theme", theme);
        engine.rootContext()->setContextProperty("demoMenu", parser.isSet("demo-menu"));
        engine.rootContext()->setContextProperty("demoSelect", parser.isSet("demo-select"));
        QObject::connect(
            &engine, &QQmlApplicationEngine::objectCreationFailed, &application, [] { QCoreApplication::exit(1); },
            Qt::QueuedConnection);
        engine.loadFromModule("VisionRT", "Main");

        if (parser.isSet("screenshot"))
        {
            const QString screenshotPath = parser.value("screenshot");
            int           delay          = 1200;
            if (parser.isSet("screenshot-delay"))
            {
                delay = parser.value("screenshot-delay").toInt();
                if (delay <= 0)
                    delay = 1200;
            }
            QTimer::singleShot(delay,
                               [&engine, screenshotPath]()
                               {
                                   int result = 1;
                                   const auto roots = engine.rootObjects();
                                   if (!roots.isEmpty())
                                   {
                                       if (auto *window = qobject_cast<QQuickWindow *>(roots.first()))
                                       {
                                           QImage    img = window->grabWindow();
                                           QFileInfo fi(screenshotPath);
                                           QDir().mkpath(fi.absolutePath());
                                           result = img.save(screenshotPath) ? 0 : 1;
                                       }
                                   }
                                   QCoreApplication::exit(result);
                               });
        }

        return application.exec();
    }
    catch (const std::exception &error)
    {
        QTextStream(stderr) << "VisionRT initialization failed: " << error.what() << '\n';
        return 1;
    }
}
