#pragma once

#include "visionrt/contracts/Tool.h"
#include <optional>
#include <vector>

namespace visionrt {

struct PortDefinition;

struct PortContract {
    std::string id;
    TypeSpec type;
    bool required = true;
    std::vector<PortValue> options;
    std::optional<PortValue> minimum;
    std::optional<PortValue> maximum;
};

PortContract portContractFromDefinition(const PortDefinition& port);

void validateToolValues(const std::vector<PortContract>& contracts, const Values& values, bool allowMissing);

} // namespace visionrt
