#include "runtime/Receiver.h"
#include "runtime/RuntimeEngine.h"
#include "core/ProjectEditor.h"
#include "core/ProjectStorage.h"
#include "core/ToolPackage.h"

#include <atomic>
#include <cassert>
#include <chrono>
#include <condition_variable>
#include <functional>
#include <iostream>
#include <mutex>
#include <thread>
#include <QTemporaryDir>

using namespace visionrt;
using namespace std::chrono_literals;

namespace {
class Function final : public IToolInstance
{
public:
    std::function<ToolResult(const Values &)> run;
    std::function<ToolStatus()>               init = []
    {
        return ToolStatus{};
    };
    std::function<ToolStatus()> close = []
    {
        return ToolStatus{};
    };

    ToolStatus initialize(const Values &) override
    {
        return init();
    }

    ToolResult execute(const Values &inputs) override
    {
        return run(inputs);
    }

    ToolStatus release() override
    {
        return close();
    }
};

ToolDefinition definition(std::string id, std::vector<std::string> inputs = {})
{
    ToolDefinition d;
    d.id           = id;

    d.displayName  = id;
    d.version      = "1";
    d.kind         = ToolKind::Cpp;
    for (auto &input : inputs) d.inputs.push_back({input, input, {Type::Int}});
    d.outputs.push_back({"value", "Value", {Type::Int}});
    return d;
}

void install(
    ToolRegistry &registry, const ToolDefinition &d, std::function<ToolResult(const Values &)> run,
    std::function<ToolStatus()> init  = [] { return ToolStatus{}; },
    std::function<ToolStatus()> close = [] { return ToolStatus{}; })
{
    registry.registerCpp(d,
                         [=](const ToolServices &)
                         {
                             auto instance   = std::make_unique<Function>();
                             instance->run   = run;
                             instance->init  = init;
                             instance->close = close;
                             return instance;
                         });
    registry.addDefinition(d);
}

NodeDefinition node(NodeId id, const ToolDefinition &d)
{
    NodeDefinition n;
    n.id               = id;
    n.toolId           = d.id;
    n.toolNameSnapshot = d.displayName;
    for (auto &port : d.inputs)
    {
        n.signatures.inputs.emplace(port.id, port.type);
        n.inputs[port.id] = {};
    }
    for (auto &port : d.outputs) n.signatures.outputs.emplace(port.id, port.type);
    n.signatures.outputs.emplace("__status_code", TypeSpec{Type::Int});
    n.signatures.outputs.emplace("__status_message", TypeSpec{Type::String});
    return n;
}

void bind(NodeDefinition &n, const std::string &input, NodeId source)
{
    n.inputs[input] = {
        BindingMode::Upstream, Missing{},
         SourcePort{source, "value"}
    };
}

RuntimeUpdate finish(RuntimeEngine &engine)
{
    const auto deadline = std::chrono::steady_clock::now() + 10s;
    while (std::chrono::steady_clock::now() < deadline)
    {
        auto update = engine.pollLatest();
        if (update.completion)
        {
            assert(update.state != RuntimeState::Idle);
            assert(!engine.acknowledgeCompletion({update.generation + 1, update.completion->token}));
            assert(engine.acknowledgeCompletion(*update.completion));
            return update;
        }
        std::this_thread::sleep_for(1ms);
    }
    throw std::runtime_error("runtime completion deadline exceeded");
}

ResultSlot::Snapshot snapshot(const RuntimeUpdate &update, NodeId id)
{
    for (auto &item : update.nodes)
        if (item.node == id)
            return item.snapshot;
    throw std::runtime_error("node update absent");
}

void branchesOverlapAndJoin()
{
    RuntimeEngine           engine(2);
    ToolRegistry            registry;
    std::mutex              mutex;
    std::condition_variable arrived;
    int                     active = 0;
    std::atomic<int>        visits = 0;
    auto                    branch = definition("test.branch");
    install(registry, branch,
            [&](const Values &)
            {
                std::unique_lock lock(mutex);
                ++active;
                ++visits;
                arrived.notify_all();
                if (!arrived.wait_for(lock, 2s, [&] { return active == 2; }))
                    return ToolResult{
                        {1, "branches did not overlap"},
                        {{"value", Missing{}}}
                    };
                return ToolResult{{}, {{"value", std::int64_t{7}}}};
            });
    auto join = definition("test.join", {"a", "a_again", "b"});
    install(registry, join,
            [](const Values &in)
            {
                return ToolResult{
                    {},
                    {{"value", std::get<std::int64_t>(in.at("a")) + std::get<std::int64_t>(in.at("a_again"))
                                   + std::get<std::int64_t>(in.at("b"))}}};
            });
    auto a = node(1, branch), b = node(2, branch), c = node(3, join);
    bind(c, "a", 1);
    bind(c, "a_again", 1);
    bind(c, "b", 2);
    ProjectDefinition project;
    project.flows.front().nodes = {a, b, c};
    assert(engine.start(project, registry, RunMode::Once));
    auto result = finish(engine);
    assert(visits == 2);
    assert(snapshot(result, 3)->status.code == 0);
    assert(std::get<std::int64_t>(snapshot(result, 3)->outputs.at("value")) == 21);
    assert(snapshot(result, 1)->round == snapshot(result, 3)->round);
    assert(result.flows.size() == 1 && result.flows.front().milliseconds > 0);
}

void failureReplacesOldValueAndManualUsesCache()
{
    RuntimeEngine engine(2);
    ToolRegistry  registry;
    int           calls  = 0;
    auto          source = definition("test.source");
    install(registry, source,
            [&](const Values &)
            {
                if (++calls == 1)
                    return ToolResult{{}, {{"value", std::int64_t{42}}}};
                return ToolResult{{}, {}};
            });
    auto consumer = definition("test.consumer", {"input"});
    install(registry, consumer,
            [](const Values &in)
            {
                return ToolResult{{},
                                  {{"value", isMissing(in.at("input")) ? std::int64_t{-99}
                                                                       : std::get<std::int64_t>(in.at("input"))}}};
            });
    auto a = node(1, source), b = node(2, consumer);
    bind(b, "input", 1);
    ProjectDefinition project;
    project.flows.front().nodes = {a, b};
    assert(engine.start(project, registry, RunMode::Once));
    auto first = finish(engine);
    assert(std::get<std::int64_t>(snapshot(first, 2)->outputs.at("value")) == 42);
    assert(engine.start(project, registry, RunMode::Node, 2));
    auto manual = finish(engine);
    assert(calls == 1);
    assert(std::get<std::int64_t>(snapshot(manual, 2)->outputs.at("value")) == 42);
    assert(snapshot(manual, 2)->session != snapshot(first, 2)->session);
    assert(engine.start(project, registry, RunMode::Once));
    auto failed = finish(engine);
    assert(snapshot(failed, 1)->status.code == -2);
    assert(std::get<std::int64_t>(snapshot(failed, 1)->outputs.at("__status_code")) == -2);
    assert(isMissing(snapshot(failed, 1)->outputs.at("value")));
    assert(snapshot(failed, 2)->status.code == -2);
    assert(isMissing(snapshot(failed, 2)->outputs.at("value")));
    assert(snapshot(failed, 2)->status.message.find("端口值缺失") != std::string::npos);
    assert(snapshot(failed, 1)->round == snapshot(failed, 2)->round);
    assert(snapshot(failed, 1)->session == snapshot(failed, 2)->session);
}

void initializationSuppressionRetryAndReset()
{
    RuntimeEngine engine(2);
    ToolRegistry  registry;
    int           initializations = 0, releases = 0, executions = 0, rounds = 0;
    bool          fail     = true;
    auto          resource = definition("test.resource");
    resource.initParams.push_back({"resource", "Resource", {Type::Int}, std::int64_t{1}});
    install(
        registry, resource,
        [&](const Values &)
        {
            ++executions;
            return ToolResult{{}, {{"value", std::int64_t{8}}}};
        },
        [&]
        {
            ++initializations;
            return fail ? ToolStatus{7, "resource initialization failed"} : ToolStatus{};
        },
        [&]
        {
            ++releases;
            return ToolStatus{9, "release diagnostic"};
        });
    auto observer = definition("test.observer", {"input"});
    observer.inputs[0].defaultValue = std::int64_t{0};
    observer.inputs[0].required = false;
    install(registry, observer,
            [&](const Values &)
            {
                if (++rounds == 3)
                    engine.requestStop();
                return ToolResult{{}, {{"value", std::int64_t{rounds}}}};
            });
    auto a = node(1, resource), b = node(2, observer);
    bind(b, "input", 1);
    ProjectDefinition project;
    project.dirty               = true;
    project.flows.front().nodes = {a, b};
    assert(engine.start(project, registry, RunMode::Loop));
    auto suppressed = finish(engine);
    assert(initializations == 1 && releases == 1 && executions == 0 && rounds == 3);
    assert(snapshot(suppressed, 1)->status.code == 7);
    assert(snapshot(suppressed, 1)->timing.initMs == 0);
    fail = false;
    assert(engine.start(project, registry, RunMode::Once));
    finish(engine);
    assert(initializations == 2 && executions == 1);
    assert(engine.start(project, registry, RunMode::Once));
    auto reused = finish(engine);
    assert(initializations == 2 && executions == 2);
    project.flows.front().nodes.front().initParams["resource"] = std::int64_t{2};
    assert(engine.start(project, registry, RunMode::Once));
    auto changed = finish(engine);
    assert(initializations == 3 && executions == 3 && releases == 2);
    assert(changed.nodes.front().lifecycleDiagnostic->status.code == -4);
    assert(changed.nodes.front().lifecycleDiagnostic->phase == NodePhase::Release);
    const auto generation = changed.generation;
    assert(engine.requestReset());
    auto reset = finish(engine);
    assert(reset.generation == generation + 1 && !snapshot(reset, 1));
    assert(reset.nodes.front().lifecycleDiagnostic->status.code == -4);
    assert(project.dirty && std::get<std::int64_t>(project.flows.front().nodes.front().initParams.at("resource")) == 2);
    assert(releases == 3);
}

void stopDrainsCommittedFlowsAndFreezesConfiguration()
{
    RuntimeEngine           engine(2);
    ToolRegistry            registry;
    std::mutex              mutex;
    std::condition_variable gate;
    bool                    entered = false, proceed = false;
    std::atomic<int>        fastCalls{0}, slowCalls{0}, wakeups{0};
    engine.setCompletionCallback([&](RuntimeCompletion) { ++wakeups; });
    auto fast = definition("test.fast", {"input"});
    install(registry, fast,
            [&](const Values &in)
            {
                ++fastCalls;
                gate.notify_all();
                return ToolResult{{}, {{"value", in.at("input")}}};
            });
    auto slow = definition("test.slow");
    install(registry, slow,
            [&](const Values &)
            {
                ++slowCalls;
                std::unique_lock lock(mutex);
                entered = true;
                gate.notify_all();
                if (!gate.wait_for(lock, 2s, [&] { return proceed; }))
                    return ToolResult{
                        {1, "test gate timeout"},
                        {{"value", Missing{}}}
                    };
                return ToolResult{{}, {{"value", std::int64_t{1}}}};
            });
    auto a = node(1, fast), b = node(2, slow);
    a.inputs["input"].literal = std::int64_t{17};
    ProjectDefinition project;
    project.flows = {
        {"fast", "fast", true, {a}},
        {"slow", "slow", true, {b}}
    };
    assert(engine.start(project, registry, RunMode::Loop));
    {
        std::unique_lock lock(mutex);
        assert(gate.wait_for(lock, 2s, [&] { return entered && fastCalls == 1; }));
    }
    project.flows.front().nodes.front().inputs["input"].literal = std::int64_t{99};
    engine.requestStop();
    assert(!engine.start(project, registry, RunMode::Once));
    {
        std::lock_guard lock(mutex);
        proceed = true;
    }
    gate.notify_all();
    auto completed = finish(engine);
    assert(completed.round == 1 && fastCalls == 1 && slowCalls == 1);
    assert(std::get<std::int64_t>(snapshot(completed, 1)->outputs.at("value")) == 17);
    assert(completed.flows.size() == 2);
    const auto deadline = std::chrono::steady_clock::now() + 1s;
    while (wakeups == 0 && std::chrono::steady_clock::now() < deadline) std::this_thread::yield();
    assert(wakeups == 1);
}

void receiverCapacityAndSessionIsolation(const std::filesystem::path &toolsRoot)
{
    ToolRegistry registry;
    registry.registerPackage(readToolsPackage(toolsRoot));
    registry.loadNativeImplementations();
    registerReceiveTools(registry);
    auto source = definition("test.sender");
    install(registry, source, [](const Values &) { return ToolResult{{}, {{"value", std::int64_t{25}}}}; });
    auto        sender  = node(1, source);
    const auto *receive = registry.find("visionrt.flow.receive");
    assert(receive);
    auto receiver       = node(2, *receive);
    receiver.initParams = {
        {"source_flow_id",       Utf8("sender")},
        {"source_node_seq", Utf8(std::to_string(sender.id))},
        {  "source_port_id",        Utf8("value")},
        {      "timeout_ms",     std::int64_t{50}}
    };
    ProjectDefinition project;
    project.flows = {
        {  "sender",   "sender",  true,   {sender}},
        {"receiver", "receiver", false, {receiver}}
    };
    RuntimeEngine single(1);
    assert(single.start(project, registry, RunMode::Once));
    finish(single);
    assert(single.start(project, registry, RunMode::Node, 2));
    auto capacity = finish(single);
    assert(snapshot(capacity, 2)->status.code == 5);
    assert(isMissing(snapshot(capacity, 2)->outputs.at("source_status_code")));
    project.flows[1].nodes[0].initParams["timeout_ms"] = std::int64_t{0};
    assert(single.start(project, registry, RunMode::Node, 2));
    assert(snapshot(finish(single), 2)->status.code == 4);
    project.flows[0].runSelected                       = false;
    project.flows[1].runSelected                       = true;
    project.flows[1].nodes[0].initParams["timeout_ms"] = std::int64_t{100};
    auto second                                        = project.flows[1].nodes.front();
    second.id                                          = 3;
    project.flows.push_back({"receiver2", "receiver2", true, {second}});
    RuntimeEngine two(2);
    assert(two.start(project, registry, RunMode::Once));
    auto       limited = finish(two);
    const auto codeA = snapshot(limited, 2)->status.code, codeB = snapshot(limited, 3)->status.code;
    assert((codeA == 4 && codeB == 5) || (codeA == 5 && codeB == 4));
    project.flows[0].runSelected = true;
    project.flows[2].runSelected = false;
    assert(two.start(project, registry, RunMode::Once));
    auto received = finish(two);
    assert(snapshot(received, 2)->status.code == 0);
    assert(std::get<std::int64_t>(snapshot(received, 2)->outputs.at("value")) == 25);
    assert(std::get<std::int64_t>(snapshot(received, 2)->outputs.at("source_status_code")) == 0);
}

void receiverConsumesFailures()
{
    ToolRegistry registry;
    auto         source = definition("test.failed_sender");
    install(registry, source,
            [](const Values &)
            {
                return ToolResult{
                    {12, "源执行失败", "source detail"},
                    {{"value", Missing{}}}
                };
            });
    auto sender   = node(1, source);
    auto consumer = definition("test.cursor");
    registry.registerCpp(consumer,
                         [name = std::to_string(sender.id)](const PlatformToolServices &services)
                         {
                             auto instance = std::make_unique<Function>();
                             instance->run = [reader = services.results, name](const Values &)
                             {
                                 ReceiveCursor  cursor;
                                 ReceiveRequest request{"sender", name, "value", {Type::Int}, 1000ms};
                                 auto           failed = reader->receive(request, cursor);
                                 request.timeout       = 0ms;
                                 auto       next       = reader->receive(request, cursor);
                                 const bool consumed
                                     = failed.status.code == 3 && failed.status.detail == "source detail"
                                    && std::get<std::int64_t>(failed.outputs.at("source_status_code")) == 12
                                    && isMissing(failed.outputs.at("value")) && next.status.code == 4
                                    && isMissing(next.outputs.at("source_status_code"));
                                 return ToolResult{{}, {{"value", std::int64_t{consumed ? 1 : 0}}}};
                             };
                             return instance;
                         });
    registry.addDefinition(consumer);
    ProjectDefinition project;
    project.flows = {
        {  "sender",   "sender", true,            {sender}},
        {"receiver", "receiver", true, {node(2, consumer)}}
    };
    RuntimeEngine engine(2);
    assert(engine.start(project, registry, RunMode::Once));
    auto received = finish(engine);
    assert(std::get<std::int64_t>(snapshot(received, 2)->outputs.at("value")) == 1);
}

void executionDiagnostics()
{
    for (int scenario = 0; scenario < 4; ++scenario)
    {
        RuntimeEngine engine(1);
        ToolRegistry  registry;
        const auto    tool = definition("test.diagnostic");
        install(
            registry, tool,
            [scenario](const Values &) -> ToolResult
            {
                if (scenario == 0)
                    throw std::runtime_error("third-party detail");
                if (scenario == 1)
                    return {{}, {{"value", Utf8("wrong type")}}};
                return {
                    {7, "业务条件未满足", "business detail"},
                    {{"value", std::int64_t{42}}}
                };
            },
            [scenario] { return scenario == 3 ? ToolStatus{6, "资源未准备", "init detail"} : ToolStatus{}; });
        ProjectDefinition project;
        project.flows.front().nodes = {node(1, tool)};
        assert(engine.start(project, registry, RunMode::Once));
        const auto result = snapshot(finish(engine), 1);
        assert(result->status.message.find("节点") == std::string::npos);
        assert(std::get<std::int64_t>(result->outputs.at("__status_code")) == result->status.code);
        assert(std::get<Utf8>(result->outputs.at("__status_message")).str() == result->status.message);
        if (scenario == 0)
        {
            assert(result->phase == NodePhase::Execute && result->status.code == -1);
            assert(result->status.message == "工具执行异常" && result->status.detail == "third-party detail");
        }
        else if (scenario == 1)
        {
            assert(result->phase == NodePhase::Convert && result->status.code == -2);
            assert(!result->status.message.empty() && isMissing(result->outputs.at("value")));
        }
        else if (scenario == 2)
        {
            assert(result->phase == NodePhase::Execute && result->status.code == 7);
            assert(result->status.message == "业务条件未满足" && result->status.detail == "business detail");
        }
        else
        {
            assert(result->phase == NodePhase::Initialize && result->status.code == 6);
            assert(result->status.message == "资源未准备" && result->status.detail == "init detail");
        }
    }
}

void managementWaitsForActiveInvocation()
{
    for (const bool reset : {true, false})
    {
        RuntimeEngine           engine(1);
        ToolRegistry            registry;
        std::mutex              mutex;
        std::condition_variable gate;
        bool                    entered = false, proceed = false, finishedExecution = false;
        std::atomic<bool>       releasedAfterExecution{false};
        auto                    resource = definition("test.reset_active");
        install(
            registry, resource,
            [&](const Values &)
            {
                std::unique_lock lock(mutex);
                entered = true;
                gate.notify_all();
                if (!gate.wait_for(lock, 2s, [&] { return proceed; }))
                    return ToolResult{
                        {1, "test gate timeout"},
                        {{"value", Missing{}}}
                    };
                finishedExecution = true;
                return ToolResult{{}, {{"value", std::int64_t{1}}}};
            },
            [] { return ToolStatus{}; },
            [&]
            {
                std::lock_guard lock(mutex);
                releasedAfterExecution = finishedExecution;
                return ToolStatus{};
            });
        ProjectDefinition project;
        project.flows.front().nodes = {node(1, resource)};
        assert(engine.start(project, registry, RunMode::Loop));
        {
            std::unique_lock lock(mutex);
            assert(gate.wait_for(lock, 2s, [&] { return entered; }));
        }
        if (reset)
            assert(engine.requestReset());
        else
        {
            project.flows.front().nodes.clear();
            assert(engine.requestSynchronize(project, registry));
        }
        assert(engine.pollLatest().state == RuntimeState::Stopping);
        assert(!releasedAfterExecution);
        {
            std::lock_guard lock(mutex);
            proceed = true;
        }
        gate.notify_all();
        const auto completed = finish(engine);
        assert(releasedAfterExecution);
        if (reset)
            assert(completed.generation == 2 && !snapshot(completed, 1));
        else
            assert(completed.generation == 1 && completed.nodes.empty());
    }
}

void executionMetricsAndTimingSeparation()
{
    RuntimeEngine engine(1);
    ToolRegistry  registry;
    auto          mockDef = definition("test.metrics");

    struct MetricsTool final
        : public IToolInstance
        , public IPythonExecutionMetrics
    {
        ToolStatus initialize(const Values &) override
        {
            return {};
        }

        ToolResult execute(const Values &) override
        {
            std::this_thread::sleep_for(10ms);
            return ToolResult{{}, {{"value", std::int64_t{42}}}};
        }

        ToolStatus release() override
        {
            return {};
        }

        ExecutionMetrics pythonMetrics() const override
        {
            ExecutionMetrics m;
            m.waitGilMs      = 3.5;
            m.pythonInputMs  = 1.2;
            m.pythonCallMs   = 5.0;
            m.pythonOutputMs = 0.8;
            return m;
        }
    };

    registry.registerCpp(mockDef,
                         [](const PlatformToolServices &) { return std::make_unique<MetricsTool>(); });
    registry.addDefinition(mockDef);
    auto              n = node(1, mockDef);
    ProjectDefinition project;
    project.dirty               = true;
    project.flows.front().nodes = {n};
    assert(engine.start(project, registry, RunMode::Once));
    auto completed = finish(engine);
    auto snap      = snapshot(completed, 1);
    assert(snap != nullptr);
    assert(snap->timing.adapterExecuteMs >= 5.0);
    assert(snap->timing.executeMs == snap->timing.adapterExecuteMs);
    assert(snap->timing.waitGilMs == 3.5);
    assert(snap->timing.pythonInputMs == 1.2);
    assert(snap->timing.pythonCallMs == 5.0);
    assert(snap->timing.pythonOutputMs == 0.8);
    // Python sub-items must not be duplicated into totalMs
    assert(snap->timing.totalMs >= snap->timing.adapterExecuteMs);
}

void timingRecordedOnFailure()
{
    RuntimeEngine engine(1);
    ToolRegistry  registry;
    auto          mockDef = definition("test.failure.metrics");

    struct FailingMetricsTool final
        : public IToolInstance
        , public IPythonExecutionMetrics
    {
        ToolStatus initialize(const Values &) override
        {
            return {};
        }

        ToolResult execute(const Values &) override
        {
            std::this_thread::sleep_for(15ms);
            throw std::runtime_error("simulated failure after delay");
        }

        ToolStatus release() override
        {
            return {};
        }

        ExecutionMetrics pythonMetrics() const override
        {
            ExecutionMetrics m;
            m.waitGilMs      = 2.0;
            m.pythonInputMs  = 1.0;
            m.pythonCallMs   = 12.0;
            m.pythonOutputMs = 0.0;
            return m;
        }
    };

    registry.registerCpp(mockDef,
                         [](const PlatformToolServices &) { return std::make_unique<FailingMetricsTool>(); });
    registry.addDefinition(mockDef);
    auto              n = node(1, mockDef);
    ProjectDefinition project;
    project.dirty               = true;
    project.flows.front().nodes = {n};
    assert(engine.start(project, registry, RunMode::Once));
    auto completed = finish(engine);
    auto snap      = snapshot(completed, 1);
    assert(snap != nullptr);
    assert(snap->status.code != 0); // Must be an error status
    // VR-07 check: timings were preserved even though execute failed
    assert(snap->timing.adapterExecuteMs >= 10.0);
    assert(snap->timing.waitGilMs == 2.0);
    assert(snap->timing.pythonCallMs == 12.0);
    assert(snap->timing.totalMs >= snap->timing.adapterExecuteMs);
}

void crossRoundPythonMetricIsolation()
{
    RuntimeEngine     engine(1);
    ToolRegistry      registry;
    auto              mockDef = definition("test.isolated.metrics", {"in"});
    std::atomic<bool> shouldFailInExecute{false};

    struct PersistentMetricsTool final
        : public IToolInstance
        , public IPythonExecutionMetrics
    {
        std::atomic<bool> &failExecute;

        explicit PersistentMetricsTool(std::atomic<bool> &flag)
            : failExecute(flag)
        {
        }

        ToolStatus initialize(const Values &) override
        {
            return {};
        }

        ToolResult execute(const Values &) override
        {
            std::this_thread::sleep_for(15ms);
            if (failExecute.load())
            {
                throw std::runtime_error("simulated failure after delay in execute");
            }
            return ToolResult{{}, {{"value", std::int64_t{42}}}};
        }

        ToolStatus release() override
        {
            return {};
        }

        ExecutionMetrics pythonMetrics() const override
        {
            ExecutionMetrics m;
            m.waitGilMs      = 3.5;
            m.pythonInputMs  = 1.2;
            m.pythonCallMs   = 15.0;
            m.pythonOutputMs = 0.8;
            return m;
        }
    };

    registry.registerCpp(mockDef, [&](const PlatformToolServices &)
                         { return std::make_unique<PersistentMetricsTool>(shouldFailInExecute); });
    registry.addDefinition(mockDef);

    auto n                 = node(1, mockDef);
    n.inputs["in"].mode    = BindingMode::Literal;
    n.inputs["in"].literal = std::int64_t{10};

    ProjectDefinition project;
    project.dirty               = true;
    project.flows.front().nodes = {n};

    assert(engine.start(project, registry, RunMode::Once));
    auto r1    = finish(engine);
    auto snap1 = snapshot(r1, 1);
    assert(snap1 != nullptr);
    assert(snap1->status.code == 0);
    assert(snap1->timing.adapterExecuteMs >= 10.0);
    assert(snap1->timing.pythonCallMs == 15.0);

    project.flows.front().nodes.front().inputs["in"].literal = Utf8("not_an_int");
    assert(engine.start(project, registry, RunMode::Once));
    auto r2    = finish(engine);
    auto snap2 = snapshot(r2, 1);
    assert(snap2 != nullptr);
    assert(snap2->status.code == -2);
    assert(snap2->timing.prepareMs > 0);
    assert(snap2->timing.executeMs == 0.0);
    assert(snap2->timing.adapterExecuteMs == 0.0);
    assert(snap2->timing.waitGilMs == 0.0);
    assert(snap2->timing.pythonInputMs == 0.0);
    assert(snap2->timing.pythonCallMs == 0.0);
    assert(snap2->timing.pythonOutputMs == 0.0);

    shouldFailInExecute.store(true);
    project.flows.front().nodes.front().inputs["in"].literal = std::int64_t{20};
    assert(engine.start(project, registry, RunMode::Once));
    auto r3    = finish(engine);
    auto snap3 = snapshot(r3, 1);
    assert(snap3 != nullptr);
    assert(snap3->status.code == -1);
    assert(snap3->timing.adapterExecuteMs >= 10.0);
    assert(snap3->timing.waitGilMs == 3.5);
    assert(snap3->timing.pythonCallMs == 15.0);
    assert(snap3->timing.totalMs >= snap3->timing.adapterExecuteMs);
}

void hostBindingReceiverContract()
{
    ToolDefinition recvDef;
    recvDef.id           = "test.custom.receiver";
    recvDef.displayName  = "自定义接收";
    recvDef.version      = "1.0.0";
    recvDef.kind         = ToolKind::Host;

    recvDef.initParams   = {
        {"source_flow_id", "源流程", {Type::String},          Utf8{""}},
        {"source_node_seq", "源节点", {Type::String},          Utf8{""}},
        {  "source_port_id", "源端口", {Type::String},          Utf8{""}},
        {      "timeout_ms",   "超时",    {Type::Int}, std::int64_t{500}}
    };
    recvDef.outputs = {
        {                "value",   "接收值",    {Type::Int}},
        {   "source_status_code",   "状态码",    {Type::Int}},
        {"source_status_message", "状态消息", {Type::String}}
    };

    ToolsPackage pkg;
    pkg.root = std::filesystem::current_path();

    PackageTool pt;
    pt.definition  = recvDef;
    pt.definition.hostBinding = "visionrt.receiver";
    pkg.tools.push_back(pt);

    ToolDefinition unknownDef = recvDef;
    unknownDef.id             = "test.unknown.receiver";
    PackageTool ptUnknown;
    ptUnknown.definition  = unknownDef;
    ptUnknown.definition.hostBinding = "visionrt.nonexistent_binding";
    pkg.tools.push_back(ptUnknown);

    ToolDefinition missingValDef = recvDef;
    missingValDef.id             = "test.missing_val.receiver";
    missingValDef.outputs.erase(missingValDef.outputs.begin());
    PackageTool ptMissingVal;
    ptMissingVal.definition  = missingValDef;
    ptMissingVal.definition.hostBinding = "visionrt.receiver";
    pkg.tools.push_back(ptMissingVal);

    ToolRegistry registry;
    registerReceiveTools(registry);
    registry.registerPackage(pkg);
    registry.loadNativeImplementations();

    auto availValid = registry.availability("test.custom.receiver");
    assert(availValid.state == ToolAvailability::Available);

    auto availUnknown = registry.availability("test.unknown.receiver");
    assert(availUnknown.state == ToolAvailability::ImplementationUnavailable);
    assert(availUnknown.status.message == "宿主绑定未注册：visionrt.nonexistent_binding");

    bool thrownUnknown = false;
    try
    {
        PlatformToolServices services;
        registry.create("test.unknown.receiver", services);
    }
    catch (const RegistryError &error)
    {
        thrownUnknown = true;
        assert(error.reason == ToolAvailability::ImplementationUnavailable);
    }
    assert(thrownUnknown);

    bool thrownMissingVal = false;
    try
    {
        PlatformToolServices services;
        registry.create("test.missing_val.receiver", services);
    }
    catch (const ContractError &error)
    {
        thrownMissingVal = true;
        assert(std::string(error.what()).find("接收工具缺少 value 输出端口") != std::string::npos);
    }
    assert(thrownMissingVal);

    auto source = definition("test.sender");
    install(registry, source, [](const Values &) { return ToolResult{{}, {{"value", std::int64_t{999}}}}; });
    auto senderNode         = node(1, source);
    auto receiverNode       = node(2, *registry.find("test.custom.receiver"));
    receiverNode.initParams = {
        {"source_flow_id",      Utf8("sender_flow")},
        {"source_node_seq", Utf8(std::to_string(senderNode.id))},
        {  "source_port_id",            Utf8("value")},
        {      "timeout_ms",        std::int64_t{500}}
    };
    ProjectDefinition project;
    project.flows = {
        {  "sender_flow",   "sender_flow", true,   {senderNode}},
        {"receiver_flow", "receiver_flow", true, {receiverNode}}
    };
    RuntimeEngine engine(2);
    assert(engine.start(project, registry, RunMode::Once));
    auto completed = finish(engine);
    assert(completed.completion.has_value());
    auto recvSnap = snapshot(completed, 2);
    assert(recvSnap != nullptr);
    assert(recvSnap->status.code == 0);
    assert(std::get<std::int64_t>(recvSnap->outputs.at("value")) == 999);
    assert(std::get<std::int64_t>(recvSnap->outputs.at("source_status_code")) == 0);

    // Dynamic type: string
    ToolDefinition strRecvDef  = recvDef;
    strRecvDef.id              = "test.str.receiver";
    strRecvDef.displayName     = "字符串接收器";
    strRecvDef.outputs[0].type = {Type::String};
    ToolsPackage strPkg;
    strPkg.root = std::filesystem::current_path();
    PackageTool strPt;
    strPt.definition  = strRecvDef;
    strPt.definition.hostBinding = "visionrt.receiver";
    strPkg.tools.push_back(strPt);

    ToolRegistry strRegistry;
    registerReceiveTools(strRegistry);
    strRegistry.registerPackage(strPkg);
    strRegistry.loadNativeImplementations();

    auto strSource            = definition("test.str_sender");
    strSource.outputs[0].type = {Type::String};
    install(strRegistry, strSource,
            [](const Values &) { return ToolResult{{}, {{"value", Utf8("dynamic host binding works")}}}; });
    auto strSenderNode         = node(1, strSource);
    auto strReceiverNode       = node(2, *strRegistry.find("test.str.receiver"));
    strReceiverNode.initParams = {
        {"source_flow_id",     Utf8("flow_str_sender")},
        {"source_node_seq", Utf8(std::to_string(strSenderNode.id))},
        {  "source_port_id",               Utf8("value")},
        {      "timeout_ms",           std::int64_t{500}}
    };
    ProjectDefinition strProject;
    strProject.flows = {
        {  "flow_str_sender",   "flow_str_sender", true,   {strSenderNode}},
        {"flow_str_receiver", "flow_str_receiver", true, {strReceiverNode}}
    };
    RuntimeEngine strEngine(2);
    assert(strEngine.start(strProject, strRegistry, RunMode::Once));
    auto strCompleted = finish(strEngine);
    auto strSnap      = snapshot(strCompleted, 2);
    assert(strSnap != nullptr);
    assert(strSnap->status.code == 0);
    assert(std::get<Utf8>(strSnap->outputs.at("value")).str() == "dynamic host binding works");
}
} // namespace

