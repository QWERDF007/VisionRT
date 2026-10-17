#include "visionrt/sdk/Contract.h"
#include <algorithm>

namespace visionrt {

void validateToolValues(const std::vector<PortContract>& contracts, const Values& values, bool allowMissing) {
    for (const auto& contract : contracts) {
        const auto it = values.find(contract.id);
        if (it == values.end()) {
            if (contract.required && !allowMissing) {
                throw ContractError("Required port value missing: " + contract.id);
            }
            continue;
        }
        if (isMissing(it->second)) {
            if (contract.required && !allowMissing) {
                throw ContractError("Port value is Missing: " + contract.id);
            }
            continue;
        }
        validateValue(contract.type, it->second);
    }
}

} // namespace visionrt
