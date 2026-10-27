#pragma once

// Test-side view over the values a tool or a node actually produced. The contract has one
// execution channel: the tool writes every declared port, the two status keys included. These
// helpers read that channel instead of a second copy, so a test cannot pass on data the host never
// receives.

#include <visionrt/contracts/Tool.h>

#include <cstddef>
#include <string>
#include <type_traits>

namespace testview {

inline std::int64_t code(const visionrt::ToolResult &result)
{
    return std::get<std::int64_t>(result.outputs.at(std::string(visionrt::kStatusCodePort)));
}

inline std::string message(const visionrt::ToolResult &result)
{
    return std::get<std::string>(result.outputs.at(std::string(visionrt::kStatusMessagePort)));
}

inline visionrt::ToolStatus status(const visionrt::ToolResult &result)
{
    return {code(result), message(result)};
}

// A lifecycle status already is the status itself.
inline const visionrt::ToolStatus &status(const visionrt::ToolStatus &value)
{
    return value;
}

// Anything that carries its own lifecycle status member (a node snapshot, registry availability, a
// registry failure, a load result) is read through that member instead of duplicated.
template<class T>
auto status(const T &value) -> std::decay_t<decltype(value.status)>
{
    return value.status;
}

// Business ports only, i.e. the declared ports minus the two status keys.
inline std::size_t businessCount(const visionrt::ToolResult &result)
{
    return result.outputs.size() - 2;
}

// Runs an execution the contract cannot satisfy. Such a run has no tool answer, so the failure
// surfaces as the exception the host turns into a diagnostic; this returns that message.
template<class Instance>
std::string executionFailure(Instance &instance, visionrt::Values inputs = {})
{
    try
    {
        instance.execute(inputs);
    }
    catch (const std::exception &error)
    {
        return error.what();
    }
    return {};
}

} // namespace testview