void singleReceiverResolvesAllValueTypes(const std::filesystem::path &toolsRoot)
{
    std::vector<std::pair<TypeSpec, PortValue>> cases{
        {{Type::Int}, std::int64_t{25}},
        {{Type::Float}, 2.5},
        {{Type::Bool}, true},
        {{Type::String}, Utf8("跨流程数据")},
        {{Type::ImageBatch}, ImageBatch({OwnedImage::copyOf(cv::Mat::ones(2, 3, CV_8UC1))})},
        {{Type::RoiBatch}, RoiBatch({{Roi::circle(2, 3, 1)}})},
        {{Type::StringArray, {}, 1}, StringArray(1, Layout::Dense, {StringBuffer({2}, {"中文", "text"})})},
        {{Type::StringArray, {}, 2, Layout::BatchRagged},
            StringArray(2, Layout::BatchRagged, {StringBuffer({2}, {"中文", "text"}), StringBuffer({0}, {})})}
    };
    for (const auto dtype : {DType::Int64, DType::Float64})
        for (const auto &[rank, layout] : std::vector<std::pair<int, Layout>>{
            {1, Layout::Dense}, {2, Layout::Dense}, {2, Layout::BatchRagged}})
        {
            const auto shape = rank == 2 && layout == Layout::Dense
                ? std::vector<std::int64_t>{1, 2} : std::vector<std::int64_t>{2};
            const std::int64_t integers[]{17, 29};
            const double floats[]{1.5, 2.5};
            const auto bytes = dtype == DType::Int64 ? std::as_bytes(std::span(integers)) : std::as_bytes(std::span(floats));
            cases.emplace_back(TypeSpec{Type::NumericArray, dtype, rank, layout},
                NumericArray(dtype, rank, layout, {NumericBuffer::copy(dtype, shape, bytes)}));
        }
    const float scores[]{0.25f, 0.75f};
    cases.emplace_back(TypeSpec{Type::NumericArray, DType::Float32, 2, Layout::BatchRagged},
        NumericArray(DType::Float32, 2, Layout::BatchRagged,
            {NumericBuffer::copy(DType::Float32, {2}, std::as_bytes(std::span(scores)))}));
    RuntimeEngine engine(2);
    for (const auto &[type, value] : cases)
    {
        ToolRegistry registry;
        registry.registerPackage(readToolsPackage(toolsRoot));
        registry.loadNativeImplementations();
        registerReceiveTools(registry);
        auto source = definition("test.typed.sender");
        source.outputs.front().type = type;
        install(registry, source, [value](const Values &) { return ToolResult{{}, {{"value", value}}}; });
        ProjectEditor editor;
        const auto sourceFlow = editor.addFlow("source");
        const auto sender = editor.addNode(sourceFlow, source, {});
        const auto receiver = editor.addNode("flow_1", *registry.find("visionrt.flow.receive"), {});
        assert(!engine.start(editor.project(), registry, RunMode::Once));
        editor.setInitParam("flow_1", receiver, "source_flow_id", Utf8(sourceFlow));
        editor.setInitParam("flow_1", receiver, "source_node_seq", Utf8(std::to_string(sender)));
        editor.setInitParam("flow_1", receiver, "source_port_id", Utf8("value"));
        editor.setInitParam("flow_1", receiver, "timeout_ms", std::int64_t{1000});
        ProjectStorage storage(registry);
        QTemporaryDir temporary;
        assert(temporary.isValid());
        auto project = editor.project();
        const auto file = std::filesystem::u8path(temporary.path().toUtf8().toStdString()) / std::filesystem::path(u8"工程.vrt.json");
        storage.save(project, file);
        const auto loaded = storage.load(file);
        assert(loaded.diagnostics.empty());
        assert(loaded.project.flows.front().nodes.front().signatures.outputs.at("value") == type);
        assert(engine.start(loaded.project, registry, RunMode::Once));
        const auto result = snapshot(finish(engine), receiver);
        assert(result->status.code == 0);
        assert(equalValues(result->outputs.at("value"), value));
        if (type == TypeSpec{Type::Int})
        {
            auto sinkDefinition = definition("test.integer.sink", {"input"});
            install(registry, sinkDefinition, [](const Values &) { return ToolResult{{}, {{"value", std::int64_t{0}}}}; });
            const auto sink = editor.addNode("flow_1", sinkDefinition, {1, 0});
            editor.setSource("flow_1", sink, "input", SourcePort{receiver, "value"});
            editor.setMode("flow_1", sink, "input", BindingMode::Upstream);
            source.outputs.front().type = {Type::String};
            ToolRegistry changedRegistry;
            changedRegistry.registerPackage(readToolsPackage(toolsRoot));
            changedRegistry.loadNativeImplementations();
            registerReceiveTools(changedRegistry);
            install(changedRegistry, source, [](const Values &) { return ToolResult{{}, {{"value", Utf8("changed")}}}; });
            install(changedRegistry, sinkDefinition, [](const Values &) { return ToolResult{{}, {{"value", std::int64_t{0}}}}; });
            const auto rejected = engine.start(editor.project(), changedRegistry, RunMode::Once);
            assert(!rejected && rejected.message.find("类型不兼容") != std::string::npos);
            const auto reconciled = ProjectStorage(changedRegistry).reconcile(editor.project());
            assert(reconciled.project.flows.front().nodes.front().signatures.outputs.at("value") == TypeSpec{Type::String});
            assert(std::any_of(reconciled.diagnostics.begin(), reconciled.diagnostics.end(), [](const auto &diagnostic)
                { return diagnostic.code == ProjectStorageCode::InvalidReference; }));
        }
    }
    std::cout << "Single receiver executed " << cases.size() << " concrete type contracts after save/reopen\n";
}

