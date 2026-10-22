#pragma once

#include "core/ToolDefinition.h"

#include <vector>

namespace visionrt {

void validateToolValue(const PortDefinition &port, const PortValue &value);
void validateToolOutput(const PortDefinition &port, const PortValue &value, const ToolStatus &status);
void validateToolValues(const std::vector<PortDefinition> &ports, const Values &values);

} // namespace visionrt
