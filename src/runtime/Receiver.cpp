#include "runtime/RuntimeInternal.h"
#include "core/Sequence.h"

#include <algorithm>
#include <array>
#include <functional>
#include <map>

namespace visionrt {
namespace {
// The receiving tool owns the business policy for a terminal source. The
// transport only reports what actually happened and which value exists.
ToolResult receiveResult(const ReceiveOutcome &outcome)
{
    Values outputs;
    outputs["source_status_code"]    = outcome.sourceStatus.code;
    outputs["source_status_message"] = std::string(outcome.sourceStatus.message);
    outputs["value"]                 = outcome.value;
    ToolStatus status;
    switch (outcome.disposition)
    {
    case ReceiveDisposition::Value:
        status = executionSuccess();
        break;
    case ReceiveDisposition::Missing:
        status = {3, outcome.message};
        break;
    case ReceiveDisposition::Timeout:
        status = {4, outcome.message};
        break;
    case ReceiveDisposition::Canceled:
        status = {kCancelledStatus, outcome.message};
        break;
    case ReceiveDisposition::Capacity:
        status = {5, outcome.message};
        break;
    case ReceiveDisposition::ProtocolError:
        status = {1, outcome.message};
        break;
    }
    return toolResult(std::move(outputs), std::move(status));
}

class ReceiveTool final : public IToolInstance
{
public:
    ReceiveTool(TypeSpec type, std::shared_ptr<IResultReader> reader)
        : reader_(std::move(reader))
    {
        request_.type = type;
    }

    ToolStatus initialize(const Values &parameters) override
    {
        if (!reader_)
            return {1, "结果读取通道不可用"};
        const auto flow = parameters.find("source_flow_id");
        const auto node = parameters.find("source_node_seq");
        const auto port = parameters.find("source_port_id");
        if (flow == parameters.end() || node == parameters.end() || port == parameters.end())
        {
            return {1, "接收声明需要 source_flow_id、source_node_seq 与 source_port_id"};
        }
        request_.flowId       = std::get<std::string>(flow->second);
        request_.nodeSequence = std::get<std::string>(node->second);
        request_.port         = std::get<std::string>(port->second);
        const auto timeout    = std::get<std::int64_t>(parameters.at("timeout_ms"));
        if (timeout < 0)
            return {1, "timeout_ms 必须为非负整数"};
        request_.timeout = std::chrono::milliseconds(timeout);
        cursor_          = {};
        return {};
    }

    ToolResult execute(const Values &) override
    {
        return receiveResult(reader_->receive(request_, cursor_));
    }