void stopWakesReceiverWait(const std::filesystem::path &toolsRoot)
{
    ToolRegistry registry;
    registry.registerPackage(readToolsPackage(toolsRoot));
    registry.loadNativeImplementations();
    registerReceiveTools(registry);
    auto source = definition("test.waiting_sender");
    install(registry, source, [](const Values &) { return ToolResult{{}, {{"value", std::int64_t{1}}}}; });
    auto sender = node(1, source);
    auto typed = *registry.find("visionrt.flow.receive");
    for (auto &port : typed.outputs)
        if (port.typeFrom)
        {
            port.type = {Type::Int};
            port.typeFrom.reset();
        }
    registry.replaceDefinition(typed);
    auto receiver = node(2, typed);
    receiver.signatures.outputs.emplace("value", TypeSpec{Type::Int});
    receiver.initParams = {
        {"source_flow_id",       Utf8("sender")},
        {"source_node_seq", Utf8(std::to_string(sender.id))},
        {  "source_port_id",        Utf8("value")},
        {      "timeout_ms", std::int64_t{10000}}
    };
    ProjectDefinition project;
    project.flows = {
        {  "sender",   "sender", false,   {sender}},
        {"receiver", "receiver",  true, {receiver}}
    };
    enum class Termination { Stop, Reset, Destruction };
    for (const auto termination : {Termination::Stop, Termination::Reset, Termination::Destruction})
    {
        auto engine = std::make_unique<RuntimeEngine>(2);
        assert(engine->start(project, registry, RunMode::Loop));
        bool waiting = false;
        for (int attempt = 0; attempt < 100 && !waiting; ++attempt)
        {
            const auto update = engine->pollLatest();
            for (const auto &item : update.nodes)
                waiting = item.node == 2 && item.active;
            if (!waiting)
                std::this_thread::sleep_for(10ms);
        }
        assert(waiting);
        const auto started = std::chrono::steady_clock::now();
        if (termination == Termination::Destruction)
            engine.reset();
        else
        {
            if (termination == Termination::Reset)
                assert(engine->requestReset());
            else
                engine->requestStop();
            const auto result = finish(*engine);
            if (termination == Termination::Reset)
                assert(result.generation == 2 && !snapshot(result, 2));
            else
            {
                const auto cancelled = snapshot(result, 2);
                assert(cancelled && cancelled->status.code == kCancelledStatus);
                assert(cancelled->status.message == "运行已取消");
            }
        }
        const auto elapsed = std::chrono::steady_clock::now() - started;
        assert(elapsed < 1s);
    }
    std::cout << "Stop, reset and destruction wake receiver waits\n";
}

