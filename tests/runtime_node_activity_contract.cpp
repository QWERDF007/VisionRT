// A node's execution window is the only live fact the window receives about running work. These
// contracts pin when it opens, that it stays open across parallel branches and rounds, that every
// exit path closes it, and that a gated node never opens one at all.

#include "core/NodeContracts.h"
#include "runtime/RuntimeEngine.h"
#include "ToolResultAssertions.h"

#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstdint>
#include <functional>
#include <iostream>
#include <memory>
#include <mutex>
#include <stdexcept>
#include <string>
#include <thread>
#include <vector>

using namespace visionrt;
using namespace std::chrono_literals;

namespace {

void require(bool condition, const std::string &message)
{
    if (!condition)
        throw std::runtime_error(message);
}

class Function final : public IToolInstance
{
public:
    std::function<ToolResult(const Values &)> run;

    ToolStatus initialize(const Values &) override
    {
        return {};
    }

    ToolResult execute(const Values &inputs) override
    {
        return run(inputs);
    }

    ToolStatus release() override
    {
        return {};
    }
};

void install(ToolRegistry &registry, const ToolDefinition &tool, std::function<ToolResult(const Values &)> run)
{
    registry.registerCpp(tool,
                         [run]()
                         {
                             auto instance = std::make_unique<Function>();
                             instance->run  = run;
                             return instance;
                         });
    registry.addDefinition(tool);
}

ToolDefinition intTool(std::string id)
{
    ToolDefinition tool;
    tool.nameEn  = std::move(id);
    tool.nameZh  = tool.nameEn;
    tool.version = "1";
    tool.kind    = ToolKind::Cpp;
    tool.outputs = {{2, "值", {Type::Int}}};
    return tool;
}

// A logical node: only the condition model can carry an execution guard, and only a node that
// publishes a Bool can be the guard's source. The model requires a python_class declaration, so the
// fixture registers one and gives it a host-side implementation.
ToolDefinition branchTool()
{
    ToolDefinition tool;
    tool.nameEn    = "test.activity.branch";
    tool.nameZh    = "条件分支";
    tool.version   = "1";
    tool.kind      = ToolKind::PythonClass;
    tool.nodeModel = NodeModel::Condition;
    tool.entry     = PythonEntry{"test", "activity/branch.py", "BranchTool"};
    PortDefinition mode;
    mode.id           = kConditionModePort;
    mode.nameZh       = "条件来源";
    mode.type         = {Type::String};
    mode.defaultValue = std::string{"formula"};
    mode.options      = {std::string{"formula"}, std::string{"upstream"}};
    tool.initParams   = {{kVariablesInitPort, "变量", {Type::Array, DType::String, 2}},
                         {kProgramInitPort, "公式", {Type::Array, DType::String, 2}}};
    tool.initParams.push_back(std::move(mode));
    tool.outputs      = {{kStatusCodePort, "状态码", {Type::Int}},
                         {kStatusMessagePort, "状态消息", {Type::String}},
                         {2, "成立", {Type::Bool}},
                         {3, "否则", {Type::Bool}},
                         {4, "成立分支状态", {Type::Int}},
                         {5, "否则分支状态", {Type::Int}}};
    return tool;
}

void installBranch(ToolRegistry &registry, const ToolDefinition &tool, std::function<ToolResult(const Values &)> run)
{
    registry.addDefinition(tool);
    registry.setPythonImplementation(tool.nameEn,
                                     [run]()
                                     {
                                         auto instance = std::make_unique<Function>();
                                         instance->run  = run;
                                         return instance;
                                     });
}

NodeDefinition node(NodeId id, const ToolDefinition &tool)
{
    NodeDefinition n;
    n.id               = id;
    n.position         = {0, static_cast<int>(id)};
    n.toolId           = tool.nameEn;
    n.toolNameSnapshot = tool.nameZh;
    if (tool.nodeModel == NodeModel::Condition)
        n.expression = ExpressionConfig{};
    const auto contract = tool.derivesPorts() ? nodeContract(n, tool) : tool;
    for (const auto &port : contract.inputs) n.signatures.inputs.emplace(port.id, port.type);
    for (const auto &port : contract.outputs) n.signatures.outputs.emplace(port.id, port.type);
    return n;
}

void start(RuntimeEngine &engine, const ProjectDefinition &project, const ToolRegistry &registry, RunMode mode,
           const char *message)
{
    const auto status = engine.start(project, registry, mode);
    require(static_cast<bool>(status), std::string(message) + ": " + status.message);
}

RuntimeUpdate finish(RuntimeEngine &engine)
{
    const auto deadline = std::chrono::steady_clock::now() + 15s;
    while (std::chrono::steady_clock::now() < deadline)
    {
        auto update = engine.pollLatest();
        if (update.completion)
        {
            require(engine.acknowledgeCompletion(*update.completion), "completion acknowledgement failed");
            return update;
        }
        std::this_thread::sleep_for(1ms);
    }
    throw std::runtime_error("runtime completion deadline exceeded");
}

ResultSlot::Snapshot snapshot(const RuntimeUpdate &update, NodeId id)
{
    for (const auto &item : update.nodes)
        if (item.node == id)
            return item.snapshot;
    throw std::runtime_error("node snapshot absent: " + std::to_string(id));
}

bool active(const RuntimeUpdate &update, NodeId id)
{
    for (const auto &item : update.nodes)
        if (item.node == id)
            return item.active;
    throw std::runtime_error("node update absent: " + std::to_string(id));
}

std::vector<NodeActivityEvent> drainAll(RuntimeEngine &engine)
{
    std::vector<NodeActivityEvent> events;
    for (auto batch = engine.drainActivity(); !batch.empty(); batch = engine.drainActivity())
        events.insert(events.end(), batch.begin(), batch.end());
    return events;
}

std::size_t countKind(const std::vector<NodeActivityEvent> &events, NodeId node, ActivityKind kind)
{
    std::size_t count = 0;
    for (const auto &event : events)
        if (event.node == node && event.kind == kind)
            ++count;
    return count;
}

// Two independent branches hold each other open, so the overlap is a fact of the schedule rather
// than a guess about timing. A stop request must not shorten either window: the nodes are still
// inside their tool call until they return.
void parallelBranchesStayRunningTogether()
{
    RuntimeEngine           engine(4);
    ToolRegistry            registry;
    std::mutex              mutex;
    std::condition_variable gate;
    int                     entered = 0;
    bool                    proceed = false;

    const auto tool = intTool("test.activity.hold");
    install(registry, tool,
            [&](const Values &)
            {
                std::unique_lock lock(mutex);
                ++entered;
                gate.notify_all();
                gate.wait_for(lock, 15s, [&] { return proceed; });
                return toolResult({{2, std::int64_t{1}}}, {});
            });
    ProjectDefinition project;
    project.flows = {
        {"left", "left", true, {node(1, tool)}},
        {"right", "right", true, {node(2, tool)}}
    };
    start(engine, project, registry, RunMode::Once, "parallel run did not start");
    {
        std::unique_lock lock(mutex);
        require(gate.wait_for(lock, 15s, [&] { return entered == 2; }), "both branches did not enter execution");
    }

    const auto during = drainAll(engine);
    require(countKind(during, 1, ActivityKind::EnteredExecution) == 1
                && countKind(during, 2, ActivityKind::EnteredExecution) == 1,
            "each branch must report its own execution start");
    require(countKind(during, 1, ActivityKind::LeftExecution) == 0
                && countKind(during, 2, ActivityKind::LeftExecution) == 0,
            "a branch reported leaving while it was still inside its tool call");

    engine.requestStop();
    require(active(engine.pollLatest(), 1) && active(engine.pollLatest(), 2),
            "a stop request faked an exit before the tool returned");
    require(drainAll(engine).empty(), "a stop request published an execution end");

    {
        std::lock_guard lock(mutex);
        proceed = true;
    }
    gate.notify_all();
    const auto completed = finish(engine);
    require(snapshot(completed, 1)->disposition == NodeDisposition::Succeeded
                && snapshot(completed, 2)->disposition == NodeDisposition::Succeeded,
            "a held branch did not complete");
    const auto after = drainAll(engine);
    require(countKind(after, 1, ActivityKind::LeftExecution) == 1
                && countKind(after, 2, ActivityKind::LeftExecution) == 1,
            "each branch must report exactly one execution end");
}

// Fast rounds are where polling loses the window; the event must still pair up per round and never
// arrive out of order across rounds of the same node.
void loopRoundsKeepTheirOwnWindow()
{
    RuntimeEngine    engine(2);
    ToolRegistry     registry;
    std::atomic<int> executions{0};
    const auto       tool = intTool("test.activity.fast");
    install(registry, tool,
            [&](const Values &)
            {
                if (executions.fetch_add(1) + 1 == 3)
                    engine.requestStop();
                return toolResult({{2, std::int64_t{1}}}, {});
            });
    ProjectDefinition project;
    project.flows.front().nodes = {node(1, tool)};
    start(engine, project, registry, RunMode::Loop, "loop run did not start");
    const auto completed = finish(engine);
    require(completed.round == 3, "the loop did not stop on the third round");
    require(executions.load() == 3, "a stopped round still executed its tool");

    const auto    events        = drainAll(engine);
    bool          expectingEnter = true;
    std::uint64_t round          = 0;
    int           windows        = 0;
    for (const auto &event : events)
    {
        if (event.node != 1)
            continue;
        require(event.generation == completed.generation && event.session == completed.session,
                "event does not belong to the completed run");
        if (event.kind == ActivityKind::EnteredExecution)
        {
            require(expectingEnter, "two execution starts without an end between them");
            require(event.round >= round, "an older round arrived after a newer one");
            round          = event.round;
            expectingEnter = false;
        }
        else
        {
            require(!expectingEnter, "execution end without a start");
            require(event.round == round, "execution end does not match its own round");
            expectingEnter = true;
            ++windows;
        }
    }
    require(expectingEnter && windows == 3, "every loop round must report exactly one complete window");
}

// The mailbox belongs to one run: a new start must not deliver the previous run's events.
void newRunStartsFromAnEmptyMailbox()
{
    RuntimeEngine    engine(2);
    ToolRegistry     registry;
    const auto       tool = intTool("test.activity.once");
    install(registry, tool, [](const Values &) { return toolResult({{2, std::int64_t{1}}}, {}); });
    ProjectDefinition project;
    project.flows.front().nodes = {node(1, tool)};

    start(engine, project, registry, RunMode::Once, "first run did not start");
    const auto first = finish(engine);
    const auto firstEvents = drainAll(engine);
    require(firstEvents.size() == 2, "a completed run must report its enter and leave");

    start(engine, project, registry, RunMode::Once, "second run did not start");
    const auto second = finish(engine);
    require(second.session > first.session, "the second run did not open a new session");
    const auto secondEvents = drainAll(engine);
    require(secondEvents.size() == 2, "the second run must report only its own window");
    for (const auto &event : secondEvents)
    {
        require(event.generation == second.generation && event.session == second.session,
                "an event from the earlier run survived into the next one");
        require(event.sequence > firstEvents.back().sequence, "event sequence did not stay global");
    }
}

void failedNodeStillClosesItsWindow()
{
    RuntimeEngine    engine(2);
    ToolRegistry     registry;
    const auto       tool = intTool("test.activity.throw");
    install(registry, tool,
            [](const Values &) -> ToolResult
            {
                throw std::runtime_error("工具内部异常");
            });
    ProjectDefinition project;
    project.flows.front().nodes = {node(1, tool)};
    start(engine, project, registry, RunMode::Once, "failing run did not start");
    const auto completed = finish(engine);
    require(snapshot(completed, 1)->disposition == NodeDisposition::Failed, "the tool exception was not a failure");

    const auto events = drainAll(engine);
    require(events.size() == 2, "a failure must still report exactly one window");
    require(events.front().kind == ActivityKind::EnteredExecution
                && events.back().kind == ActivityKind::LeftExecution,
            "the failed node did not close its window");
}

// A skipped node never enters the algorithm, so it must never look like it did.
void gatedNodeNeverEntersExecution()
{
    RuntimeEngine    engine(2);
    ToolRegistry     registry;
    std::atomic<int> executions{0};
    const auto       branch = branchTool();
    installBranch(registry, branch,
                  [&](const Values &)
                  {
                      executions.fetch_add(1);
                      return toolResult({{2, false}, {3, true}, {4, std::int64_t{0}}, {5, std::int64_t{0}}}, {});
                  });

    auto source = node(1, branch);
    auto gated  = node(2, branch);
    gated.executionGuard = ExecutionGuard{SourcePort{1, 2}};
    ProjectDefinition project;
    project.flows.front().nodes = {source, gated};

    start(engine, project, registry, RunMode::Once, "gated run did not start");
    const auto completed = finish(engine);
    require(executions.load() == 1, "a skipped node still executed its tool");
    require(snapshot(completed, 1)->disposition == NodeDisposition::Succeeded, "the guard source did not succeed");
    require(snapshot(completed, 2)->disposition == NodeDisposition::Skipped, "a false guard did not skip the node");

    const auto events = drainAll(engine);
    require(countKind(events, 1, ActivityKind::EnteredExecution) == 1,
            "the executed guard source reported no window");
    require(countKind(events, 2, ActivityKind::EnteredExecution) == 0
                && countKind(events, 2, ActivityKind::LeftExecution) == 0,
            "a skipped node published an execution window");
}

} // namespace

int main()
try
{
    std::cout << "1\n"; parallelBranchesStayRunningTogether();
    std::cout << "2\n"; loopRoundsKeepTheirOwnWindow();
    std::cout << "3\n"; newRunStartsFromAnEmptyMailbox();
    std::cout << "4\n"; failedNodeStillClosesItsWindow();
    std::cout << "5\n"; gatedNodeNeverEntersExecution();
    std::cout << "node activity contracts passed\n";
    return 0;
}
catch (const std::exception &error)
{
    std::cerr << "node activity contract failed: " << error.what() << "\n";
    return 1;
}
