#include "ui/AppController.h"
#include "ui/ImageViewport.h"
#include <QCommandLineParser>
#include <QGuiApplication>
#include <QQmlApplicationEngine>
#include <QQmlContext>
#include <QQuickStyle>
#include <QTextStream>
#include <charconv>
#include <thread>
#include <algorithm>
#include <limits>

int main(int argc, char* argv[]) {
    QGuiApplication application(argc, argv);
    QCoreApplication::setApplicationName("VisionRT");
    QCoreApplication::setOrganizationName("VisionRT");
    QCoreApplication::setApplicationVersion("0.1.0");
    QQuickStyle::setStyle("Basic");
    QCommandLineParser parser;
    parser.setApplicationDescription("VisionRT visual workflow workbench");
    parser.addHelpOption();
    parser.addVersionOption();
    parser.addOption({"workers", "Taskflow worker count (positive integer)", "count"});
    parser.addOption({"python-home", "Matching CPython runtime directory", "directory"});
    parser.process(application);
    std::size_t workers = std::max(2u, std::thread::hardware_concurrency());
    if (parser.isSet("workers")) {
        const auto text = parser.value("workers").toLatin1();
        const auto [end, error] = std::from_chars(text.constData(), text.constData() + text.size(), workers);
        if (error != std::errc{} || end != text.constData() + text.size() || workers == 0) {
            QTextStream(stderr) << "--workers requires a positive integer\n";
            return 2;
        }
    }
    if (parser.isSet("python-home"))
        qputenv("VISIONRT_PYTHON_HOME", parser.value("python-home").toUtf8());
    try {
        visionrt::AppController controller(workers, QCoreApplication::applicationDirPath());
        qmlRegisterType<visionrt::ImageViewport>("VisionRT.View", 1, 0, "ImageViewport");
        QQmlApplicationEngine engine;
        engine.addImportPath(QCoreApplication::applicationDirPath() + "/qml");
        engine.rootContext()->setContextProperty("app", &controller);
        QObject::connect(&engine, &QQmlApplicationEngine::objectCreationFailed,
                         &application, [] { QCoreApplication::exit(1); }, Qt::QueuedConnection);
        engine.loadFromModule("VisionRT", "Main");
        return application.exec();
    } catch (const std::exception& error) {
        QTextStream(stderr) << "VisionRT initialization failed: " << error.what() << '\n';
        return 1;
    }
}
