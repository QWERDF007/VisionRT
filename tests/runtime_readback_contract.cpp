#include "core/ProjectEditor.h"
#include "runtime/RuntimeEngine.h"

#include <atomic>
#include <chrono>
#include <iostream>
#include <thread>

using namespace visionrt;
using namespace std::chrono_literals;

namespace {
void require(bool condition, const char *message)
{
    if (!condition)
        throw std::runtime_error(message);
}

class Counter final : public IToolInstance
{
public:
    explicit Counter(std::atomic<int> &calls)
        : calls_(calls)
    {
    }

    ToolStatus initialize(const Values &) override
    {
        return {};
    }

    ToolStatus release() override
    {
        return {};
    }

    ToolResult execute(const Values &) override
    {
        values_.get<NestedVector<std::int64_t, 1>>()[0] = ++calls_;
        return {{}, {{"value", values_}}};
    }

private:
    std::atomic<int> &calls_;
    Array             values_{NestedVector<std::int64_t, 1>{0}};
};

RuntimeUpdate readback(RuntimeEngine &engine)
{
    const auto deadline = std::chrono::steady_clock::now() + 3s;
    while (std::chrono::steady_clock::now() < deadline)
    {
        auto update = engine.pollLatest();
        if (update.readback)
            return update;
        std::this_thread::sleep_for(1ms);
    }
    throw std::runtime_error("runtime did not enter its read-safe stage");
}

RuntimeUpdate finish(RuntimeEngine &engine)
{
    const auto deadline = std::chrono::steady_clock::now() + 3s;
    while (std::chrono::steady_clock::now() < deadline)
    {
        auto update = engine.pollLatest();
        if (update.completion)
        {
            require(engine.acknowledgeCompletion(*update.completion), "runtime completion rejected");
            return update;
        }
        std::this_thread::sleep_for(1ms);
    }
    throw std::runtime_error("runtime did not complete after read-safe acknowledgement");
}
}

int main()
try
{
    std::atomic<int> calls = 0;
    ToolRegistry     registry;
    ToolDefinition   definition;
    definition.id          = "test.readback";
    definition.displayName = "读取阶段";
    definition.outputs     = {
        {"value", "值", {Type::Array, DType::Int64, 1}}
    };
    registry.addDefinition(definition);
    registry.registerCpp(definition, [&](const ToolServices &) { return std::make_unique<Counter>(calls); });
    ProjectEditor editor;
    const auto    node = editor.addNode("flow_1", *registry.find(definition.id), {});
    RuntimeEngine engine(2);
    engine.setReadbackEnabled(true);
    require(bool(engine.start(editor.project(), registry, RunMode::Loop)), "readback loop did not start");
    std::this_thread::sleep_for(40ms);
    require(calls >= 2, "loop dispatched without waiting for read stage acknowledgement");
    engine.requestStop();
    const auto stoppedRead = readback(engine);
    require(stoppedRead.readback.has_value(), "final read stage missing after stop");
    const auto stage = *stoppedRead.readback;
    auto stale = stage;
    ++stale.round;
    require(!engine.acknowledgeReadback(stale), "runtime accepted another round's read acknowledgement");
    require(engine.acknowledgeReadback(stage), "stopping round rejected read completion");
    const auto stoppedCalls = calls.load();
    const auto stopped = finish(engine);
    require(calls == stoppedCalls, "Stop dispatched another round");
    require(bool(engine.start(editor.project(), registry, RunMode::Once)), "readback once did not start");
    const auto once = readback(engine);
    require(once.readback->session != stage.session && once.readback->round == 1 && !once.completion,
            "once run skipped read stage or reused a session");
    require(!engine.acknowledgeReadback(stage), "another session's acknowledgement released the reader stage");
    require(engine.acknowledgeReadback(*once.readback), "once read acknowledgement rejected");
    finish(engine);
    require(bool(engine.start(editor.project(), registry, RunMode::Node, node)), "single-node readback did not start");
    readback(engine);
    engine.requestStop();
    engine.setReadbackEnabled(false);
    finish(engine);
    require(bool(engine.start(editor.project(), registry, RunMode::Once)), "headless once did not start");
    finish(engine);
    {
        RuntimeEngine closing(2);
        closing.setReadbackEnabled(true);
        require(bool(closing.start(editor.project(), registry, RunMode::Loop)), "closing readback did not start");
        closing.requestStop();
        readback(closing);
    }
    // T-B01: Barrier stops Once/Node completion until ACK even when stopRequested
    {
        engine.setReadbackEnabled(true);
        // 1. Once with Stop while reader holds barrier
        require(bool(engine.start(editor.project(), registry, RunMode::Once)), "Once barrier test failed to start");
        const auto onceBarrier = readback(engine);
        require(onceBarrier.readback.has_value(), "Once reader barrier missing readback");
        engine.requestStop();
        std::this_thread::sleep_for(std::chrono::milliseconds(50));
        require(engine.state() == RuntimeState::Stopping, "Engine should be in Stopping state");
        // Before ACK: start must be rejected, and completion must not be published
        require(!engine.start(editor.project(), registry, RunMode::Once), "Start accepted before readback ACK");
        auto pollBeforeAck = engine.pollLatest();
        require(!pollBeforeAck.completion.has_value(), "Completion published before reader ACKed Once readback");
        require(engine.acknowledgeReadback(*onceBarrier.readback), "Failed to ACK Once readback");
        finish(engine);

        // 2. Node with Stop while reader holds barrier
        require(bool(engine.start(editor.project(), registry, RunMode::Node, node)), "Node barrier test failed to start");
        const auto nodeBarrier = readback(engine);
        require(nodeBarrier.readback.has_value(), "Node reader barrier missing readback");
        engine.requestStop();
        std::this_thread::sleep_for(std::chrono::milliseconds(50));
        require(engine.state() == RuntimeState::Stopping, "Engine should be in Stopping state");
        require(!engine.start(editor.project(), registry, RunMode::Node, node), "Start accepted before node readback ACK");
        auto pollNodeBeforeAck = engine.pollLatest();
        require(!pollNodeBeforeAck.completion.has_value(), "Completion published before reader ACKed Node readback");
        require(engine.acknowledgeReadback(*nodeBarrier.readback), "Failed to ACK Node readback");
        finish(engine);

        // 3. Once with Reset while reader holds barrier
        require(bool(engine.start(editor.project(), registry, RunMode::Once)), "Once reset barrier test failed to start");
        const auto resetBarrier = readback(engine);
        require(resetBarrier.readback.has_value(), "Once reader barrier missing readback for reset");
        const auto callsBeforeReset = calls.load();
        require(bool(engine.requestReset()), "Reset request should be accepted");
        std::this_thread::sleep_for(std::chrono::milliseconds(50));
        require(engine.state() == RuntimeState::Stopping, "Engine should be in Stopping state after requestReset");
        require(!engine.start(editor.project(), registry, RunMode::Once), "Start accepted during barrier before reset ACK");
        require(calls.load() == callsBeforeReset, "In-place tool executed while reader still holds readback barrier");
        auto pollResetBeforeAck = engine.pollLatest();
        require(!pollResetBeforeAck.completion.has_value(), "Completion published before reader ACKed readback during Reset");
        require(engine.acknowledgeReadback(*resetBarrier.readback), "Failed to ACK readback for Reset");
        finish(engine);
    }
    std::cout << "Read-safe stages gate loop/once/node dispatch, preserve Stop and include reader time\n";
}
catch (const std::exception &error)
{
    std::cerr << error.what() << '\n';
    return 1;
}
