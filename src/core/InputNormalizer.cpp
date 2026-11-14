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

NormalizedInput normalizeInput(const PortDefinition &port, PortValue value, InputOrigin origin)
{
    NormalizedInput result;
    if (isMissing(value))
    {
        // An upstream port that produced nothing stays empty: filling it with the declaration's
        // manual default would present data the upstream never published.
        if (origin == InputOrigin::Upstream)
        {
            result.value = Missing{};
            return result;
        }
        // Every value-carrying port already holds its explicit or type default; only a host status
        // port stays empty, and an empty status is a normal state rather than an author error.
        if (!isMissing(port.defaultValue))
        {
            result.value      = portDefault(port);
            result.adjustment = "使用默认值";
            return result;
        }
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
        throw ContractError(portLabel(port.id) + " 的值类型不匹配（期望 " + typeName(port.type.type) + "，实际 "
                            + (actual ? std::string(typeName(actual->type)) : std::string("缺失")) + "）："
                            + error.what());
    }

    if (port.type.type == Type::Float && !std::isfinite(std::get<double>(value)))
        throw ContractError(portLabel(port.id) + " 的值必须为有限数值");

    clampNumeric(port, value, result.adjustment);

    // An enumerated value is either one of the declared options or an error. Replacing it with the
    // default would turn a rejected choice into a successful run that used another value.
    if (!port.options.empty() && !inOptions(port.options, value))
        throw ContractError(portLabel(port.id) + " 的值不在允许的选项内");

    result.value = std::move(value);
    return result;
}

Values normalizeInputs(const std::vector<PortDefinition> &ports, Values values,
                       std::vector<std::string> *adjustments, const std::set<PortId> *upstreamPorts)
{
    Values normalized;
    for (const auto &port : ports)
    {
        const bool upstream = upstreamPorts && upstreamPorts->contains(port.id);
        const auto found    = values.find(port.id);
        auto       result   = normalizeInput(port, found == values.end() ? PortValue{Missing{}} : found->second,
                                             upstream ? InputOrigin::Upstream : InputOrigin::Manual);
        if (!result.adjustment.empty() && adjustments)
            adjustments->push_back(port.nameZh.empty() ? portLabel(port.id) : port.nameZh + "：" + result.adjustment);
        normalized.emplace(port.id, std::move(result.value));
    }
    return normalized;
}

} // namespace visionrt
