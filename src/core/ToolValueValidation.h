#pragma once

#include "core/ToolDefinition.h"

#include <string>
#include <vector>

namespace visionrt {

void validateToolValue(const PortDefinition &port, const PortValue &value);
void validateToolOutput(const PortDefinition &port, const PortValue &value, const ToolStatus &status);
void validateToolValues(const std::vector<PortDefinition> &ports, const Values &values);

// Validates all declared ports in outputs (including the two status ports) and detail.
// Derives and returns the execution ToolStatus without rewriting messages.
ToolStatus validateToolExecution(const std::vector<PortDefinition> &ports, const Values &outputs,
                                 const std::string &detail);

} // namespace visionrt
