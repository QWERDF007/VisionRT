#include "core/ProjectEditor.h"
#include "core/NodeContracts.h"
#include "runtime/RuntimeInternal.h"

#include <taskflow/taskflow.hpp>

#include <algorithm>
#include <condition_variable>
#include <deque>
#include <set>
#include <thread>

namespace visionrt {
namespace {
using namespace runtime_detail;
using Clock = std::chrono::steady_clock;

struct CompiledFlow
{
    std::string                         id;
    std::uint64_t                       revision = 0;
    std::vector<NodeId>                 nodes;
    std::vector<DependencyEdge>         edges;
    std::vector<ToolDefinition>         contracts;
    tf::Taskflow                        graph;
    std::shared_ptr<const RoundContext> activeRound;
};

bool samePorts(const std::vector<PortDefinition> &a, const std::vector<PortDefinition> &b)
{
    if (a.size() != b.size())
        return false;
    for (std::size_t i = 0; i < a.size(); ++i)
        if (a[i].id != b[i].id || a[i].type != b[i].type)
            return false;
    return true;
}

bool sameContract(const ToolDefinition &a, const ToolDefinition &b)
{
    return a.id == b.id && a.version == b.version && a.kind == b.kind && a.hostBinding == b.hostBinding
        && a.script == b.script && a.symbol == b.symbol && samePorts(a.inputs, b.inputs)
        && samePorts(a.outputs, b.outputs) && samePorts(a.initParams, b.initParams);
}

bool isPython(ToolKind kind)
{
    return kind == ToolKind::PythonFunction || kind == ToolKind::PythonClass;
}

double milliseconds(Clock::duration duration)
{
    return std::chrono::duration<double, std::milli>(duration).count();
}

// Single-node execution may reuse a previous session, but only while every
// consumed publication is still the current, successful and type-compatible
// source. The result is the first node that must be rerun.
std::optional<std::string> upstreamValidityError(
    const FrozenRun &frozen, NodeId target, std::uint64_t generation,
    const std::map<NodeId, std::shared_ptr<NodeRuntime>> &runtimes)
{
    std::set<NodeId> valid;
    std::set<NodeId> visiting;
    std::function<std::optional<std::string>(NodeId)> check = [&](NodeId id) -> std::optional<std::string>
    {
        if (valid.contains(id))
            return std::nullopt;
        if (!visiting.insert(id).second)
            return std::string("上游依赖存在循环，无法执行");
        const auto nodeIt = frozen.nodes.find(id);
        if (nodeIt == frozen.nodes.end())
            return std::string("上游节点不存在");
        const auto &node        = *nodeIt->second;
        const auto  runtimeIt   = runtimes.find(id);
        const auto  snapshot    = runtimeIt == runtimes.end() ? ResultSlot::Snapshot{} : runtimeIt->second->slot.latest();
        if (!snapshot || snapshot->generation != generation)
            return "上游节点 " + node.title() + " 尚未运行，请先运行该节点";
        if (snapshot->executionRevision != node.executionRevision)
            return "上游节点 " + node.title() + " 的参数已修改，请先重新运行该节点";
        if (snapshot->status.code != 0)
            return "上游节点 " + node.title() + " 未成功运行，请先重新运行该节点";
        if (snapshot->toolId != node.toolId)
            return "上游节点 " + node.title() + " 的工具已变化，请先重新运行该节点";
        for (const auto &use : snapshot->inputUses)
        {
            const auto binding = node.inputs.find(use.inputPort);
            if (binding == node.inputs.end() || binding->second.mode != BindingMode::Upstream
                || !binding->second.source || binding->second.source->node != use.sourceNode
                || binding->second.source->port != use.sourcePort)
                return "上游节点 " + node.title() + " 的输入关系已变化，请先重新运行该节点";
            const auto sourceIt = frozen.nodes.find(use.sourceNode);
            if (sourceIt == frozen.nodes.end())
                return "上游节点 " + node.title() + " 引用的源节点不存在";
            const auto sourceRuntime = runtimes.find(use.sourceNode);
            const auto sourceSnapshot = sourceRuntime == runtimes.end() ? ResultSlot::Snapshot{}
                                                                       : sourceRuntime->second->slot.latest();
            if (!sourceSnapshot || sourceSnapshot->publication != use.sourcePublication)
                return "上游节点 " + sourceIt->second->title() + " 已重新运行，请先重新运行节点 " + node.title();
            const auto input = node.signatures.inputs.find(use.inputPort);
            const auto output = sourceSnapshot->outputs.find(use.sourcePort);
            if (input == node.signatures.inputs.end() || output == sourceSnapshot->outputs.end()
                || isMissing(output->second))
                return "上游节点 " + sourceIt->second->title() + " 的输出不可用，请先重新运行该节点";
            const auto actual = typeOf(output->second);
            if (!actual || !compatible(*actual, input->second))
                return "上游节点 " + sourceIt->second->title() + " 的输出类型与 " + node.title() + " 的输入不匹配";
            if (auto error = check(use.sourceNode))
                return error;
        }
        visiting.erase(id);
        valid.insert(id);
        return std::nullopt;
    };

    const auto targetIt = frozen.nodes.find(target);
    if (targetIt == frozen.nodes.end())
        return std::string("目标节点不存在");
    for (const auto &[input, binding] : targetIt->second->inputs)
    {
        if (binding.mode != BindingMode::Upstream || !binding.source)
            continue;
        if (auto error = check(binding.source->node))
            return error;
    }
    return std::nullopt;
}
} // namespace

struct RuntimeEngine::Impl
{
    enum class Management
    {
        None,
        Reset,
        Refresh
    };

