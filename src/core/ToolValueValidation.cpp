#include "core/ToolValueValidation.h"

#include <algorithm>
#include <cmath>
#include <string>

namespace visionrt {

namespace {

std::string numberText(const PortValue &value)
{
    if (std::holds_alternative<std::int64_t>(value))
    {
        return std::to_string(std::get<std::int64_t>(value));
    }
    return std::to_string(std::get<double>(value));
}

bool lessNumber(const PortValue &left, const PortValue &right, Type type)
{
    if (type == Type::Int)
    {
        return std::get<std::int64_t>(left) < std::get<std::int64_t>(right);
    }
    return std::get<double>(left) < std::get<double>(right);
}

} // namespace

void validateToolValue(const PortDefinition &port, const PortValue &value)
{
    if (isMissing(value))
    {
        if (port.required)
        {
            throw ContractError("端口值缺失：" + port.id);
        }
        return;
    }
    try
    {
        validateValue(port.type, value);
    }
    catch (const ContractError &error)
    {
        throw ContractError("端口 " + port.id + " 的值类型不匹配（期望 " + typeName(port.type.type) + "，实际 "
                            + typeName(typeOf(value)->type) + "）：" + error.what());
    }

    if (!port.options.empty()
        && std::none_of(port.options.begin(), port.options.end(),
                        [&](const auto &option) { return equalValues(option, value); }))
    {
        throw ContractError("端口 " + port.id + " 的值不在允许的选项内");
    }

    if (port.minimum || port.maximum)
    {
        if (port.type.type == Type::Float && !std::isfinite(std::get<double>(value)))
        {
            throw ContractError("端口 " + port.id + " 的值必须为有限数值");
        }
        if (port.minimum && lessNumber(value, *port.minimum, port.type.type))
        {
            throw ContractError("端口 " + port.id + " 的值 " + numberText(value) + " 小于最小值 "
                                + numberText(*port.minimum));
        }
        if (port.maximum && lessNumber(*port.maximum, value, port.type.type))
        {
            throw ContractError("端口 " + port.id + " 的值 " + numberText(value) + " 大于最大值 "
                                + numberText(*port.maximum));
        }
    }
}

void validateToolValues(const std::vector<PortDefinition> &ports, const Values &values)
{
    const PortValue missing = Missing{};
    for (const auto &port : ports)
    {
        const auto found = values.find(port.id);
        validateToolValue(port, found == values.end() ? missing : found->second);
    }
}

void validateToolOutput(const PortDefinition &port, const PortValue &value, const ToolStatus &status)
{
    if (!status && isMissing(value))
        return;
    validateToolValue(port, value);
}

ToolStatus validateToolExecution(const std::vector<PortDefinition> &ports, const Values &outputs,
                                 const std::string &detail)
{
    const auto codeIt = outputs.find(std::string(kStatusCodePort));
    if (codeIt == outputs.end())
        throw ContractError("缺少状态码端口：" + std::string(kStatusCodePort));
    const auto messageIt = outputs.find(std::string(kStatusMessagePort));
    if (messageIt == outputs.end())
        throw ContractError("缺少状态消息端口：" + std::string(kStatusMessagePort));

    if (!std::holds_alternative<std::int64_t>(codeIt->second))
        throw ContractError("状态码端口类型必须为整数");
    if (!std::holds_alternative<std::string>(messageIt->second))
        throw ContractError("状态消息端口类型必须为字符串");

    const auto code = std::get<std::int64_t>(codeIt->second);
    const auto &message = std::get<std::string>(messageIt->second);

    validateUtf8(message);
    validateUtf8(detail);

    ToolStatus status{code, message, detail};

    for (const auto &port : ports)
    {
        const auto found = outputs.find(port.id);
        if (found == outputs.end())
            throw ContractError("缺少输出端口：" + port.id);
        validateToolOutput(port, found->second, status);
    }
    if (outputs.size() != ports.size())
        throw ContractError("输出端口与声明不一致");

    return status;
}

} // namespace visionrt
