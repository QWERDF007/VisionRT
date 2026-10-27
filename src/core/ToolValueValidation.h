#pragma once

#include "core/ToolDefinition.h"

#include <vector>

namespace visionrt {

void validateToolValue(const PortDefinition &port, const PortValue &value);
void validateToolOutput(const PortDefinition &port, const PortValue &value, const ToolStatus &status);
void validateToolValues(const std::vector<PortDefinition> &ports, const Values &values);

// Validates a complete tool output set and returns the status the tool wrote itself. Exactly the
// declared ports must be present, the two status ports included; nothing is synthesised here.
ToolStatus validateToolExecution(const std::vector<PortDefinition> &ports, const Values &outputs);

} // namespace visionrt
