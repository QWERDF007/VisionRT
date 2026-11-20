#include "TestPythonConfiguration.h"
#include "WorkbenchFixtures.h"
#include "log/LogService.h"
#include "ui/AppController.h"
#include "ui/ImageViewport.h"
#include "ui/UiTypes.h"
#include "ui/ToolEditorController.h"

#include <QCoreApplication>
#include <QGuiApplication>
#include <QProcess>
#include <QQmlApplicationEngine>
#include <QQmlContext>
#include <QQuickStyle>
#include <QQuickWindow>
#include <QScopeGuard>
#include <QTest>
#include <QTimer>
#include <iostream>
#include <stdexcept>

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
    if (argc == 3 && std::string(argv[1]) == "--close-child")
    {
        testsupport::useBuildInterpreter();
        QGuiApplication application(argc, argv);
        QQuickStyle::setStyle("Basic");
        visionrt::registerUiTypes();
        qmlRegisterType<visionrt::ImageViewport>("VisionRT.View", 1, 0, "ImageViewport");
        visionrt::testing::WorkbenchInstallation installation;
        require(visionrt::LogService::instance().start(visionrt::LogConfig{}, installation.directory() + "/logs/exit.log"),
                "exit fixture could not start logging");
        const auto stopLogs = qScopeGuard([] { visionrt::LogService::instance().shutdown(); });
        visionrt::AppController controller(1, installation.directory());
        require(QTest::qWaitFor([&] { return !controller.busy(); }, 10000), "exit fixture did not settle");
        QQmlApplicationEngine engine;
        engine.addImportPath(QStringLiteral(VISIONRT_BINARY_DIR "/qml"));
        engine.rootContext()->setContextProperty("app", &controller);
        engine.rootContext()->setContextProperty("demoMenu", false);
        engine.rootContext()->setContextProperty("demoSelect", false);
        engine.load(QUrl::fromLocalFile(QStringLiteral(VISIONRT_SOURCE_DIR "/src/ui/qml/Main.qml")));
        require(!engine.rootObjects().isEmpty(), "exit fixture could not load Main.qml");
        auto *window = qobject_cast<QQuickWindow *>(engine.rootObjects().first());
        require(window && QTest::qWaitForWindowExposed(window), "exit fixture main window was not exposed");
        require(!controller.dirty(), "exit fixture must start without unsaved changes");
        QObject::connect(&controller, &visionrt::AppController::closeReady, [] {
            std::cout << "CLOSE_READY" << std::endl;
        });
        QObject::connect(&application, &QGuiApplication::lastWindowClosed, [] {
            std::cout << "LAST_WINDOW_CLOSED" << std::endl;
        });
        QObject::connect(&application, &QCoreApplication::aboutToQuit, [] {
            std::cout << "APPLICATION_QUITTING" << std::endl;
        });
        const std::string scenario = argv[2];
        const bool showLog = scenario == "log-visible" || scenario == "menu-log-visible";
        QTimer::singleShot(0, window, [&] {
            if (showLog)
            {
                auto *log = window->findChild<QObject *>("logWindow");
                require(log && QMetaObject::invokeMethod(log, "showAndRaise"), "exit fixture could not open the log window");
            }
            if (scenario == "settings-visible" || scenario == "editor-visible" || scenario == "editor-dirty")
            {
                auto *auxiliary = window->findChild<QObject *>(scenario == "settings-visible" ? "settingsWindow" : "customToolWindow");
                require(auxiliary && QMetaObject::invokeMethod(auxiliary, "openDialog"), "cannot open modal auxiliary window");
                if (scenario == "editor-dirty") require(controller.toolEditor()->addTool("退出契约"), "cannot create dirty editor");
            }
            std::cout << "REQUEST_WINDOW_CLOSE log=" << showLog << std::endl;
            if (scenario == "menu-log-visible")
                controller.requestClose();
            else
                window->close();
            QTimer::singleShot(500, &application, [&] {
                std::cout << "AFTER_CLOSE main_visible=" << window->isVisible()
                          << " approved=" << window->property("closingApproved").toBool()
                          << " closing=" << controller.isClosing() << std::endl;
            });
        });
        return application.exec();
    }

    QCoreApplication application(argc, argv);
    for (const auto *scenario : {"log-hidden", "log-visible", "menu-log-visible", "settings-visible", "editor-visible", "editor-dirty"})
    {
        QProcess child;
        child.setProcessChannelMode(QProcess::MergedChannels);
        child.start(QCoreApplication::applicationFilePath(), {"--close-child", scenario});
        require(child.waitForStarted(5000), "could not start the exit child process");
        const bool finished = child.waitForFinished(10000);
        const auto output = child.readAll();
        if (!finished)
        {
            child.kill();
            child.waitForFinished();
        }
        require(finished, std::string("closing the main window left its process running (") + scenario + "):\n"
                              + output.toStdString());
        require(child.exitStatus() == QProcess::NormalExit && child.exitCode() == 0,
                std::string("exit child failed (") + scenario + "):\n" + output.toStdString());
        std::cout << "EXIT_PASS " << scenario << '\n' << output.toStdString();
    }
    return 0;
}
catch (const std::exception &error)
{
    std::cerr << error.what() << '\n';
    return 1;
}
