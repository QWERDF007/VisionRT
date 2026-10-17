#pragma once

#include "visionrt/sdk/Tool.h"
#include <vector>

namespace visionrt {

struct PortContract {
    std::string id;
    TypeSpec type;
    bool required = true;
};

struct ToolContract {
    std::string toolId;
    std::string registryName;
    std::vector<PortContract> initParams;
    std::vector<PortContract> inputs;
    std::vector<PortContract> outputs;
};

VT_EXPORT void validateToolValues(const std::vector<PortContract>& contracts, const Values& values, bool allowMissing);

} // namespace visionrt
