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
    const auto first = readback(engine);
    const auto stage = *first.readback;
    require(stage.round == 1 && first.nodes[0].node == node && calls == 1, "first read stage has incorrect identity");
    require(!first.completion && engine.state() == RuntimeState::RunningLoop, "read stage became Idle or completed");
    std::this_thread::sleep_for(40ms);
    require(calls == 1, "loop dispatched another writer before the reader acknowledged");
    require(std::get<Array>(first.nodes[0].snapshot->outputs.at("value")).get<NestedVector<std::int64_t, 1>>()[0] == 1,
            "published native storage changed while readers held the stage");
    auto stale = stage;
    ++stale.round;
    require(!engine.acknowledgeReadback(stale), "runtime accepted another round's read acknowledgement");
    require(engine.acknowledgeReadback(stage), "runtime rejected current read acknowledgement");
    const auto second = readback(engine);
    require(second.readback->round == 2 && calls == 2,
            "next round did not execute");
    require(!engine.acknowledgeReadback(stage), "runtime accepted a stale read acknowledgement");
    engine.requestStop();
    std::this_thread::sleep_for(30ms);
    require(!engine.pollLatest().completion && calls == 2, "Stop released tool instances before readers drained");
    require(engine.acknowledgeReadback(*second.readback), "stopping round rejected read completion");
    const auto stopped = finish(engine);
    require(calls == 2, "Stop dispatched another round");
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
        readback(closing);
    }
    std::cout << "Read-safe stages gate loop/once/node dispatch, preserve Stop and include reader time\n";
}
catch (const std::exception &error)
{
    std::cerr << error.what() << '\n';
    return 1;
}