    explicit Impl(std::size_t requested)
        : executor(requested ? requested : std::max(2u, std::thread::hardware_concurrency()))
        , budget(std::make_shared<WaitBudget>(executor.num_workers()))
        , coordinator([this] { coordinate(); })
    {
    }

    ~Impl()
    {
        std::shared_ptr<std::stop_source> stop;
        {
            std::lock_guard lock(control);
            shutdown      = true;
            stopRequested = true;
            stop          = activeStop;
        }
        if (stop)
            stop->request_stop();
        commandReady.notify_one();
        coordinator.join();
    }

    tf::Executor                                                      executor;
    std::shared_ptr<WaitBudget>                                       budget;
    std::mutex                                                        control;
    std::condition_variable                                           commandReady;
    RuntimeState                                                      state          = RuntimeState::Idle;
    bool                                                              shutdown       = false;
    bool                                                              stopRequested  = false;
    bool                                                              commandPending = false;
    bool                                                              finishing      = false;
    Management                                                        management     = Management::None;
    std::function<ToolStatus()>                                       managementWork;
    std::shared_ptr<FrozenRun>                                        command;
    std::map<NodeId, std::shared_ptr<NodeRuntime>>                    nodes;
    std::map<std::string, std::shared_ptr<CompiledFlow>, std::less<>> plans;
    std::uint64_t                                                     generation      = 1;
    std::uint64_t                                                     session         = 0;
    std::uint64_t                                                     round           = 0;
    std::uint64_t                                                     completionToken = 0;
    std::optional<RuntimeCompletion>                                  completion;
    std::function<void(RuntimeCompletion)>                            callback;
    std::vector<FlowTiming>                                           flowTiming;
    double                                                            roundMilliseconds = 0;
    std::deque<RuntimeDiagnostic>                                     diagnostics;
    std::uint64_t                                                     nextDiagnosticSequence = 1;
    std::uint64_t                                                     deliveredDiagnosticSequence = 0;
    std::size_t                                                       droppedDiagnostics = 0;
    std::shared_ptr<std::stop_source>                                 activeStop;
    std::thread                                                       coordinator;

    void diagnose(std::string message)
    {
        std::lock_guard lock(control);
        if (diagnostics.size() == 100)
        {
            diagnostics.pop_front();
            ++droppedDiagnostics;
        }
        diagnostics.push_back({generation, nextDiagnosticSequence++, std::move(message)});
    }

    void resetDiagnostics()
    {
        diagnostics.clear();
        nextDiagnosticSequence      = 1;
        deliveredDiagnosticSequence = 0;
        droppedDiagnostics          = 0;
    }

