#pragma once

#include "core/Export.h"

#include "core/ToolDefinition.h"

#include <string>
#include <vector>

namespace visionrt {

VISIONRT_CORE_API void validateToolValue(const PortDefinition &port, const PortValue &value);
VISIONRT_CORE_API void validateToolOutput(const PortDefinition &port, const PortValue &value, const ToolStatus &status);
VISIONRT_CORE_API void validateToolValues(const std::vector<PortDefinition> &ports, const Values &values);

// Requires every business output for the invocation phase on success and keeps other outputs Missing.
// Validates values and detail, and derives ToolStatus without rewriting messages.
VISIONRT_CORE_API ToolStatus validateToolExecution(const std::vector<PortDefinition> &ports, Values &outputs,
                                 const std::string &detail, ProducedBy phase = ProducedBy::Execute);

} // namespace visionrt
