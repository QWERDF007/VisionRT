#include "core/NodeContracts.h"

#include <algorithm>
#include <charconv>
#include <functional>
#include <limits>
#include <set>

namespace visionrt {
TypeMap resolvedOutputTypes(const ToolDefinition &contract)
{
    TypeMap result{
        {   "__status_code",    {Type::Int}},
        {"__status_message", {Type::String}}
    };
    for (const auto &port : contract.outputs)
    {
        if (port.typeFrom)
            throw ContractError("输出类型尚未解析：" + port.id);
        result.insert_or_assign(port.id, port.type);
    }
    return result;
}

void setOutputDeclarations(NodeDefinition &node, const ToolDefinition &definition)
{
    TypeMap outputs;
    for (const auto *id : {"__status_code", "__status_message"})
        if (const auto previous = node.signatures.outputs.find(id); previous != node.signatures.outputs.end())
            outputs.emplace(id, previous->second);
    node.outputTypeSources.clear();
    for (const auto &port : definition.outputs)
    {
        if (port.typeFrom)
        {
            node.outputTypeSources.emplace(port.id, *port.typeFrom);
            const auto previous = node.signatures.outputs.find(port.id);
            if (previous != node.signatures.outputs.end())
                outputs.emplace(port.id, previous->second);
        }
        else
            outputs.insert_or_assign(port.id, port.type);
    }
    node.signatures.outputs = std::move(outputs);
}

void resolveOutputTypes(ProjectDefinition &project)
{
    struct Resolution
    {
        TypeMap outputs;
        std::map<std::string, std::string, std::less<>> errors;
        const FlowDefinition *flow;
    };
    std::map<NodeId, Resolution> resolutions;
    for (const auto &flow : project.flows)
        for (const auto &node : flow.nodes)
        {
            Resolution resolution{node.signatures.outputs, {}, &flow};
            for (const auto &[port, reference] : node.outputTypeSources)
                resolution.outputs.erase(port);
            if (!resolutions.emplace(node.id, std::move(resolution)).second)
                throw ContractError("Duplicate node ID");
        }
    std::set<std::pair<NodeId, std::string>> resolving;
    std::function<std::optional<TypeSpec>(const NodeDefinition &, const std::string &)> resolve;
    resolve = [&](const NodeDefinition &node, const std::string &port) -> std::optional<TypeSpec>
    {
        auto &resolution = resolutions.at(node.id);
        if (!node.unresolved.isEmpty())
            return {};
        if (const auto found = resolution.outputs.find(port); found != resolution.outputs.end())
            return found->second;
        const auto reference = node.outputTypeSources.find(port);
        if (reference == node.outputTypeSources.end())
            return {};
        auto fail = [&](std::string message) -> std::optional<TypeSpec>
        {
            resolution.errors[port] = std::move(message);
            return {};
        };
        const auto key = std::pair{node.id, port};
        if (!resolving.insert(key).second)
            return fail("Output type reference contains a cycle");
        auto parameter = [&](const std::string &id)
        {
            const auto value = node.initParams.find(id);
            const auto *text = value == node.initParams.end() ? nullptr : std::get_if<Utf8>(&value->second);
            return text ? text->str() : std::string{};
        };
        const auto flowId     = parameter(reference->second.flowParameter);
        const auto nodeSeq    = parameter(reference->second.nodeParameter);
        const auto sourcePort = parameter(reference->second.portParameter);
        std::optional<TypeSpec> type;
        if (flowId.empty() || nodeSeq.empty() || sourcePort.empty())
            fail("需要选择源流程、源节点和输出端口");
        else if (!std::all_of(nodeSeq.begin(), nodeSeq.end(), [](char c) { return c >= '0' && c <= '9'; }))
            fail("源节点序号必须是无符号十进制文本：" + nodeSeq);
        else
        {
            std::uint64_t sourceNode = 0;
            const auto    parsed     = std::from_chars(nodeSeq.data(), nodeSeq.data() + nodeSeq.size(), sourceNode);
            const FlowDefinition *sourceFlow = nullptr;
            for (const auto &flow : project.flows)
                if (flow.id == flowId)
                {
                    sourceFlow = &flow;
                    break;
                }
            if (parsed.ec != std::errc{} || parsed.ptr != nodeSeq.data() + nodeSeq.size())
                fail("源节点序号超出 uint64 范围：" + nodeSeq);
            else if (!sourceFlow || sourceFlow == resolution.flow)
                fail("源流程不存在或就是接收流程：" + flowId);
            else
            {
                const NodeDefinition *source = nullptr;
                for (const auto &candidate : sourceFlow->nodes)
                    if (candidate.id == sourceNode)
                    {
                        source = &candidate;
                        break;
                    }
                if (!source)
                    fail("源节点不存在：" + flowId + "/" + nodeSeq);
                else
                {
                    type = resolve(*source, sourcePort);
                    if (!type)
                        fail("源输出端口不存在或类型无法解析：" + nodeSeq + "." + sourcePort);
                }
            }
        }
        resolving.erase(key);
        if (type)
        {
            resolution.outputs.emplace(port, *type);
            resolution.errors.erase(port);
        }
        return type;
    };
    for (const auto &flow : project.flows)
        for (const auto &node : flow.nodes)
            for (const auto &[port, reference] : node.outputTypeSources)
                resolve(node, port);
    std::set<std::string> changedFlows;
    for (const auto &flow : project.flows)
        for (const auto &node : flow.nodes)
            if (node.signatures.outputs != resolutions.at(node.id).outputs)
            {
                if (node.executionRevision == std::numeric_limits<std::uint64_t>::max()
                    || flow.structureRevision == std::numeric_limits<std::uint64_t>::max())
                    throw ContractError("Output type revision exhausted");
                changedFlows.insert(flow.id);
            }
    for (auto &flow : project.flows)
    {
        for (auto &node : flow.nodes)
        {
            auto &resolution = resolutions.at(node.id);
            if (node.signatures.outputs != resolution.outputs)
            {
                node.signatures.outputs = std::move(resolution.outputs);
                ++node.executionRevision;
                project.dirty = true;
            }
            node.outputTypeErrors = std::move(resolution.errors);
        }
        if (changedFlows.contains(flow.id))
            ++flow.structureRevision;
    }
}

ToolDefinition nodeContract(const NodeDefinition &node, const ToolDefinition &definition)
{
    auto contract = definition;
    for (auto &port : contract.outputs)
        if (port.typeFrom)
        {
            const auto type = node.signatures.outputs.find(port.id);
            if (type == node.signatures.outputs.end())
            {
                const auto error = node.outputTypeErrors.find(port.id);
                throw ContractError("Unresolved output type for " + node.title() + "." + port.id + ": "
                    + (error == node.outputTypeErrors.end() ? "source is not configured" : error->second));
            }
            port.type = type->second;
            port.typeFrom.reset();
        }
    return contract;
}
}