    ToolStatus release() override
    {
        cursor_ = {};
        return {};
    }

private:
    ReceiveRequest                 request_;
    ReceiveCursor                  cursor_;
    std::shared_ptr<IResultReader> reader_;
};
} // namespace

void registerReceiveTools(ToolRegistry &registry)
{
    registry.registerHostBinding("visionrt.receiver",
                                 [](const ToolDefinition &definition, const PlatformToolServices &services)
                                 {
                                     const auto output
                                         = std::find_if(definition.outputs.begin(), definition.outputs.end(),
                                                        [](const auto &port) { return port.id == "value"; });
                                     if (output == definition.outputs.end())
                                     {
                                         throw ContractError("接收工具缺少 value 输出端口：" + definition.id);
                                     }
                                     return std::make_unique<ReceiveTool>(output->type, services.results);
                                 });
}

namespace runtime_detail {
namespace {
constexpr std::string_view kReceiverBinding = "visionrt.receiver";

const std::string *stringParameter(const NodeDefinition &node, const char *id)
{
    const auto found = node.initParams.find(id);
    return found == node.initParams.end() ? nullptr : std::get_if<std::string>(&found->second);
}

std::string receiverLabel(const FlowDefinition &flow, const NodeDefinition &node)
{
    return flow.name + "/" + node.title();
}
} // namespace

std::optional<ToolStatus> validateReceiverPlan(const FrozenRun &frozen)
{
    std::map<NodeId, const FlowDefinition *> owners;
    for (const auto &flow : frozen.project.flows)
        for (const auto &node : flow.nodes)
            owners.emplace(node.id, &flow);
    struct PlannedReceiver
    {
        const FlowDefinition *flow       = nullptr;
        const NodeDefinition *node       = nullptr;
        const FlowDefinition *sourceFlow = nullptr;
        const NodeDefinition *source     = nullptr;
    };
    std::map<NodeId, PlannedReceiver> receivers;
    for (const auto &[id, contract] : frozen.contracts)
    {
        if (contract.hostBinding != kReceiverBinding)
            continue;
        const auto nodeIt = frozen.nodes.find(id);
        const auto flowIt = owners.find(id);
        if (nodeIt == frozen.nodes.end() || flowIt == owners.end())
            continue;
        const auto *node   = nodeIt->second;
        const auto *flow   = flowIt->second;
        const auto *flowId = stringParameter(*node, "source_flow_id");
        const auto *nodeId = stringParameter(*node, "source_node_seq");
        const auto *portId = stringParameter(*node, "source_port_id");
        if (!flowId || !nodeId || !portId)
            continue;
        NodeId sourceId = 0;
        try
        {
            sourceId = parseSequence(*nodeId, "source_node_seq");
        }
        catch (const ContractError &)
        {
            continue;
        }
        const FlowDefinition *sourceFlow = nullptr;
        for (const auto &candidate : frozen.project.flows)
            if (candidate.id == *flowId)
            {
                sourceFlow = &candidate;
                break;
            }
        if (!sourceFlow || sourceFlow == flow)
            continue;
        const NodeDefinition *source = nullptr;
        for (const auto &candidate : sourceFlow->nodes)
            if (candidate.id == sourceId)
            {
                source = &candidate;
                break;
            }
        if (!source)
            continue;
        const auto sourcePort = source->signatures.outputs.find(*portId);
        const auto valuePort  = node->signatures.outputs.find("value");
        if (sourcePort == source->signatures.outputs.end() || valuePort == node->signatures.outputs.end()
            || sourcePort->second != valuePort->second)
            continue;
        receivers.emplace(id, PlannedReceiver{flow, node, sourceFlow, source});
    }
    for (const auto &[id, plan] : receivers)
    {
        const bool selected = std::find(frozen.selected.begin(), frozen.selected.end(), plan.sourceFlow->id)
                              != frozen.selected.end();
        const bool executable
            = selected && (frozen.mode != RunMode::Node || (frozen.target && plan.source->id == *frozen.target));
        if (!executable)
            return ToolStatus{1, "接收源未在本轮执行：" + plan.sourceFlow->name + "（" + plan.sourceFlow->id + "）/ "
                                     + plan.source->title() + "；接收节点 " + plan.node->title()};
    }
    std::map<NodeId, NodeId> waits;
    for (const auto &[id, plan] : receivers)
        if (receivers.contains(plan.source->id))
            waits.emplace(id, plan.source->id);
    std::map<NodeId, int> state;
    std::vector<NodeId>   stack;
    std::function<std::optional<ToolStatus>(NodeId)> visit = [&](NodeId id) -> std::optional<ToolStatus>
    {
        state[id] = 1;
        stack.push_back(id);
        if (const auto found = waits.find(id); found != waits.end())
        {
            const auto next = found->second;
            if (state[next] == 1)
            {
                std::string path;
                const auto  begin = std::find(stack.begin(), stack.end(), next);
                for (auto item = begin; item != stack.end(); ++item)
                    path += receiverLabel(*receivers.at(*item).flow, *receivers.at(*item).node) + " → ";
                path += receiverLabel(*receivers.at(next).flow, *receivers.at(next).node);
                return ToolStatus{1, "接收等待闭环无法先产出：" + path};
            }
            if (state[next] == 0)
                if (auto error = visit(next))
                    return error;
        }
        stack.pop_back();
        state[id] = 2;
        return std::nullopt;
    };
    for (const auto &[id, plan] : receivers)
        if (state[id] == 0)
            if (auto error = visit(id))
                return error;
    return std::nullopt;
}

ReceiveOutcome NodeReader::receive(const ReceiveRequest &request, ReceiveCursor &cursor)
{
    const auto failure = [](ReceiveDisposition disposition, std::int64_t code, std::string message)
    {
        ReceiveOutcome outcome;
        outcome.disposition  = disposition;
        outcome.sourceStatus = {code, message};
        outcome.message      = std::move(message);
        return outcome;
    };
    using Clock        = std::chrono::steady_clock;
    const auto started = Clock::now();
    if (request.timeout.count() < 0)
        return failure(ReceiveDisposition::ProtocolError, 1, "timeout_ms 必须为非负整数");
    const auto remaining = std::chrono::duration_cast<std::chrono::milliseconds>(Clock::time_point::max() - started);
    if (request.timeout > remaining)
        return failure(ReceiveDisposition::ProtocolError, 1, "timeout_ms 超出单调时钟可表示范围");
    const auto deadline = started + request.timeout;
    const auto context  = context_.lock();
    if (!context)
        return failure(ReceiveDisposition::ProtocolError, 1, "没有活动的接收调用");
    NodeId sourceNode = 0;
    try
    {
        sourceNode = parseSequence(request.nodeSequence, "source_node_seq");
    }
    catch (const ContractError &error)
    {
        return failure(ReceiveDisposition::ProtocolError, 1, error.what());
    }
    const FlowDefinition *sourceFlow = nullptr;
    const FlowDefinition *ownFlow    = nullptr;
    for (const auto &flow : context->frozen->project.flows)
    {
        if (flow.id == request.flowId)
            sourceFlow = &flow;
        for (const auto &node : flow.nodes)
            if (node.id == receiver_)
                ownFlow = &flow;
    }
    if (!sourceFlow)
        return failure(ReceiveDisposition::ProtocolError, 1, "源流程不存在：" + request.flowId);
    if (sourceFlow == ownFlow)
        return failure(ReceiveDisposition::ProtocolError, 1, "跨流程接收需要不同流程；同一流程内请使用连线绑定");
    const NodeDefinition *source = nullptr;
    for (const auto &node : sourceFlow->nodes)
        if (node.id == sourceNode)
        {
            source = &node;
            break;
        }
    if (!source)
        return failure(ReceiveDisposition::ProtocolError, 1, "源节点不存在：" + request.nodeSequence);
    const auto sourceType = source->signatures.outputs.find(request.port);
    if (sourceType == source->signatures.outputs.end() || sourceType->second != request.type)
        return failure(ReceiveDisposition::ProtocolError, 2, "源输出端口类型与接收器不一致");
    if (cursor.session != context->session || cursor.source != source->id)
        cursor = {context->session, source->id, 0};
    const auto fresh = [&](const NodeSnapshot &snapshot)
    {
        return snapshot.generation == context->generation && snapshot.session == context->session
            && snapshot.publication > cursor.publication;
    };
    auto      &slot = context->frozen->runtimes.at(source->id)->slot;
    const auto take = [&](const ResultSlot::Snapshot &snapshot)
    {
        cursor.publication       = snapshot->publication;
        ReceiveOutcome outcome;
        outcome.sourceStatus     = snapshot->status;
        outcome.sourceStatus.detail = snapshot->status.detail;
        const auto output        = snapshot->outputs.find(request.port);
        if (output == snapshot->outputs.end() || isMissing(output->second))
        {
            outcome.disposition = ReceiveDisposition::Missing;
            outcome.message     = snapshot->disposition == NodeDisposition::Skipped
                                    ? "源节点本轮已跳过，没有可用值：" + snapshot->status.message
                                : snapshot->disposition == NodeDisposition::Blocked
                                    ? "源节点本轮被阻塞，没有可用值：" + snapshot->status.message
                                    : "源节点没有发布该端口的可用值：" + snapshot->status.message;
            return outcome;
        }
        try
        {
            validateValue(request.type, output->second);
        }
        catch (const ContractError &error)
        {
            outcome.disposition = ReceiveDisposition::ProtocolError;
            outcome.message     = std::string("源输出值不符合接收声明：") + error.what();
            return outcome;
        }
        outcome.disposition = ReceiveDisposition::Value;
        outcome.value       = output->second;
        return outcome;
    };
    const auto stop = context->stop ? context->stop->get_token() : std::stop_token{};
    if (stop.stop_requested())
        return failure(ReceiveDisposition::Canceled, kCancelledStatus, "运行已取消");
    auto snapshot = slot.latest();
    if (snapshot && fresh(*snapshot))
        return take(snapshot);
    if (request.timeout.count() == 0 || Clock::now() >= deadline)
        return failure(ReceiveDisposition::Timeout, 4, "等待本次会话的新结果超时");
    if (!budget_->acquire())
    {
        snapshot = slot.latest();
        if (snapshot && fresh(*snapshot))
            return take(snapshot);
        return failure(ReceiveDisposition::Capacity, 5, "接收等待容量已耗尽，请减少同时等待的接收器或增加工作线程");
    }

    struct Lease
    {
        WaitBudget &budget;

        ~Lease()
        {
            budget.release();
        }
    } lease{*budget_};

    snapshot = slot.waitUntil(deadline, fresh, stop);
    if (stop.stop_requested())
        return failure(ReceiveDisposition::Canceled, kCancelledStatus, "运行已取消");
    if (!snapshot)
        return failure(ReceiveDisposition::Timeout, 4, "等待本次会话的新结果超时");
    return take(snapshot);
}
} // namespace runtime_detail
} // namespace visionrt