    std::vector<std::shared_ptr<CompiledFlow>> compile(const std::shared_ptr<FrozenRun> &frozen)
    {
        std::vector<std::shared_ptr<NodeRuntime>> removed;
        {
            std::lock_guard lock(control);
            for (auto it = nodes.begin(); it != nodes.end();)
            {
                if (!frozen->nodes.contains(it->first))
                {
                    removed.push_back(std::move(it->second));
                    it = nodes.erase(it);
                }
                else
                    ++it;
            }
            for (const auto &[id, config] : frozen->nodes)
            {
                auto [item, inserted] = nodes.try_emplace(id);
                if (inserted)
                {
                    item->second = std::make_shared<NodeRuntime>(id, budget);
                    if (const auto *definition = frozen->registry.find(config->toolId))
                        item->second->kind = definition->kind;
                }
                frozen->runtimes.emplace(id, item->second);
            }
        }
        std::erase_if(plans,
                      [&](const auto &item)
                      {
                          return std::any_of(item.second->nodes.begin(), item.second->nodes.end(),
                                             [&](NodeId id) { return !frozen->nodes.contains(id); });
                      });
        for (auto &runtime : removed) runtime->release();
        std::vector<std::shared_ptr<CompiledFlow>> selected;
        for (const auto &flow : frozen->project.flows)
        {
            if (std::find(frozen->selected.begin(), frozen->selected.end(), flow.id) == frozen->selected.end())
                continue;
            auto edges = frozen->mode == RunMode::Node ? std::vector<DependencyEdge>{} : dependencyEdges(flow);
            std::vector<NodeId>         ids;
            std::vector<ToolDefinition> contracts;
            for (const auto &node : flow.nodes)
            {
                if (frozen->mode == RunMode::Node && node.id != *frozen->target)
                    continue;
                ids.push_back(node.id);
                contracts.push_back(frozen->contracts.at(node.id));
            }
            if (ids.empty())
                continue;
            // A manual invocation never replaces the cached full-flow graph.
            auto previous = frozen->mode == RunMode::Node ? plans.end() : plans.find(flow.id);
            bool reusable = previous != plans.end() && previous->second->revision == flow.structureRevision
                         && previous->second->nodes == ids && previous->second->edges == edges
                         && previous->second->contracts.size() == contracts.size();
            if (reusable)
                for (std::size_t i = 0; i < contracts.size(); ++i)
                    if (!sameContract(previous->second->contracts[i], contracts[i]))
                    {
                        reusable = false;
                        break;
                    }
            if (reusable)
            {
                selected.push_back(previous->second);
                continue;
            }
            auto plan       = std::make_shared<CompiledFlow>();
            plan->id        = flow.id;
            plan->revision  = flow.structureRevision;
            plan->nodes     = std::move(ids);
            plan->edges     = std::move(edges);
            plan->contracts = std::move(contracts);
            std::map<NodeId, tf::Task> tasks;
            for (auto id : plan->nodes)
            {
                auto  runtime = frozen->runtimes.at(id);
                auto *holder  = plan.get();
                tasks.emplace(id, plan->graph.emplace(
                                      [holder, runtime]
                                      {
                                          auto context = holder->activeRound;
                                          runtime->run(context);
                                      }));
            }
            for (const auto &[source, target] : plan->edges) tasks.at(source).precede(tasks.at(target));
            if (frozen->mode != RunMode::Node)
                plans[flow.id] = plan;
            selected.push_back(std::move(plan));
        }
        std::erase_if(plans,
                      [&](const auto &item)
                      {
                          return std::none_of(frozen->project.flows.begin(), frozen->project.flows.end(),
                                              [&](const auto &flow) { return flow.id == item.first; });
                      });
        return selected;
    }

