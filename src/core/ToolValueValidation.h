#pragma once

#include "core/ToolDefinition.h"

#include <vector>

namespace visionrt {

void validateToolValue(const PortDefinition &port, const PortValue &value);
void validateToolValues(const std::vector<PortDefinition> &ports, const Values &values);

} // namespace visionrt
