#include "dll_consumer.h"
#include "log/LogService.h"
#include <spdlog/spdlog.h>
#include "test_consumer/Logging.h"
#include "core/ToolRegistry.h"
#include "ui/Color.h"
#include "ui/Size.h"

visionrt::LogService *consumerLogService()
{
    return &visionrt::LogService::instance();
}

void consumerRegisterLogger(const std::shared_ptr<spdlog::logger> &logger)
{
    visionrt::test_consumer::registerLogger(logger);
}

void consumerUnregisterLogger()
{
    visionrt::test_consumer::unregisterLogger();
}

spdlog::logger *consumerDefaultLogger() { return spdlog::default_logger_raw(); }
spdlog::logger *consumerNamedLogger() { return spdlog::get("visionrt").get(); }

void consumerWrite()
{
    spdlog::info("dll-info");
    spdlog::warn("dll-warning");
    visionrt::LogService::write(visionrt::LogLevel::Warn, visionrt::LogSource::System, std::string_view("dll-service"));
}

std::unique_ptr<visionrt::ToolRegistry> consumerRegistry()
{
    return std::make_unique<visionrt::ToolRegistry>();
}

visionrt::VisionRTColor *consumerColor() { return visionrt::VisionRTColor::getInstance(); }
visionrt::VisionRTSize *consumerSize() { return visionrt::VisionRTSize::getInstance(); }
