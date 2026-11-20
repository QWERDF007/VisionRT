#include "runtime/Receiver.h"
#include "tools/ReceiveTool.h"
#include "core/NodeContracts.h"

#include <algorithm>

namespace visionrt {
namespace {
// The receiving tool owns the business policy for a terminal source. The
// transport only reports what actually happened and which value exists.
ToolResult receiveResult(const ReceiveOutcome &outcome)
{
    Values outputs;
    outputs[ReceivePorts::SourceStatus]        = outcome.sourceStatus.code;
    outputs[ReceivePorts::SourceStatusMessage] = std::string(outcome.sourceStatus.message);
    outputs[ReceivePorts::Value]               = outcome.value;
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
        const auto flow = parameters.find(ReceiveInitPorts::SourceFlow);
        const auto node = parameters.find(ReceiveInitPorts::SourceNode);
        const auto port = parameters.find(ReceiveInitPorts::SourcePort);
        if (flow == parameters.end() || node == parameters.end() || port == parameters.end())
        {
            return {1, "接收声明需要 source_flow_id、source_node_seq 与 source_port_id"};
        }
        request_.flowId       = std::get<std::string>(flow->second);
        request_.nodeSequence = std::get<std::string>(node->second);
        request_.port         = static_cast<PortId>(std::get<std::int64_t>(port->second));
        const auto timeout    = std::get<std::int64_t>(parameters.at(ReceiveInitPorts::Timeout));
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
                                 [](const ToolDefinition &definition, const HostToolContext &host)
                                 {
                                     const auto output
                                         = std::find_if(definition.outputs.begin(), definition.outputs.end(),
                                                        [](const auto &port) { return port.id == ReceivePorts::Value; });
                                     if (output == definition.outputs.end())
                                     {
                                         throw ContractError("接收工具缺少 value 输出端口：" + definition.nameEn);
                                     }
                                     return std::make_unique<ReceiveTool>(output->type, host.results);
                                 });
}

} // namespace visionrt
