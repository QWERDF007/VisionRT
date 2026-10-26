#pragma once

#include <cstdint>

namespace visionrt {

// Host terminal code for a node the run never executed (skipped/blocked). It is
// deliberately distinct from a business failure and from kCancelledStatus so a
// consumer can tell "not executed" from "ran and failed".
inline constexpr std::int64_t kNotExecutedStatus = -6;

} // namespace visionrt
