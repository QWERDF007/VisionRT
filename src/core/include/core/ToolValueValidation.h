#pragma once

#include "core/Export.h"

#include "core/ToolDefinition.h"

#include <string>
#include <vector>

namespace visionrt {

VISIONRT_CORE_API void validateToolValue(const PortDefinition &port, const PortValue &value);
VISIONRT_CORE_API void validateToolOutput(const PortDefinition &port, const PortValue &value, const ToolStatus &status);
VISIONRT_CORE_API void validateToolValues(const std::vector<PortDefinition> &ports, const Values &values);

// Materialises every omitted business output with its declared default, validates the written
// values and the detail, and derives the execution ToolStatus without rewriting messages.
VISIONRT_CORE_API ToolStatus validateToolExecution(const std::vector<PortDefinition> &ports, Values &outputs,
                                 const std::string &detail);

} // namespace visionrt
