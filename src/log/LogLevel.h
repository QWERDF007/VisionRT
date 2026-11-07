#pragma once

#include <string_view>

namespace visionrt
{

// Production severity. `Off` is a threshold value only: no message carries it, it rejects every
// new record. The numeric order is the filter order, so a threshold comparison is one comparison.
enum class LogLevel
{
    Trace    = 0,
    Debug    = 1,
    Info     = 2,
    Warn     = 3,
    Error    = 4,
    Critical = 5,
    Off      = 6
};

// Stable keys used by the preferences file and the settings window. They are the storage format,
// so they never follow the UI language; the window owns the translated labels.
std::string_view logLevelKey(LogLevel level) noexcept;
bool             parseLogLevel(std::string_view key, LogLevel &level) noexcept;

} // namespace visionrt
