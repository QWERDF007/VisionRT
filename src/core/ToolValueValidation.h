#pragma once

#include "core/ToolDefinition.h"

#include <string>
#include <vector>

namespace visionrt {

void validateToolValue(const PortDefinition &port, const PortValue &value);
void validateToolOutput(const PortDefinition &port, const PortValue &value, const ToolStatus &status);
void validateToolValues(const std::vector<PortDefinition> &ports, const Values &values);

// Materialises every omitted business output with its declared default, validates the written
// values and the detail, and derives the execution ToolStatus without rewriting messages.
ToolStatus validateToolExecution(const std::vector<PortDefinition> &ports, Values &outputs,
                                 const std::string &detail);

} // namespace visionrt
