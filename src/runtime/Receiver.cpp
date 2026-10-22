#include "runtime/RuntimeInternal.h"

#include <algorithm>
#include <array>
#include <charconv>

namespace visionrt {
namespace {
ToolResult receiveError(std::int64_t code, std::string message)
{
    return {
        {code, std::move(message)},
        {{"value", Missing{}}, {"source_status_code", Missing{}}, {"source_status_message", Missing{}}}
    };
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
        // Legacy declarations used source_flow_name/source_node_name. Read the
        // stable identity fields; fall back to the legacy names only so old
        // packages produce a precise message instead of a parameter error.
        const auto flow = parameters.find("source_flow_id");
        const auto node = parameters.find("source_node_seq");
        const auto port = parameters.find("source_port_id");
        if (flow == parameters.end() || node == parameters.end() || port == parameters.end())
        {
            const bool legacy = parameters.contains("source_flow_name")
                || parameters.contains("source_node_name");
            return {1, legacy ? "接收声明使用旧版名称字段，请更新工具包为 source_flow_id/source_node_seq/source_port_id"
                              : "接收声明需要 source_flow_id、source_node_seq 与 source_port_id"};
        }
        request_.flowId       = std::get<Utf8>(flow->second).str();
        request_.nodeSequence = std::get<Utf8>(node->second).str();
        request_.port         = std::get<Utf8>(port->second).str();
        const auto timeout    = std::get<std::int64_t>(parameters.at("timeout_ms"));
        if (timeout < 0)
            return {1, "timeout_ms 必须为非负整数"};
        request_.timeout = std::chrono::milliseconds(timeout);
        cursor_          = {};
        return {};
    }

    ToolResult execute(const Values &) override
    {
        return reader_->receive(request_, cursor_);
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
                                         throw ContractError("receiver value output is missing: " + definition.id);
                                     }
                                     return std::make_unique<ReceiveTool>(output->type, services.results);
                                 });
}

namespace runtime_detail {
ToolResult NodeReader::receive(const ReceiveRequest &request, ReceiveCursor &cursor)
{
    using Clock        = std::chrono::steady_clock;
    const auto started = Clock::now();
    if (request.timeout.count() < 0)
        return receiveError(1, "timeout_ms 必须为非负整数");
    const auto remaining = std::chrono::duration_cast<std::chrono::milliseconds>(Clock::time_point::max() - started);
    if (request.timeout > remaining)
        return receiveError(1, "timeout_ms 超出单调时钟可表示范围");
    const auto deadline = started + request.timeout;
    const auto context  = context_.lock();
    if (!context)
        return receiveError(1, "没有活动的接收调用");
    if (request.nodeSequence.empty()
        || !std::all_of(request.nodeSequence.begin(), request.nodeSequence.end(),
                        [](char character) { return character >= '0' && character <= '9'; }))
        return receiveError(1, "source_node_seq 必须是无符号十进制文本");
    std::uint64_t sourceNode = 0;
    const auto    parsed     = std::from_chars(request.nodeSequence.data(),
                                                request.nodeSequence.data() + request.nodeSequence.size(), sourceNode);
    if (parsed.ec != std::errc{} || parsed.ptr != request.nodeSequence.data() + request.nodeSequence.size())
        return receiveError(1, "source_node_seq 超出 uint64 范围");
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
        return receiveError(1, "源流程不存在：" + request.flowId);
    if (sourceFlow == ownFlow)
        return receiveError(1, "跨流程接收需要不同流程；同一流程内请使用连线绑定");
    const NodeDefinition *source = nullptr;
    for (const auto &node : sourceFlow->nodes)
        if (node.id == sourceNode)
        {
            source = &node;
            break;
        }
    if (!source)
        return receiveError(1, "源节点不存在：" + request.nodeSequence);
    const auto sourceType = source->signatures.outputs.find(request.port);
    if (sourceType == source->signatures.outputs.end() || sourceType->second != request.type)
        return receiveError(2, "源输出端口类型与接收器不一致");
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
        cursor.publication                   = snapshot->publication;
        auto result                          = receiveError(3, "源节点执行失败：" + snapshot->status.message);
        result.outputs["source_status_code"] = snapshot->status.code;
        result.outputs["source_status_message"] = Utf8(snapshot->status.message);
        if (!snapshot->status)
            return result;
        const auto output = snapshot->outputs.find(request.port);
        if (output == snapshot->outputs.end() || isMissing(output->second))
        {
            result.status = {2, "源节点成功但输出端口没有值"};
            return result;
        }
        try
        {
            validateValue(request.type, output->second);
        }
        catch (const ContractError &error)
        {
            result.status = {2, error.what()};
            return result;
        }
        result.status           = {};
        result.outputs["value"] = output->second;
        return result;
    };
    const auto stop = context->stop ? context->stop->get_token() : std::stop_token{};
    if (stop.stop_requested())
        return receiveError(kCancelledStatus, "运行已取消");
    auto snapshot = slot.latest();
    if (snapshot && fresh(*snapshot))
        return take(snapshot);
    if (request.timeout.count() == 0 || Clock::now() >= deadline)
        return receiveError(4, "等待本次会话的新结果超时");
    if (!budget_->acquire())
    {
        snapshot = slot.latest();
        if (snapshot && fresh(*snapshot))
            return take(snapshot);
        return receiveError(5, "接收等待容量已耗尽，请减少同时等待的接收器或增加工作线程");
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
        return receiveError(kCancelledStatus, "运行已取消");
    if (!snapshot)
        return receiveError(4, "等待本次会话的新结果超时");
    return take(snapshot);
}
} // namespace runtime_detail
} // namespace visionrt
