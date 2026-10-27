#pragma once

#include <cstdint>
#include <string_view>

namespace visionrt {

// Host terminal code for a node the run never executed (skipped/blocked). It is
// deliberately distinct from a business failure and from kCancelledStatus so a
// consumer can tell "not executed" from "ran and failed".
inline constexpr std::int64_t kNotExecutedStatus = -6;

// A tool status message is one text. When a tool puts its curated reason on the first line and the
// implementation text after a line break, presentation shows the reason as the summary and the rest
// as detail. Nothing is rewritten: this only chooses what a row displays.
struct StatusLines
{
    std::string_view summary;
    std::string_view detail;
};

inline StatusLines statusLines(std::string_view message)
{
    const auto split = message.find('\n');
    if (split == std::string_view::npos)
        return {message, {}};
    return {message.substr(0, split), message.substr(split + 1)};
}

} // namespace visionrt