    void run(const std::shared_ptr<FrozenRun> &frozen)
    {
        auto selected            = compile(frozen);
        bool infrastructureError = false;
        for (;;)
        {
            std::shared_ptr<const RoundContext> context;
            {
                std::lock_guard lock(control);
                if (stopRequested || shutdown)
                    break;
                // Stop and the all-selected-flows commitment share this exact decision point.
                context = std::make_shared<const RoundContext>(
                    RoundContext{frozen, generation, session, ++round, activeStop});
            }

            struct CompletionClock
            {
                Clock::time_point start, end;
            };

            struct Submission
            {
                std::shared_ptr<CompiledFlow>    plan;
                std::shared_ptr<CompletionClock> clock;
                tf::Future<void>                 future;
            };

            std::vector<Submission> submitted;
            submitted.reserve(selected.size());
            const auto roundStart = Clock::now();
            auto       roundEnd   = roundStart;
            for (const auto &plan : selected)
            {
                try
                {
                    plan->activeRound = context;
                    auto clock        = std::make_shared<CompletionClock>();
                    clock->start      = Clock::now();
                    auto future       = executor.run(plan->graph, [clock] { clock->end = Clock::now(); });
                    submitted.push_back({plan, std::move(clock), std::move(future)});
                }
                catch (const std::exception &error)
                {
                    infrastructureError = true;
                    diagnose(std::string("Taskflow submission failed: ") + error.what());
                    break;
                }
                catch (...)
                {
                    infrastructureError = true;
                    diagnose("Taskflow submission failed with an unknown exception");
                    break;
                }
            }
            std::vector<FlowTiming> timings;
            timings.reserve(submitted.size());
            for (auto &submission : submitted)
            {
                try
                {
                    submission.future.get();
                }
                catch (const std::exception &error)
                {
                    infrastructureError = true;
                    diagnose(std::string("Taskflow completion failed: ") + error.what());
                }
                catch (...)
                {
                    infrastructureError = true;
                    diagnose("Taskflow completion failed with an unknown exception");
                }
                if (submission.clock->end != Clock::time_point{})
                {
                    timings.push_back(
                        {submission.plan->id, milliseconds(submission.clock->end - submission.clock->start)});
                    roundEnd = std::max(roundEnd, submission.clock->end);
                }
            }
            for (const auto &plan : selected) plan->activeRound.reset();
            {
                std::lock_guard lock(control);
                flowTiming        = std::move(timings);
                roundMilliseconds = milliseconds(roundEnd - roundStart);
                for (auto &[id, runtime] : nodes) runtime->active.store(false, std::memory_order_release);
            }
            if (infrastructureError || frozen->mode != RunMode::Loop)
                break;
        }
    }

    void manage(Management action, std::function<ToolStatus()> work)
    {
        std::vector<std::shared_ptr<NodeRuntime>> affected;
        {
            std::lock_guard lock(control);
            for (auto &[id, runtime] : nodes) affected.push_back(runtime);
        }
        for (auto &runtime : affected) runtime->reset();
        plans.clear();
        if (work)
        {
            auto status = work();
            if (!status)
                diagnose(status.message);
        }
        if (action == Management::Reset)
        {
            std::lock_guard lock(control);
            ++generation;
            session = 0;
            round   = 0;
            flowTiming.clear();
            roundMilliseconds = 0;
            resetDiagnostics();
        }
    }

