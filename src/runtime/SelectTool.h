#pragma once

#include "core/ToolRegistry.h"

namespace visionrt {
// Host merge tool: the execution gate decides which branch is required; execute
// only forwards the already validated value and keeps its original Owner.
void registerSelectTools(ToolRegistry &registry);
} // namespace visionrt
