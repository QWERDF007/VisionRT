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
                                 const ToolStatus &status)
{
    if (outputs.contains(std::string(kStatusCodePort)) || outputs.contains(std::string(kStatusMessagePort)))
        throw ContractError("工具不得写入状态端口，状态由宿主物化");

    std::size_t business = 0;
    for (const auto &port : ports)
    {
        if (isStatusPort(port.id))
            continue;
        const auto found = outputs.find(port.id);
        if (found == outputs.end())
            throw ContractError("缺少输出端口：" + port.id);
        validateToolOutput(port, found->second, status);
        ++business;
    }
    if (outputs.size() != business)
        throw ContractError("输出端口与声明不一致");

    validateUtf8(status.message);
    validateUtf8(status.detail);
    return status;
}

ToolStatus materialiseToolStatus(Values &outputs, ToolStatus status)
{
    if (status.message.empty() && status.code == 0)
        status.message = "运行成功";
    outputs.insert_or_assign(std::string(kStatusCodePort), status.code);
    outputs.insert_or_assign(std::string(kStatusMessagePort), status.message);
    return status;
}

} // namespace visionrt
