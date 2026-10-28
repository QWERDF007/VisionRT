#pragma once

#include "core/Error.h"

namespace visionrt {

// Host terminal code for a node the run never executed (skipped/blocked). It is deliberately
// distinct from a business failure and from cancellation so a consumer can tell "not executed"
// from "ran and failed".
inline constexpr std::int64_t kNotExecutedStatus = hostCode(ErrorCode::NotExecuted);

} // namespace visionrt
