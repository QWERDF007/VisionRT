#include "core/ToolValueValidation.h"

#include <algorithm>
#include <cmath>
#include <set>
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

void validateToolValue(const PortDefinition &port, const PortValue &value, ValueCompleteness completeness)
{
    if (isMissing(value))
        return;
    try
    {
        validateValue(port.type, value);
    }
    catch (const ContractError &error)
    {
        throw ContractError(port.nameZh + " 的值类型不匹配（期望 " + typeName(port.type.type) + "，实际 "
                            + typeName(typeOf(value)->type) + "）：" + error.what());
    }

    if (!port.options.empty()
        && std::none_of(port.options.begin(), port.options.end(),
                        [&](const auto &option) { return equalValues(option.value, value); }))
    {
        throw ContractError(port.nameZh + " 的值不在允许的选项内");
    }

    if (isShapeType(port.type.type) && (port.minRegions || port.maxRegions))
    {
        const auto &groups = *shapeRegions(value);
        if (groups.empty() && port.minRegions > 0 && completeness == ValueCompleteness::Complete)
            throw ContractError(port.nameZh + " 的区域数量少于声明下限");
        for (const auto &group : groups)
        {
            if (completeness == ValueCompleteness::Complete && group.size() < static_cast<std::size_t>(port.minRegions))
                throw ContractError(port.nameZh + " 的区域数量少于声明下限");
            if (port.maxRegions && group.size() > static_cast<std::size_t>(port.maxRegions))
                throw ContractError(port.nameZh + " 的区域数量超过声明上限");
        }
    }

    if (port.minimum || port.maximum)
    {
        if (port.type.type == Type::Float && !std::isfinite(std::get<double>(value)))
        {
            throw ContractError(port.nameZh + " 的值必须为有限数值");
        }
        if (port.minimum && lessNumber(value, *port.minimum, port.type.type))
        {
            throw ContractError(port.nameZh + " 的值 " + numberText(value) + " 小于最小值 "
                                + numberText(*port.minimum));
        }
        if (port.maximum && lessNumber(*port.maximum, value, port.type.type))
        {
            throw ContractError(port.nameZh + " 的值 " + numberText(value) + " 大于最大值 "
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

ToolStatus validateToolExecution(const std::vector<PortDefinition> &ports, Values &outputs,
                                 const std::string &detail, ProducedBy phase)
{
    const auto codeIt = outputs.find(kStatusCodePort);
    if (codeIt == outputs.end())
        throw ContractError("缺少状态码端口：" + portLabel(kStatusCodePort));
    const auto messageIt = outputs.find(kStatusMessagePort);
    if (messageIt == outputs.end())
        throw ContractError("缺少状态消息端口：" + portLabel(kStatusMessagePort));

    if (!std::holds_alternative<std::int64_t>(codeIt->second))
        throw ContractError("状态码端口类型必须为整数");
    if (!std::holds_alternative<std::string>(messageIt->second))
        throw ContractError("状态消息端口类型必须为字符串");

    const auto code = std::get<std::int64_t>(codeIt->second);
    const auto &message = std::get<std::string>(messageIt->second);

    validateUtf8(message);
    validateUtf8(detail);

    ToolStatus status{code, message, detail};

    // The one completion point for a tool answer: every declared business output must be published.
    // The host never fills one from the declaration, so a successful status can never disguise a
    // result the tool did not produce.
    std::set<PortId> declared;
    for (const auto &port : ports)
    {
        declared.insert(port.id);
        if (isReservedPort(PortStage::Outputs, port.id))
            continue;
        if (port.producedBy != ProducedBy::Both && port.producedBy != phase)
        {
            outputs.insert_or_assign(port.id, Missing{});
            continue;
        }
        const auto found = outputs.find(port.id);
        if (found == outputs.end() || isMissing(found->second))
        {
            // A failing invocation may leave a business port empty. Keeping it empty — instead of
            // substituting the declared default — is what lets a downstream gate tell "no value was
            // produced" from a real false/zero value.
            if (!status)
            {
                outputs.insert_or_assign(port.id, Missing{});
                continue;
            }
            throw ContractError("工具未发布输出端口：" + port.nameZh);
        }
        validateToolOutput(port, found->second, status);
    }
    for (const auto &[id, value] : outputs)
        if (!declared.contains(id))
            throw ContractError("工具发布了未声明的输出端口：" + portLabel(id));

    return status;
}

} // namespace visionrt
