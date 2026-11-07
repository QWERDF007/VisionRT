#include "log/LogLevel.h"

namespace visionrt
{

std::string_view logLevelKey(LogLevel level) noexcept
{
    switch (level)
    {
    case LogLevel::Trace: return "trace";
    case LogLevel::Debug: return "debug";
    case LogLevel::Info: return "info";
    case LogLevel::Warn: return "warn";
    case LogLevel::Error: return "error";
    case LogLevel::Critical: return "critical";
    case LogLevel::Off: return "off";
    }
    return "info";
}

bool parseLogLevel(std::string_view key, LogLevel &level) noexcept
{
    for (const auto candidate : {LogLevel::Trace, LogLevel::Debug, LogLevel::Info, LogLevel::Warn,
                                 LogLevel::Error, LogLevel::Critical, LogLevel::Off})
        if (logLevelKey(candidate) == key)
        {
            level = candidate;
            return true;
        }
    return false;
}

} // namespace visionrt
