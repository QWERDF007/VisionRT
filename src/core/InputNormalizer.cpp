#include "core/InputNormalizer.h"

#include <algorithm>
#include <cmath>
#include <string>

namespace visionrt {
namespace {

std::string numberText(const PortValue &value)
{
    if (const auto *integer = std::get_if<std::int64_t>(&value))
        return std::to_string(*integer);
    if (const auto *real = std::get_if<double>(&value))
        return std::to_string(*real);
    return {};
}

bool inOptions(const std::vector<PortValue> &options, const PortValue &value)
{
    return std::any_of(options.begin(), options.end(), [&](const auto &option) { return equalValues(option, value); });
}

bool inRange(const PortDefinition &port, const PortValue &value)
{
    if (port.type.type == Type::Int)
    {
        const auto current = std::get<std::int64_t>(value);
        if (port.minimum && current < std::get<std::int64_t>(*port.minimum))
            return false;
        if (port.maximum && current > std::get<std::int64_t>(*port.maximum))
            return false;
        return true;
    }
    const auto current = std::get<double>(value);
    if (port.minimum && current < std::get<double>(*port.minimum))
        return false;
    if (port.maximum && current > std::get<double>(*port.maximum))
        return false;
    return true;
}

void clampNumeric(const PortDefinition &port, PortValue &value, std::string &adjustment)
{
    if (!port.minimum && !port.maximum)
        return;
    if (port.type.type == Type::Int)
    {
        auto &current = std::get<std::int64_t>(value);
        const auto before = current;
        if (port.minimum)
            current = std::max(current, std::get<std::int64_t>(*port.minimum));
        if (port.maximum)
            current = std::min(current, std::get<std::int64_t>(*port.maximum));
        if (current != before)
            adjustment = "原值 " + std::to_string(before) + "，实际输入 " + std::to_string(current);
        return;
    }
    auto &current = std::get<double>(value);
    const auto before = current;
    if (port.minimum)
        current = std::max(current, std::get<double>(*port.minimum));
    if (port.maximum)
        current = std::min(current, std::get<double>(*port.maximum));
    if (current != before)
        adjustment = "原值 " + numberText(PortValue{before}) + "，实际输入 " + numberText(PortValue{current});
}

} // namespace

NormalizedInput normalizeInput(const PortDefinition &port, PortValue value)
{
    NormalizedInput result;
    if (isMissing(value))
    {
        if (!isMissing(port.defaultValue))
        {
            result.value      = port.defaultValue;
            result.adjustment = "使用默认值";
            return result;
        }
        if (port.required)
            throw ContractError("端口 " + port.id + " 缺少必填值");
        result.value = Missing{};
        return result;
    }

    try
    {
        validateValue(port.type, value);
    }
    catch (const ContractError &error)
    {
        const auto actual = typeOf(value);
        throw ContractError("端口 " + port.id + " 的值类型不匹配（期望 " + typeName(port.type.type) + "，实际 "
                            + (actual ? std::string(typeName(actual->type)) : std::string("缺失")) + "）："
                            + error.what());
    }

    if (port.type.type == Type::Float && !std::isfinite(std::get<double>(value)))
        throw ContractError("端口 " + port.id + " 的值必须为有限数值");

    clampNumeric(port, value, result.adjustment);

    if (!port.options.empty() && !inOptions(port.options, value))
    {
        if (!isMissing(port.defaultValue) && inOptions(port.options, port.defaultValue))
        {
            result.value = port.defaultValue;
            result.adjustment += std::string(result.adjustment.empty() ? "" : "；") + "不在允许选项内，回退默认值";
            return result;
        }
        throw ContractError("端口 " + port.id + " 的值不在允许的选项内");
    }

    result.value = std::move(value);
    return result;
}

Values normalizeInputs(const std::vector<PortDefinition> &ports, Values values, std::vector<std::string> *adjustments)
{
    Values normalized;
    for (const auto &port : ports)
    {
        const auto found = values.find(port.id);
        auto       result = normalizeInput(port, found == values.end() ? PortValue{Missing{}} : found->second);
        if (!result.adjustment.empty() && adjustments)
            adjustments->push_back(port.name.empty() ? port.id : port.name + "：" + result.adjustment);
        normalized.emplace(port.id, std::move(result.value));
    }
    return normalized;
}

} // namespace visionrt