void singleNodeUpstreamValidity()
{
    RuntimeEngine engine(2);
    ToolRegistry  registry;
    int           executions[3] = {0, 0, 0};
    auto          first = definition("test.chain.first");
    install(registry, first,
            [&](const Values &)
            {
                ++executions[0];
                return ToolResult{{}, {{"value", std::int64_t{1}}}};
            });
    auto middle = definition("test.chain.middle", {"input"});
    install(registry, middle,
            [&](const Values &in)
            {
                ++executions[1];
                return ToolResult{{}, {{"value", std::get<std::int64_t>(in.at("input")) + 1}}};
            });
    auto last = definition("test.chain.last", {"input"});
    install(registry, last,
            [&](const Values &in)
            {
                ++executions[2];
                return ToolResult{{}, {{"value", std::get<std::int64_t>(in.at("input")) + 1}}};
            });
    auto a = node(1, first), b = node(2, middle), c = node(3, last);
    bind(b, "input", 1);
    bind(c, "input", 2);
    ProjectDefinition project;
    project.flows.front().nodes = {a, b, c};
    assert(engine.start(project, registry, RunMode::Once));
    finish(engine);
    assert(executions[0] == 1 && executions[1] == 1 && executions[2] == 1);

    project.flows.front().nodes[0].executionRevision = 1;
    const auto rejected = engine.start(project, registry, RunMode::Node, 3);
    assert(!rejected && rejected.message.find("参数已修改") != std::string::npos);
    const auto rejectedMiddle = engine.start(project, registry, RunMode::Node, 2);
    assert(!rejectedMiddle && rejectedMiddle.message.find("参数已修改") != std::string::npos);
    assert(executions[1] == 1 && executions[2] == 1);

    assert(engine.start(project, registry, RunMode::Node, 1));
    finish(engine);
    assert(executions[0] == 2);
    assert(engine.start(project, registry, RunMode::Node, 2));
    finish(engine);
    assert(executions[1] == 2);
    assert(engine.start(project, registry, RunMode::Node, 3));
    finish(engine);
    assert(executions[2] == 2);

    project.flows.front().nodes[0].executionRevision = 2;
    assert(engine.start(project, registry, RunMode::Node, 1));
    finish(engine);
    assert(executions[0] == 3);
    const auto staleMiddle = engine.start(project, registry, RunMode::Node, 3);
    assert(!staleMiddle && staleMiddle.message.find("已重新运行") != std::string::npos);
    assert(executions[2] == 2);
    assert(engine.start(project, registry, RunMode::Node, 2));
    finish(engine);
    assert(engine.start(project, registry, RunMode::Node, 3));
    finish(engine);
    assert(executions[2] == 3);

    project.flows.front().nodes[1].alias = "重命名不影响结果";
    project.flows.front().nodes[1].position = {4, 5};
    assert(engine.start(project, registry, RunMode::Node, 3));
    finish(engine);
    assert(executions[2] == 4);
    std::cout << "Single-node execution rejects stale upstream publications and ignores layout changes\n";
}

