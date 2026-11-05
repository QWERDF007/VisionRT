#pragma once

#include "core/ToolDefinition.h"

#include <optional>
#include <set>
#include <string>
#include <vector>

namespace visionrt {

// Where the value came from decides what a Missing value means. A manual value that was never
// filled may take the port's declared default; an upstream result that produced nothing must stay
// missing, because substituting the manual default would invent data the upstream did not publish.
enum class InputOrigin
{
    Manual,
    Upstream
};

struct NormalizedInput
{
    PortValue   value = Missing{};
    std::string adjustment; // 本次调用的轻量诊断；未调整时为空

    bool operator==(const NormalizedInput &) const = default;
};

// 唯一的输入规范化实现：手动字面量、运行时上游值和声明默认值走同一条规则。
// 不写回上游结果，不复制图像/ROI Owner，只产生本次调用可用的合法值。
NormalizedInput normalizeInput(const PortDefinition &port, PortValue value,
                               InputOrigin origin = InputOrigin::Manual);

// 规范化一组输入。缺失端口按 Missing 处理；required 且无默认值时抛 ContractError。
// adjustments（可选）按端口顺序收集本次发生的调整说明；upstreamPorts 指出哪些端口的值来自上游。
Values normalizeInputs(const std::vector<PortDefinition> &ports, Values values,
                       std::vector<std::string> *adjustments = nullptr,
                       const std::set<PortId> *upstreamPorts = nullptr);

} // namespace visionrt
