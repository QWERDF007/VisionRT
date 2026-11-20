#pragma once

#include "tools/Export.h"

#include "core/ToolRegistry.h"

namespace visionrt {
// Host merge tool: the execution gate decides which branch is required; execute
// only forwards the already validated value and keeps its original Owner.
VISIONRT_TOOLS_API void registerSelectTools(ToolRegistry &registry);
} // namespace visionrt