void explicitRequirednessAtExecution()
{
    for (const bool initialization : {false, true})
        for (const bool required : {false, true})
            for (const bool explicitMissing : {false, true})
            {
                RuntimeEngine engine(1);
                ToolRegistry registry;
                auto declared = definition("test.required");
                PortDefinition limit{"limit", "限制", {Type::Int}, std::int64_t{5}};
                limit.required = required;
                (initialization ? declared.initParams : declared.inputs).push_back(limit);
                int initialized = 0;
                int executed = 0;
                install(registry, declared,
                        [&](const Values &inputs)
                        {
                            ++executed;
                            if (!initialization)
                                assert(equalValues(inputs.at("limit"), explicitMissing ? PortValue{Missing{}}
                                                                                     : PortValue{std::int64_t{5}}));
                            return ToolResult{{}, {{"value", std::int64_t{1}}}};
                        },
                        [&]
                        {
                            ++initialized;
                            return ToolStatus{};
                        });
                auto configured = node(1, declared);
                if (initialization)
                {
                    configured.signatures.initParams.emplace("limit", TypeSpec{Type::Int});
                    if (explicitMissing)
                        configured.initParams.emplace("limit", Missing{});
                }
                else if (!explicitMissing)
                    configured.inputs.erase("limit");
                ProjectDefinition project;
                project.flows.front().nodes = {configured};
                assert(engine.start(project, registry, RunMode::Once));
                const auto result = snapshot(finish(engine), 1);
                const bool accepted = !required || !explicitMissing;
                assert(static_cast<bool>(result->status) == accepted);
                assert(initialized == (accepted ? 1 : 0));
                assert(executed == (accepted ? 1 : 0));
                if (!accepted)
                    assert(result->status.message.find("limit") != std::string::npos);
            }
    std::cout << "Explicit requiredness governs input and initialization Missing values independently of defaults\n";
}

