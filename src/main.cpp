#include "common/Version.h"
#include "log/LogService.h"
#include "log/LogModel.h"
#include "ui/AppController.h"
#include "ui/ImageViewport.h"
#include "ui/Theme.h"
#include "ui/UiPreferences.h"

#include <quickui/Color.h>

#include <QCommandLineParser>
#include <QDir>
#include <QEventLoop>
#include <QFileInfo>
#include <QGuiApplication>
#include <QImage>
#include <QJsonDocument>
#include <QJsonObject>
#include <QQmlApplicationEngine>
#include <QQmlContext>
#include <QQuickStyle>
#include <QQuickWindow>
#include <QSaveFile>
#include <QTextStream>
#include <QTimer>
#include <algorithm>
#include <charconv>
#include <limits>
#include <thread>

namespace
{
// Release evidence has to describe a settled environment, not a check in flight. The deadline is
// wider than the probe's own budget, so a hung interpreter is reported by the probe itself instead
// of being cut off here and looking like a missing answer.
constexpr int kReportBudgetMs = 120000;

// Writes what the running process knows about Python and exits. It is the only way to record the
// loaded-module snapshot the packaging checks compare against, and it deliberately opens no window:
// a machine without a desktop can still prove which interpreter is resident.
int writePythonReport(visionrt::AppController &controller, const QString &path)
{
    if (controller.pythonEnvironmentPhase() == QStringLiteral("probing"))
    {
        QEventLoop loop;
        QTimer     poll;
        QObject::connect(&poll, &QTimer::timeout, &loop,
                         [&controller, &loop]
                         {
                             if (controller.pythonEnvironmentPhase() != QStringLiteral("probing"))
                                 loop.quit();
                         });
        poll.start(50);
        QTimer::singleShot(kReportBudgetMs, &loop, &QEventLoop::quit);
        loop.exec();
    }

    const auto report = controller.pythonEnvironmentReport();
    const auto bytes  = QJsonDocument(QJsonObject::fromVariantMap(report)).toJson(QJsonDocument::Indented);
    QFileInfo  info(path);
    QDir().mkpath(info.absolutePath());
    QSaveFile file(path);
    if (!file.open(QIODevice::WriteOnly))
    {
        QTextStream(stderr) << "cannot write the Python report: " << path << '\n';
        return 4;
    }
    file.write(bytes);
    if (!file.commit())
    {
        QTextStream(stderr) << "cannot commit the Python report: " << path << '\n';
        return 4;
    }
    const auto ready = report.value(QStringLiteral("environment")).toMap().value(QStringLiteral("phase")).toString()
                       == QStringLiteral("ready");
    // A report about a broken environment is still evidence; the exit code is what tells the caller
    // whether the environment behind it was usable.
    return ready ? 0 : 3;
}
} // namespace

int main(int argc, char *argv[])
{
    // Qt keeps shader and pipeline caches in the per-user cache directory by default. This
    // installation writes everything it creates under the software directory, so the disk caches
    // are disabled instead of being redirected; they are rebuilt on demand.
    qputenv("QSG_RHI_DISABLE_DISK_CACHE", "1");
    qputenv("QT_DISABLE_SHADER_DISK_CACHE", "1");

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
    parser.addOption({"project", "Project file to open on startup", "file"});
    parser.addOption({"select-node", "Node to select on startup", "nodeId"});
    parser.addOption({"screenshot", "Capture UI screenshot to file and exit", "path"});
    parser.addOption({"screenshot-delay", "Delay before screenshot in ms (default 1000)", "ms"});
    parser.addOption({"demo-menu", "Open tools menu on startup for verification"});
    parser.addOption({"demo-settings", "Open the settings window on startup for verification"});
    parser.addOption({"demo-select", "Select first category on startup for verification"});
    parser.addOption({"run-once", "Execute pipeline once on startup"});
    parser.addOption({"python-report", "Write the Python environment evidence report to a file and exit", "path"});
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
    try
    {
        // Logging covers every producer below it and is released only after the controller and the
        // QML engine are gone, so a late Python owner still has a live logger. The production
        // threshold is part of the stored settings and is applied before the service starts, because
        // a producer above this point must already obey the user's choice.
        visionrt::Settings settings;
        visionrt::UiPreferences::loadSettings(settings);
        visionrt::LogConfig logConfig;
        logConfig.minimumLevel = settings.loggingLevel;
        visionrt::LogService::instance().start(logConfig);
        struct LogShutdown
        {
            ~LogShutdown() { visionrt::LogService::instance().shutdown(); }
        } logShutdown;
        QMetaObject::Connection startupConnection;
        visionrt::AppController controller(workers, QCoreApplication::applicationDirPath());
        if (parser.isSet("python-report"))
            return writePythonReport(controller, parser.value("python-report"));
        if (parser.isSet("project"))
        {
            const QString projectPath = parser.value("project");
            const QString selectNode  = parser.value("select-node");
            const bool    runOnce     = parser.isSet("run-once");
            const auto openProject = [&controller, &startupConnection, projectPath, selectNode, runOnce]()
            {
                if (controller.busy())
                    return;
                QObject::disconnect(startupConnection);
                QObject::connect(&controller, &visionrt::AppController::projectReplaced, &controller,
                                 [&controller, selectNode, runOnce]()
                                 {
                                     const auto nodes = controller.nodes();
                                     if (!selectNode.isEmpty())
                                         controller.selectNode(selectNode);
                                     else if (!nodes.isEmpty())
                                         controller.selectNode(nodes.first().toMap().value("id").toString());
                                     if (runOnce)
                                     {
                                         QTimer::singleShot(200, &controller, [&controller]() {
                                             controller.runOnce();
                                         });
                                     }
                                 }, Qt::SingleShotConnection);
                controller.requestOpen(projectPath);
                controller.resolveUnsaved("discard");
            };
            startupConnection = QObject::connect(&controller, &visionrt::AppController::runtimeChanged,
                                                 &controller, openProject);
            openProject();
        }
        qmlRegisterType<visionrt::ImageViewport>("VisionRT.View", 1, 0, "ImageViewport");
        visionrt::registerStyleModule();
        (void)quickui::QuiColor::getInstance();
        QQmlApplicationEngine engine;
        engine.addImportPath(QCoreApplication::applicationDirPath());
        engine.addImportPath(QCoreApplication::applicationDirPath() + "/qml");
        engine.rootContext()->setContextProperty("app", &controller);
        engine.rootContext()->setContextProperty("demoMenu", parser.isSet("demo-menu"));
        engine.rootContext()->setContextProperty("demoSettings", parser.isSet("demo-settings"));
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
