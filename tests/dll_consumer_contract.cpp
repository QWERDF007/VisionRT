#include "fixtures/dll_consumer.h"
#include "log/LogService.h"
#include "core/ToolRegistry.h"
#include "ui/Color.h"
#include "ui/Size.h"
#include "ui/UiTypes.h"

#include <QGuiApplication>
#include <QQmlComponent>
#include <QQmlEngine>
#include <QDate>
#include <QFile>
#include <QTemporaryDir>
#include <QTest>
#include <cassert>
#include <spdlog/spdlog.h>

int main(int argc, char **argv)
{
    QGuiApplication app(argc, argv);
    assert(consumerLogService() == &visionrt::LogService::instance());
    auto registry = consumerRegistry();
    try
    {
        registry->create("missing");
        assert(false);
    }
    catch (const visionrt::RegistryError &error)
    {
        assert(error.reason == visionrt::ToolAvailability::DefinitionMissing);
    }
    registry.reset();
    assert(consumerColor() == visionrt::VisionRTColor::getInstance());
    assert(consumerSize() == visionrt::VisionRTSize::getInstance());
    assert(consumerColor()->metaObject() == &visionrt::VisionRTColor::staticMetaObject);
    visionrt::registerUiTypes();
    QQmlEngine engine;
    QQmlComponent component(&engine);
    component.setData("import QtQml; import VisionRT.Ui 1.0; QtObject { property var palette: VisionRTColor; property var metrics: VisionRTSize }", {});
    assert(component.isReady());
    std::unique_ptr<QObject> object(component.create());
    assert(object);
    assert(object->property("palette").value<QObject *>() == consumerColor());
    assert(object->property("metrics").value<QObject *>() == consumerSize());
    component.setData("import VisionRT 1.0; WorkbenchSeparator {}", {});
    assert(component.isReady());
    object.reset(component.create());
    assert(object);
    auto &service = visionrt::LogService::instance();
    auto *previous = consumerDefaultLogger();
    QTemporaryDir directory;
    assert(directory.isValid());
    for (int cycle = 0; cycle < 2; ++cycle)
    {
        visionrt::LogConfig config;
        config.minimumLevel = visionrt::LogLevel::Warn;
        assert(service.start(config, directory.filePath("visionrt.log")));
        auto logger = service.sharedLogger();
        consumerRegisterLogger(logger);
        assert(consumerDefaultLogger() == logger.get());
        assert(consumerNamedLogger() == logger.get());
        consumerWrite();
        service.setMinimumLevel(visionrt::LogLevel::Info);
        consumerWrite();
        QString text;
        for (int wait = 0; wait < 100 && text.count("dll-service") < 2; ++wait)
        {
            QTest::qWait(10);
            for (const auto &row : service.take(100))
                text += row.line + '\n';
        }
        assert(text.count("dll-info") == 1);
        assert(text.count("dll-warning") == 2);
        assert(text.count("[系统] dll-service") == 2);
        consumerWrite();
        consumerUnregisterLogger();
        assert(consumerDefaultLogger() == previous);
        assert(consumerNamedLogger() == nullptr);
        logger.reset();
        service.shutdown();
        QFile file(directory.filePath("visionrt_" + QDate::currentDate().toString("yyyy-MM-dd") + ".log"));
        assert(file.open(QIODevice::ReadOnly));
        const auto written = QString::fromUtf8(file.readAll());
        assert(written.count("dll-info") == (cycle + 1) * 2);
        assert(written.count("dll-warning") == (cycle + 1) * 3);
    }
}