void managementDiagnosticsPreserveStatus()
{
    {
        RuntimeEngine     engine(1);
        ToolRegistry      registry;
        ProjectDefinition project;
        const auto        empty = engine.start(project, registry, RunMode::Once);
        assert(empty.code == 1 && empty.message == "请至少选择一个非空流程");
        const auto missing = engine.start(project, registry, RunMode::Node, 99);
        assert(missing.code == 1 && missing.message == "目标节点不存在：99");
    }
    for (int scenario = 0; scenario < 3; ++scenario)
    {
        RuntimeEngine engine(1);
        assert(engine.requestRefresh(
            [scenario]() -> ToolStatus
            {
                if (scenario == 1)
                    throw ContractError("管理配置无效");
                if (scenario == 2)
                    throw std::runtime_error("foreign management detail");
                return {9, "资源刷新失败", "original refresh detail"};
            }));
        const auto completed = finish(engine);
        assert(completed.diagnostics.size() == 1);
        const auto &event = completed.diagnostics.front();
        assert(event.generation == completed.generation && event.phase == RuntimePhase::Management);
        const auto &status = event.status();
        if (scenario == 0)
            assert(status.code == 9 && status.message == "资源刷新失败" && status.detail == "original refresh detail");
        else if (scenario == 1)
            assert(status.code == -2 && status.message == "管理配置无效" && status.detail.empty());
        else
            assert(status.code == -1 && status.message == "运行时管理异常"
                   && status.detail == "foreign management detail");
        assert(engine.pollLatest().diagnostics.empty());
    }
}