    void coordinate() noexcept
    {
        for (;;)
        {
            std::shared_ptr<FrozenRun> frozen;
            {
                std::unique_lock lock(control);
                commandReady.wait(lock, [&] { return commandPending || shutdown; });
                if (shutdown && !commandPending)
                    break;
                commandPending = false;
                frozen         = std::move(command);
            }
            try
            {
                if (frozen)
                    run(frozen);
            }
            catch (const std::exception &error)
            {
                diagnose(std::string("Runtime setup failed: ") + error.what());
            }
            catch (...)
            {
                diagnose("Runtime setup failed with an unknown exception");
            }
            Management                  action;
            std::function<ToolStatus()> work;
            {
                std::lock_guard lock(control);
                finishing = true;
                action    = management;
                work      = std::move(managementWork);
                if (action == Management::Reset)
                    state = RuntimeState::Resetting;
                if (action == Management::Refresh)
                    state = RuntimeState::Refreshing;
            }
            try
            {
                if (action != Management::None)
                    manage(action, std::move(work));
            }
            catch (const std::exception &error)
            {
                diagnose(std::string("Runtime management failed: ") + error.what());
            }
            catch (...)
            {
                diagnose("Runtime management failed with an unknown exception");
            }
            frozen.reset();
            std::function<void(RuntimeCompletion)> notify;
            RuntimeCompletion                      finished;
            {
                std::lock_guard lock(control);
                management = Management::None;
                for (auto &[id, runtime] : nodes) runtime->active.store(false, std::memory_order_release);
                finished   = {generation, ++completionToken};
                completion = finished;
                finishing  = false;
                notify     = callback;
            }
            if (notify)
            {
                try
                {
                    notify(finished);
                }
                catch (const std::exception &error)
                {
                    diagnose(std::string("Completion callback failed: ") + error.what());
                }
                catch (...)
                {
                    diagnose("Completion callback failed with an unknown exception");
                }
            }
        }
        plans.clear();
        for (auto &[id, runtime] : nodes) runtime->release();
        nodes.clear();
    }
};

RuntimeEngine::RuntimeEngine(std::size_t workers)
    : impl_(std::make_unique<Impl>(workers))
{
}

RuntimeEngine::~RuntimeEngine() = default;

std::size_t RuntimeEngine::workers() const noexcept
{
    return impl_->executor.num_workers();
}

ToolStatus RuntimeEngine::start(const ProjectDefinition &project, const ToolRegistry &registry, RunMode mode,
                                std::optional<NodeId> target)
{
    std::lock_guard lock(impl_->control);
    if (impl_->state != RuntimeState::Idle || impl_->shutdown)
        return {1, "runtime is busy"};
    try
    {
        auto frozen      = std::make_shared<FrozenRun>();
        frozen->project  = project;
        frozen->registry = registry;
        frozen->mode     = mode;
        frozen->target   = target;
        for (auto &flow : frozen->project.flows)
            for (auto &node : flow.nodes)
                if (const auto *definition = registry.find(node.toolId))
                    setOutputDeclarations(node, *definition);
                else
                {
                    node.signatures.outputs.clear();
                    node.outputTypeSources.clear();
                }
        resolveOutputTypes(frozen->project);
        std::set<std::string> flowIds;
        bool                  nonempty    = false;
        bool                  targetFound = false;
        for (const auto &flow : frozen->project.flows)
        {
            if (!flowIds.insert(flow.id).second)
                throw ContractError("duplicate flow identity");
            bool containsTarget = false;
            for (const auto &node : flow.nodes)
            {
                if (!frozen->nodes.emplace(node.id, &node).second)
                    throw ContractError("duplicate node identity");
                if (target && node.id == *target)
                    containsTarget = targetFound = true;
            }
            const bool selected = mode == RunMode::Node ? containsTarget : flow.runSelected;
            if (!selected)
                continue;
            frozen->selected.push_back(flow.id);
            nonempty = nonempty || !flow.nodes.empty();
            for (const auto &node : flow.nodes)
            {
                if (mode == RunMode::Node && node.id != *target)
                    continue;
                const auto *definition = frozen->registry.find(node.toolId);
                if (!definition)
                    throw ContractError("selected node tool definition is missing: " + node.toolId);
                frozen->contracts.emplace(node.id, nodeContract(node, *definition));
                auto &parameters = frozen->initParameters[node.id];
                for (const auto &port : definition->initParams)
                {
                    const auto value = node.initParams.find(port.id);
                    parameters.emplace(port.id, value == node.initParams.end() ? port.defaultValue : value->second);
                }
            }
            if (mode != RunMode::Node && hasDependencyCycle(flow))
                throw ContractError("selected flow contains a dependency cycle");
        }
        if (mode == RunMode::Node && (!target || !targetFound))
            return {1, "target node does not exist"};
        if (frozen->selected.empty() || !nonempty)
            return {1, "select at least one nonempty flow"};
        if (mode == RunMode::Node)
        {
            if (auto error = upstreamValidityError(*frozen, *target, impl_->generation, impl_->nodes))
                return {1, *error};
        }
        impl_->state         = mode == RunMode::Node ? RuntimeState::RunningNode
                             : mode == RunMode::Loop ? RuntimeState::RunningLoop
                                                     : RuntimeState::RunningOnce;
        impl_->stopRequested = false;
        impl_->activeStop    = std::make_shared<std::stop_source>();
        impl_->completion.reset();
        impl_->resetDiagnostics();
        impl_->flowTiming.clear();
        impl_->roundMilliseconds = 0;
        ++impl_->session;
        impl_->round          = 0;
        impl_->command        = std::move(frozen);
        impl_->commandPending = true;
        impl_->commandReady.notify_one();
        return {};
    }
    catch (const std::exception &error)
    {
        return {1, error.what()};
    }
}

void RuntimeEngine::requestStop()
{
    std::shared_ptr<std::stop_source> stop;
    {
        std::lock_guard lock(impl_->control);
        if (impl_->state == RuntimeState::RunningLoop || impl_->state == RuntimeState::RunningOnce
            || impl_->state == RuntimeState::RunningNode)
        {
            impl_->stopRequested = true;
            impl_->state         = RuntimeState::Stopping;
            stop                 = impl_->activeStop;
        }
    }
    if (stop)
        stop->request_stop();
}

ToolStatus RuntimeEngine::requestReset()
{
    std::unique_lock lock(impl_->control);
    if (impl_->shutdown || impl_->finishing || impl_->management != Impl::Management::None || impl_->completion)
        return {1, "runtime management is already pending"};
    if (impl_->state == RuntimeState::Refreshing || impl_->state == RuntimeState::Resetting)
        return {1, "runtime management is already pending"};
    impl_->management    = Impl::Management::Reset;
    impl_->stopRequested = true;
    if (impl_->state == RuntimeState::Idle)
    {
        impl_->state = RuntimeState::Resetting;
        impl_->resetDiagnostics();
        impl_->commandPending = true;
        impl_->commandReady.notify_one();
    }
    else
        impl_->state = RuntimeState::Stopping;
    const auto stop = impl_->activeStop;
    lock.unlock();
    if (stop)
        stop->request_stop();
    return {};
}

ToolStatus RuntimeEngine::removeNodes(std::span<const NodeId> ids)
{
    std::vector<std::shared_ptr<NodeRuntime>> removed;
    {
        std::lock_guard lock(impl_->control);
        if (impl_->state != RuntimeState::Idle || impl_->shutdown || impl_->finishing
            || impl_->management != Impl::Management::None || impl_->completion)
            return {1, "runtime is busy"};
        for (const auto id : ids)
        {
            const auto found = impl_->nodes.find(id);
            if (found == impl_->nodes.end())
                continue;
            removed.push_back(std::move(found->second));
            impl_->nodes.erase(found);
        }
    }
    for (const auto &runtime : removed)
        if (auto status = runtime->release())
            impl_->diagnose(status->message);
    return {};
}

ToolStatus RuntimeEngine::requestRefresh(std::function<ToolStatus()> work)
{
    std::lock_guard lock(impl_->control);
    if (impl_->state != RuntimeState::Idle || impl_->shutdown)
        return {1, "refresh requires Idle"};
    impl_->management     = Impl::Management::Refresh;
    impl_->managementWork = std::move(work);
    impl_->state          = RuntimeState::Refreshing;
    impl_->resetDiagnostics();
    impl_->commandPending = true;
    impl_->commandReady.notify_one();
    return {};
}

RuntimeUpdate RuntimeEngine::pollLatest()
{
    std::lock_guard lock(impl_->control);
    RuntimeUpdate   update;
    update.state             = impl_->state;
    update.generation        = impl_->generation;
    update.session           = impl_->session;
    update.round             = impl_->round;
    update.flows             = impl_->flowTiming;
    update.roundMilliseconds = impl_->roundMilliseconds;
    for (const auto &diagnostic : impl_->diagnostics)
    {
        if (diagnostic.sequence <= impl_->deliveredDiagnosticSequence)
            continue;
        update.diagnostics.push_back(diagnostic);
        impl_->deliveredDiagnosticSequence = diagnostic.sequence;
    }
    update.droppedDiagnostics = impl_->droppedDiagnostics;
    impl_->droppedDiagnostics = 0;
    update.completion         = impl_->completion;
    update.nodes.reserve(impl_->nodes.size());
    for (const auto &[id, runtime] : impl_->nodes)
    {
        NodeUpdate item{id, runtime->active.load(std::memory_order_acquire), runtime->slot.latest(), {}};
        if (const auto status = runtime->lifecycle.load(std::memory_order_acquire))
            item.lifecycleDiagnostic = *status;
        update.nodes.push_back(std::move(item));
    }
    return update;
}

#ifdef VISIONRT_TEST_PROBES
void RuntimeEngine::injectDiagnosticForTest(std::string message)
{
    impl_->diagnose(std::move(message));
}
#endif

bool RuntimeEngine::acknowledgeCompletion(RuntimeCompletion completion)
{
    std::lock_guard lock(impl_->control);
    if (!impl_->completion || completion.generation != impl_->generation
        || completion.token != impl_->completion->token)
        return false;
    impl_->completion.reset();
    impl_->state = RuntimeState::Idle;
    return true;
}

void RuntimeEngine::setCompletionCallback(std::function<void(RuntimeCompletion)> callback)
{
    std::lock_guard lock(impl_->control);
    impl_->callback = std::move(callback);
}
} // namespace visionrt
