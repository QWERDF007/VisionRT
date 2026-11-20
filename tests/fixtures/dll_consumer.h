#pragma once
#include "test_consumer/Export.h"
#include <memory>

namespace visionrt { class LogService; class ToolRegistry; class VisionRTColor; class VisionRTSize; }
namespace spdlog { class logger; }

VISIONRT_TEST_CONSUMER_API visionrt::LogService *consumerLogService();
VISIONRT_TEST_CONSUMER_API void consumerRegisterLogger(const std::shared_ptr<spdlog::logger> &logger);
VISIONRT_TEST_CONSUMER_API void consumerUnregisterLogger();
VISIONRT_TEST_CONSUMER_API spdlog::logger *consumerDefaultLogger();
VISIONRT_TEST_CONSUMER_API spdlog::logger *consumerNamedLogger();
VISIONRT_TEST_CONSUMER_API void consumerWrite();
VISIONRT_TEST_CONSUMER_API std::unique_ptr<visionrt::ToolRegistry> consumerRegistry();
VISIONRT_TEST_CONSUMER_API visionrt::VisionRTColor *consumerColor();
VISIONRT_TEST_CONSUMER_API visionrt::VisionRTSize *consumerSize();
