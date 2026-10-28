#pragma once

#include "core/ToolDefinition.h"

#include <vector>

namespace visionrt {

void validateToolValue(const PortDefinition &port, const PortValue &value);
void validateToolOutput(const PortDefinition &port, const PortValue &value, const ToolStatus &status);
void validateToolValues(const std::vector<PortDefinition> &ports, const Values &values);

// Validates the business ports a tool returned for one execution. The two status ports are
// host-owned: a tool that writes them, omits a declared port or invents one breaks the contract.
// The tool's own status is carried through unchanged.
ToolStatus validateToolExecution(const std::vector<PortDefinition> &ports, const Values &outputs,
                                 const ToolStatus &status);

// Publishes a validated tool status into the two host-owned ports. This is the only place they are
// written, so every consumer of a tool's answer sees the same two values.
ToolStatus materialiseToolStatus(Values &outputs, ToolStatus status);

} // namespace visionrt