void optionalOutputsAndCachedMissing()
{
    ToolRegistry registry;
    auto         source        = definition("test.optional_output");
    source.outputs[0].required = false;
    source.kind                = ToolKind::Host;
    source.hostBinding         = "test.optional_output";
    registry.addDefinition(source);
    registry.registerHostBinding(source.hostBinding,
                                 [](const ToolDefinition &, const PlatformToolServices &)
                                 {
                                     auto instance = std::make_unique<Function>();
                                     instance->run = [](const Values &)
                                     {
                                         return ToolResult{{}, {{"value", Missing{}}}};
                                     };
                                     return instance;
                                 });
    auto middle               = definition("test.optional_input", {"input"});
    middle.inputs[0].required = false;
    install(registry, middle,
            [](const Values &inputs)
            {
                assert(isMissing(inputs.at("input")));
                return ToolResult{{}, {{"value", std::int64_t{42}}}};
            });
    auto sink = definition("test.optional_sink", {"input"});
    install(registry, sink, [](const Values &inputs) { return ToolResult{{}, {{"value", inputs.at("input")}}}; });
    ProjectDefinition project;
    auto              first = node(1, source), second = node(2, middle), third = node(3, sink);
    bind(second, "input", 1);
    bind(third, "input", 2);
    project.flows.front().nodes = {first, second, third};
    RuntimeEngine engine(1);
    assert(engine.start(project, registry, RunMode::Once));
    auto full = finish(engine);
    assert(snapshot(full, 1)->status.code == 0 && isMissing(snapshot(full, 1)->outputs.at("value")));
    assert(snapshot(full, 1)->outputTypes == first.signatures.outputs);
    assert(snapshot(full, 2)->status.code == 0 && snapshot(full, 3)->status.code == 0);
    assert(engine.start(project, registry, RunMode::Node, 3));
    auto cached = finish(engine);
    assert(snapshot(cached, 3)->status.code == 0);
    assert(std::get<std::int64_t>(snapshot(cached, 3)->outputs.at("value")) == 42);
    auto nowRequired                = source;
    nowRequired.outputs[0].required = true;
    registry.replaceDefinition(nowRequired);
    const auto requiredRejected = engine.start(project, registry, RunMode::Node, 3);
    assert(!requiredRejected && requiredRejected.message.find("value") != std::string::npos);
    auto changed            = source;
    changed.outputs[0].type = {Type::Float};
    registry.replaceDefinition(changed);
    const auto rejected = engine.start(project, registry, RunMode::Node, 3);
    assert(!rejected && rejected.message.find("输出合同已变化") != std::string::npos);
}

