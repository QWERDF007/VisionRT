#include "core/ToolValueValidation.h"
#include "core/ToolDefinition.h"

#include <algorithm>
#include <cmath>
#include <string>

namespace visionrt {

namespace {

std::string numberText(const PortValue& value) {
    if (std::holds_alternative<std::int64_t>(value)) {
        return std::to_string(std::get<std::int64_t>(value));
    }
    return std::to_string(std::get<double>(value));
}

bool lessNumber(const PortValue& left, const PortValue& right, Type type) {
    if (type == Type::Int) {
        return std::get<std::int64_t>(left) < std::get<std::int64_t>(right);
    }
    return std::get<double>(left) < std::get<double>(right);
}

std::string actualTypeText(const PortValue& value) {
    const auto actual = typeOf(value);
    return actual ? typeName(actual->type) : "Missing";
}

} // namespace

PortContract portContractFromDefinition(const PortDefinition& port) {
    PortContract contract;
    contract.id = port.id;
    contract.type = port.type;
    // A declared default means a missing entry is supplied by the parameter
    // preparation boundary, so the port is not required at the value check.
    contract.required = isMissing(port.defaultValue);
    contract.options = port.options;
    contract.minimum = port.minimum;
    contract.maximum = port.maximum;
    return contract;
}

void validateToolValues(const std::vector<PortContract>& contracts, const Values& values, bool allowMissing) {
    for (const auto& contract : contracts) {
        const auto it = values.find(contract.id);
        if (it == values.end()) {
            // A declared default satisfies a missing entry; only ports without
            // any default are truly required at this boundary.
            if (contract.required && !allowMissing) {
                throw ContractError("缺少必需端口值：" + contract.id);
            }
            continue;
        }
        if (isMissing(it->second)) {
            if (contract.required && !allowMissing) {
                throw ContractError("端口值缺失：" + contract.id);
            }
            continue;
        }
        try {
            validateValue(contract.type, it->second);
        } catch (const ContractError& error) {
            throw ContractError("端口 " + contract.id + " 的值类型不匹配（期望 " +
                typeName(contract.type.type) + "，实际 " + actualTypeText(it->second) + "）：" + error.what());
        }

        if (!contract.options.empty() &&
            std::none_of(contract.options.begin(), contract.options.end(),
                         [&](const auto& option) { return equalValues(option, it->second); })) {
            throw ContractError("端口 " + contract.id + " 的值不在允许的选项内");
        }

        if (contract.minimum || contract.maximum) {
            if (contract.type.type == Type::Float && !std::isfinite(std::get<double>(it->second))) {
                throw ContractError("端口 " + contract.id + " 的值必须为有限数值");
            }
            if (contract.minimum && lessNumber(it->second, *contract.minimum, contract.type.type)) {
                throw ContractError("端口 " + contract.id + " 的值 " + numberText(it->second) +
                    " 小于最小值 " + numberText(*contract.minimum));
            }
            if (contract.maximum && lessNumber(*contract.maximum, it->second, contract.type.type)) {
                throw ContractError("端口 " + contract.id + " 的值 " + numberText(it->second) +
                    " 大于最大值 " + numberText(*contract.maximum));
            }
        }
    }
}

} // namespace visionrt
