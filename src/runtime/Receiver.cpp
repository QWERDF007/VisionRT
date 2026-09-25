#include "runtime/RuntimeInternal.h"

#include <algorithm>
#include <array>

namespace visionrt {
namespace {
ToolResult receiveError(std::int64_t code, std::string message) {
    return {{code, std::move(message)}, {{"value", Missing{}},
        {"source_status_code", Missing{}}, {"source_status_message", Missing{}}}};
}
class ReceiveTool final : public IToolInstance {
public:
    ReceiveTool(TypeSpec type, std::shared_ptr<IResultReader> reader) : reader_(std::move(reader)) {
        request_.type = type;
    }
    ToolStatus initialize(const Values& parameters) override {
        if (!reader_) return {1, "result directory is unavailable"};
        request_.flowName = std::get<Utf8>(parameters.at("source_flow_name")).str();
        request_.nodeName = std::get<Utf8>(parameters.at("source_node_name")).str();
        request_.port = std::get<Utf8>(parameters.at("source_port_id")).str();
        const auto timeout = std::get<std::int64_t>(parameters.at("timeout_ms"));
        if (timeout < 0) return {1, "timeout_ms must be finite and nonnegative"};
        request_.timeout = std::chrono::milliseconds(timeout);
        cursor_ = {};
        return {};
    }
    ToolResult execute(const Values&) override { return reader_->receive(request_, cursor_); }
    ToolStatus release() override { cursor_ = {}; return {}; }
private:
    ReceiveRequest request_;
    ReceiveCursor cursor_;
    std::shared_ptr<IResultReader> reader_;
};
}
void registerReceiveTools(ToolRegistry& registry) {
    constexpr std::array suffixes{"image", "roi", "int", "float", "bool", "string",
        "float32_ragged2", "int64_ragged2", "string_ragged2", "int64_dense1",
        "int64_dense2", "float64_dense1", "float64_dense2", "string_dense1"};
    for (const auto* suffix : suffixes) {
        const auto id = std::string("visionrt.flow.receive.") + suffix;
        const auto* definition = registry.find(id);
        if (!definition) continue;
        const auto output = std::find_if(definition->outputs.begin(), definition->outputs.end(),
            [](const auto& port) { return port.id == "value"; });
        if (output == definition->outputs.end()) throw ContractError("receiver value output is missing: " + id);
        const auto type = output->type;
        registry.registerCpp(definition->registryName, *definition, [type](const ToolServices& services) {
            return std::make_unique<ReceiveTool>(type, services.results);
        });
    }
}
namespace runtime_detail {
ToolResult NodeReader::receive(const ReceiveRequest& request, ReceiveCursor& cursor) {
    using Clock = std::chrono::steady_clock;
    const auto started = Clock::now();
    if (request.timeout.count() < 0) return receiveError(1, "timeout_ms must be nonnegative");
    const auto remaining = std::chrono::duration_cast<std::chrono::milliseconds>(Clock::time_point::max() - started);
    if (request.timeout > remaining) return receiveError(1, "timeout_ms exceeds the monotonic clock range");
    const auto deadline = started + request.timeout;
    const auto context = context_.lock();
    if (!context) return receiveError(1, "no active receive invocation");
    const FlowDefinition* sourceFlow = nullptr;
    const FlowDefinition* ownFlow = nullptr;
    for (const auto& flow : context->frozen->project.flows) {
        if (flow.name == request.flowName) {
            if (sourceFlow) return receiveError(1, "source flow name is ambiguous");
            sourceFlow = &flow;
        }
        for (const auto& node : flow.nodes) if (node.id == receiver_) ownFlow = &flow;
    }
    if (!sourceFlow) return receiveError(1, "source flow does not exist");
    if (sourceFlow == ownFlow) return receiveError(1, "receive requires a different flow; use a binding within a flow");
    const NodeDefinition* source = nullptr;
    for (const auto& node : sourceFlow->nodes) {
        if (node.title() == request.nodeName) {
            if (source) return receiveError(1, "source node name is ambiguous");
            source = &node;
        }
    }
    if (!source) return receiveError(1, "source node does not exist");
    const auto* definition = context->frozen->registry.find(source->toolId);
    if (!definition) return receiveError(1, "source tool definition is missing");
    std::optional<TypeSpec> sourceType;
    if (request.port == "__status_code") sourceType = TypeSpec{Type::Int};
    else if (request.port == "__status_message") sourceType = TypeSpec{Type::String};
    else for (const auto& port : definition->outputs) if (port.id == request.port) sourceType = port.type;
    if (!sourceType || *sourceType != request.type) return receiveError(2, "source output TypeSpec does not match receiver");
    if (cursor.session != context->session || cursor.source != source->id)
        cursor = {context->session, source->id, 0};
    const auto fresh = [&](const NodeSnapshot& snapshot) {
        return snapshot.generation == context->generation && snapshot.session == context->session &&
               snapshot.publication > cursor.publication;
    };
    auto& slot = context->frozen->runtimes.at(source->id)->slot;
    const auto take = [&](const ResultSlot::Snapshot& snapshot) {
        cursor.publication = snapshot->publication;
        auto result = receiveError(3, "source invocation failed: " + snapshot->status.message);
        result.outputs["source_status_code"] = snapshot->status.code;
        result.outputs["source_status_message"] = Utf8(snapshot->status.message);
        if (!snapshot->status) return result;
        const auto output = snapshot->outputs.find(request.port);
        if (output == snapshot->outputs.end() || isMissing(output->second)) {
            result.status = {2, "successful source invocation has no value"};
            return result;
        }
        try { validateValue(request.type, output->second); }
        catch (const ContractError& error) { result.status = {2, error.what()}; return result; }
        result.status = {};
        result.outputs["value"] = output->second;
        return result;
    };
    auto snapshot = slot.latest();
    if (snapshot && fresh(*snapshot)) return take(snapshot);
    if (request.timeout.count() == 0 || Clock::now() >= deadline)
        return receiveError(4, "receive timed out waiting for a new current-session result");
    if (!budget_->acquire()) {
        snapshot = slot.latest();
        if (snapshot && fresh(*snapshot)) return take(snapshot);
        return receiveError(5, "receive wait capacity exhausted; reduce simultaneous receivers or increase workers");
    }
    struct Lease {
        WaitBudget& budget;
        ~Lease() { budget.release(); }
    } lease{*budget_};
    snapshot = slot.waitUntil(deadline, fresh);
    if (!snapshot) return receiveError(4, "receive timed out waiting for a new current-session result");
    return take(snapshot);
}
} // namespace runtime_detail
} // namespace visionrt