void failedBusinessOutputsAreNotInputs()
{
    ToolRegistry registry;
    const auto   source = definition("test.failed_publication");
    install(registry, source,
            [](const Values &)
            {
                return ToolResult{
                    {7, "业务执行失败", "original failure detail"},
                    {{"value", std::int64_t{99}}}
                };
            });
    auto required      = definition("test.required_consumer", {"input"});
    int  requiredCalls = 0;
    install(registry, required,
            [&](const Values &)
            {
                ++requiredCalls;
                return ToolResult{{}, {{"value", std::int64_t{1}}}};
            });
    auto optional               = definition("test.optional_consumer", {"input"});
    optional.inputs[0].required = false;
    install(registry, optional,
            [](const Values &inputs)
            {
                assert(isMissing(inputs.at("input")));
                return ToolResult{{}, {{"value", std::int64_t{42}}}};
            });
    auto observer           = definition("test.status_consumer", {"code", "message"});
    observer.inputs[1].type = {Type::String};
    install(registry, observer,
            [](const Values &inputs)
            {
                assert(std::get<std::int64_t>(inputs.at("code")) == 7);
                assert(std::get<Utf8>(inputs.at("message")).str() == "业务执行失败");
                return ToolResult{{}, {{"value", inputs.at("code")}}};
            });
    auto first = node(1, source), second = node(2, required), third = node(3, optional), fourth = node(4, observer);
    bind(second, "input", 1);
    bind(third, "input", 1);
    fourth.inputs["code"] = {
        BindingMode::Upstream, Missing{},
         SourcePort{1, "__status_code"}
    };
    fourth.inputs["message"] = {
        BindingMode::Upstream, Missing{},
         SourcePort{1, "__status_message"}
    };
    ProjectDefinition project;
    project.flows.front().nodes = {first, second, third, fourth};
    RuntimeEngine engine(1);
    assert(engine.start(project, registry, RunMode::Once));
    const auto result = finish(engine);
    assert(snapshot(result, 1)->status.code == 7);
    assert(snapshot(result, 1)->status.detail == "original failure detail");
    assert(std::get<std::int64_t>(snapshot(result, 1)->outputs.at("value")) == 99);
    assert(requiredCalls == 0 && snapshot(result, 2)->status.code == -2);
    assert(isMissing(snapshot(result, 2)->outputs.at("value")));
    assert(snapshot(result, 3)->status.code == 0 && snapshot(result, 4)->status.code == 0);
}

void diagnosticIdentityAndTruncation()
{
    RuntimeEngine engine(1);
    engine.injectDiagnosticForTest("同文本");
    engine.injectDiagnosticForTest("同文本");
    const auto first = engine.pollLatest();
    assert(first.diagnostics.size() == 2);
    assert(first.diagnostics[0].generation == first.generation);
    assert(first.diagnostics[0].sequence < first.diagnostics[1].sequence);
    assert(first.diagnostics[0].status().message == "同文本" && first.diagnostics[1].status().message == "同文本");
    assert(first.droppedDiagnostics == 0);
    const auto unchanged = engine.pollLatest();
    assert(unchanged.diagnostics.empty() && unchanged.droppedDiagnostics == 0);

    for (int i = 0; i < 101; ++i)
        engine.injectDiagnosticForTest("事件 " + std::to_string(i));
    const auto truncated = engine.pollLatest();
    assert(truncated.droppedDiagnostics == 3);
    assert(truncated.diagnostics.size() == 100);
    assert(truncated.diagnostics.front().sequence == 4);
    assert(truncated.diagnostics.back().sequence == 103);
    const auto drained = engine.pollLatest();
    assert(drained.diagnostics.empty() && drained.droppedDiagnostics == 0);
    std::cout << "Diagnostic events are incremental, identity-stable and truncation-aware\n";
}

int main(int argc, char **argv)
{
    assert(argc == 2);
    optionalOutputsAndCachedMissing();
    failedBusinessOutputsAreNotInputs();
    explicitRequirednessAtExecution();
    diagnosticIdentityAndTruncation();
    managementDiagnosticsPreserveStatus();
    singleNodeUpstreamValidity();
    stopWakesReceiverWait(argv[1]);
    branchesOverlapAndJoin();
    failureReplacesOldValueAndManualUsesCache();
    initializationSuppressionRetryAndReset();
    stopDrainsCommittedFlowsAndFreezesConfiguration();
    assert(argc == 2);
    receiverCapacityAndSessionIsolation(argv[1]);
    singleReceiverResolvesAllValueTypes(argv[1]);
    receiverConsumesFailures();
    hostBindingReceiverContract();
    executionDiagnostics();
    managementWaitsForActiveInvocation();
    executionMetricsAndTimingSeparation();
    timingRecordedOnFailure();
    crossRoundPythonMetricIsolation();
    std::cout << "runtime contracts passed\n";
}
