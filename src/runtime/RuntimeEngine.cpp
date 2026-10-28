#include "core/ProjectEditor.h"
#include "core/NodeContracts.h"
#include "core/ToolValueValidation.h"
#include "runtime/RuntimeInternal.h"

#include <taskflow/taskflow.hpp>

#include <algorithm>
#include <condition_variable>
#include <deque>
#include <limits>
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
        && a.entry == b.entry && a.source == b.source && samePorts(a.inputs, b.inputs)
        && samePorts(a.outputs, b.outputs) && samePorts(a.initParams, b.initParams);
}

double milliseconds(Clock::duration duration)
{
    return std::chrono::duration<double, std::milli>(duration).count();
}

double nanosecondsToMilliseconds(std::int64_t nanoseconds)
{
    return static_cast<double>(nanoseconds) / 1e6;
}

const char *exceptionSummary(RuntimePhase phase)
{
    switch (phase)
    {
    case RuntimePhase::Setup:
        return "运行准备异常";
    case RuntimePhase::Submission:
        return "任务提交异常";
    case RuntimePhase::Completion:
        return "任务完成异常";
    case RuntimePhase::Management:
        return "运行时管理异常";
    case RuntimePhase::Notification:
        return "运行完成通知异常";
    }
    std::terminate();
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
        // Reuse does not require a successful business status: a failed publication with
        // valid outputs is a legitimate, current input.
        if (snapshot->disposition == NodeDisposition::Skipped || snapshot->disposition == NodeDisposition::Blocked)
            return "上游节点 " + node.title() + " 本轮未执行，请先运行该节点";
        if (node.executionGuard && node.executionGuard->mode == BindingMode::Upstream
            && node.executionGuard->source)
        {
            const auto guard = runtimes.find(node.executionGuard->source->node);
            const auto flag  = guard == runtimes.end() ? ResultSlot::Snapshot{} : guard->second->slot.latest();
            if (!flag || flag->generation != generation)
                return "节点 " + node.title() + " 的执行条件在本轮不可用，请先运行条件来源";
        }
        if (snapshot->toolId != node.toolId)
            return "上游节点 " + node.title() + " 的工具已变化，请先重新运行该节点";
        const auto *definition = frozen.registry.find(node.toolId);
        if (!definition)
            return "上游节点 " + node.title() + " 的工具定义缺失";
        const auto contract = nodeContract(node, *definition);
        if (snapshot->outputTypes != resolvedOutputTypes(contract))
            return "上游节点 " + node.title() + " 的输出合同已变化，请先重新运行该节点";
        try
        {
            validateToolValues(contract.outputs, snapshot->outputs);
        }
        catch (const ContractError &error)
        {
            return "上游节点 " + node.title() + " 的输出值不符合当前声明：" + error.what();
        }
        std::map<std::string, const PortDefinition *, std::less<>> inputs;
        for (const auto &port : contract.inputs) inputs.emplace(port.id, &port);
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
            const auto input      = inputs.find(use.inputPort);
            const auto output = sourceSnapshot->outputs.find(use.sourcePort);
            const auto outputType = sourceSnapshot->outputTypes.find(use.sourcePort);
            if (input == inputs.end() || output == sourceSnapshot->outputs.end()
                || outputType == sourceSnapshot->outputTypes.end())
                return "上游节点 " + sourceIt->second->title() + " 的输出不可用，请先重新运行该节点";
            if (!compatible(outputType->second, input->second->type))
                return "上游节点 " + sourceIt->second->title() + " 的输出类型与 " + node.title() + " 的输入不匹配";
            try
            {
                // Structure only: range/option normalisation happens at the call boundary,
                // so a currently out-of-range publication is reusable, not a stale one.
                validateValue(input->second->type, output->second);
            }
            catch (const ContractError &error)
            {
                return "上游节点 " + node.title() + " 的输入值不符合当前声明：" + error.what();
            }
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
        Refresh,
        Synchronize
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
            readback.reset();
            stop          = activeStop;
        }
        if (stop)
            stop->request_stop();
        commandReady.notify_all();
        if (coordinator.joinable())
            coordinator.join();
    }

    tf::Executor                                                      executor;
    std::shared_ptr<WaitBudget>                                       budget;
    std::mutex                                                        control;
    std::condition_variable                                           commandReady;
    RuntimeState                                                      state          = RuntimeState::Idle;
    RunMode                                                           sessionMode    = RunMode::Once;
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
    bool                                                              readbackEnabled = false;
    std::function<void(RuntimeReadback)>                              readbackCallback;
    std::optional<RuntimeReadback>                                    readback;
    std::vector<FlowTiming>                                           flowTiming;
    std::optional<double>                                             roundMilliseconds;
    Clock::time_point                                                 sessionStartTime;
    std::optional<Clock::time_point>                                  sessionEndTime;
    double                                                            sessionElapsedMs = 0;
    std::deque<RuntimeDiagnostic>                                     diagnostics;
    std::uint64_t                                                     nextDiagnosticSequence = 1;
    std::size_t                                                       droppedDiagnostics = 0;
    std::shared_ptr<std::stop_source>                                 activeStop;
    std::thread                                                       coordinator;

    void diagnose(RuntimePhase phase, RuntimeDiagnostic::Payload payload)
    {
        std::lock_guard lock(control);
        if (diagnostics.size() == 100)
        {
            diagnostics.pop_front();
            ++droppedDiagnostics;
        }
        diagnostics.push_back({generation, nextDiagnosticSequence++, phase, std::move(payload)});
    }

    template<class Work>
    bool attempt(RuntimePhase phase, Work &&work)
    {
        try
        {
            work();
            return true;
        }
        catch (const RegistryError &error)
        {
            diagnose(phase, error.status);
        }
        catch (const ContractError &error)
        {
            diagnose(phase, makeStatus(ErrorCode::InvalidArgument, error.what()));
        }
        catch (const std::exception &error)
        {
            diagnose(phase, makeStatus(ErrorCode::Internal, exceptionSummary(phase), error.what()));
        }
        catch (...)
        {
            diagnose(phase, makeStatus(ErrorCode::Internal, exceptionSummary(phase), "未知异常"));
        }
        return false;
    }

    void synchronize(const ProjectDefinition &project, const ToolRegistry &registry, const std::set<NodeId> &retry = {})
    {
        std::map<NodeId, const NodeDefinition *> definitions;
        for (const auto &flow : project.flows)
            for (const auto &node : flow.nodes)
                if (!definitions.emplace(node.id, &node).second)
                    throw ContractError("节点标识重复：" + std::to_string(node.id));
        std::vector<std::shared_ptr<NodeRuntime>> removed;
        std::vector<std::pair<std::shared_ptr<NodeRuntime>, const NodeDefinition *>> configured;
        {
            std::lock_guard lock(control);
            for (auto it = nodes.begin(); it != nodes.end();)
            {
                if (!definitions.contains(it->first))
                {
                    removed.push_back(std::move(it->second));
                    it = nodes.erase(it);
                }
                else
                    ++it;
            }
            for (const auto &[id, definition] : definitions)
            {
                auto [item, inserted] = nodes.try_emplace(id);
                if (inserted)
                    item->second = std::make_shared<NodeRuntime>(id, budget);
                configured.emplace_back(item->second, definition);
            }
        }
        std::erase_if(plans,
                      [&](const auto &item)
                      {
                          return std::any_of(item.second->nodes.begin(), item.second->nodes.end(),
                                             [&](NodeId id) { return !definitions.contains(id); });
                      });
        for (auto &runtime : removed)
            if (auto status = runtime->release())
                diagnose(RuntimePhase::Management, *status);
        for (const auto &[runtime, node] : configured)
            runtime->configure(*node, registry, InstanceConfiguration(*node, registry, project.path.parent_path()),
                               retry.contains(node->id));
    }

    std::vector<std::shared_ptr<CompiledFlow>> compile(const std::shared_ptr<FrozenRun> &frozen)
    {
        std::set<NodeId> retry;
        for (const auto &[id, contract] : frozen->contracts) retry.insert(id);
        synchronize(frozen->project, frozen->registry, retry);
        {
            std::lock_guard lock(control);
            frozen->runtimes = nodes;
        }
        std::vector<std::shared_ptr<CompiledFlow>> selected;
        for (const auto &flow : frozen->project.flows)
        {
            if (std::find(frozen->selected.begin(), frozen->selected.end(), flow.id) == frozen->selected.end())
                continue;
            if (frozen->mode == RunMode::Node && frozen->target)
            {
                for (const auto &node : flow.nodes)
                {
                    if (node.id != *frozen->target)
                        continue;
                    for (const auto &[input, binding] : node.inputs)
                    {
                        if (binding.mode == BindingMode::Upstream && binding.source)
                        {
                            const auto check = validateBinding(flow, binding.source->node, binding.source->port, node.id, input);
                            if (!check)
                                throw ContractError("单节点执行失败：" + check.message);
                        }
                    }
                }
            }
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

            struct Submission
            {
                std::shared_ptr<CompiledFlow> plan;
                tf::Future<void>              future;
            };

            std::vector<Submission> submitted;
            submitted.reserve(selected.size());

            for (const auto &plan : selected)
            {
                if (!attempt(RuntimePhase::Submission,
                             [&]
                             {
                                 plan->activeRound = context;
                                 submitted.push_back({plan, executor.run(plan->graph)});
                             }))
                {
                    infrastructureError = true;
                    break;
                }
            }
            for (auto &submission : submitted)
                if (!attempt(RuntimePhase::Completion, [&] { submission.future.get(); }))
                    infrastructureError = true;
            for (const auto &plan : selected) plan->activeRound.reset();

            // 流程与全流程都从节点自己发布的执行窗口推导，不再另起一套计时：
            //   流程 = 该流程内所有已执行节点的 [startedNs, endedNs] 并集；
            //   全流程 = 本轮所有流程窗口的并集。
            // 因此单节点单流程时，节点总计、流程、全流程是同一个区间，数值必然相等；多节点/多流程
            // 的差额只来自节点之间真实的调度间隙与并发错峰。
            std::vector<FlowTiming> timings;
            timings.reserve(submitted.size());
            auto roundFirst = std::numeric_limits<std::int64_t>::max();
            auto roundLast  = std::numeric_limits<std::int64_t>::min();
            for (const auto &submission : submitted)
            {
                auto first = std::numeric_limits<std::int64_t>::max();
                auto last  = std::numeric_limits<std::int64_t>::min();
                for (auto id : submission.plan->nodes)
                {
                    const auto snapshot = nodes.at(id)->slot.latest();
                    if (!snapshot || snapshot->generation != context->generation
                        || snapshot->session != context->session || snapshot->round != context->round
                        || snapshot->startedNs == 0 || snapshot->endedNs < snapshot->startedNs)
                        continue;
                    first = std::min(first, snapshot->startedNs);
                    last  = std::max(last, snapshot->endedNs);
                }
                if (first <= last)
                {
                    timings.push_back({submission.plan->id, nanosecondsToMilliseconds(last - first)});
                    roundFirst = std::min(roundFirst, first);
                    roundLast  = std::max(roundLast, last);
                }
                else
                {
                    // 整条流程本轮没有节点真正执行：报 0 而不是沿用上一轮的旧值。
                    timings.push_back({submission.plan->id, 0.0});
                }
            }
            {
                std::unique_lock lock(control);
                flowTiming = std::move(timings);
                roundMilliseconds = roundFirst <= roundLast
                                      ? std::optional<double>(nanosecondsToMilliseconds(roundLast - roundFirst))
                                      : std::nullopt;
                sessionElapsedMs = milliseconds(Clock::now() - sessionStartTime);
                for (auto &[id, runtime] : nodes) runtime->active.store(false, std::memory_order_release);
                std::function<void(RuntimeReadback)> notifyReadback;
                std::optional<RuntimeReadback>       currentReadback;
                const bool requiresReadback = readbackEnabled && !shutdown && frozen->mode != RunMode::Loop;
                if (requiresReadback)
                {
                    readback = RuntimeReadback{generation, session, round};
                    currentReadback = readback;
                    notifyReadback = readbackCallback;
                }
                lock.unlock();
                if (notifyReadback && currentReadback)
                    notifyReadback(*currentReadback);
                lock.lock();
                if (requiresReadback && !shutdown && readback.has_value())
                {
                    commandReady.wait(lock, [&] { return !readback || shutdown; });
                }
            }
            if (infrastructureError || frozen->mode != RunMode::Loop)
                break;
        }

        if (frozen->mode == RunMode::Loop)
        {
            std::function<void(RuntimeReadback)> notifyFinal;
            std::optional<RuntimeReadback>       finalReadback;
            std::unique_lock lock(control);
            if (readbackEnabled && !shutdown)
            {
                readback = RuntimeReadback{generation, session, round};
                finalReadback = readback;
                notifyFinal = readbackCallback;
            }
            lock.unlock();
            if (notifyFinal && finalReadback)
                notifyFinal(*finalReadback);
            lock.lock();
            if (readbackEnabled && !shutdown && readback.has_value())
            {
                commandReady.wait(lock, [&] { return !readback || shutdown; });
            }
        }
    }

    void manage(Management action, std::function<ToolStatus()> work)
    {
        std::vector<std::shared_ptr<NodeRuntime>> affected;
        {
            std::lock_guard lock(control);
            for (auto &[id, runtime] : nodes) affected.push_back(runtime);
        }
        if (action != Management::Synchronize)
        {
            for (auto &runtime : affected) runtime->reset();
            plans.clear();
        }
        if (work)
        {
            auto status = work();
            if (!status)
                diagnose(RuntimePhase::Management, std::move(status));
        }
        if (action == Management::Reset)
        {
            std::lock_guard lock(control);
            ++generation;
            session = 0;
            round   = 0;
            flowTiming.clear();
            roundMilliseconds.reset();
            sessionElapsedMs  = 0;
            sessionEndTime.reset();
            diagnostics.clear();
            nextDiagnosticSequence = 1;
            droppedDiagnostics     = 0;
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
            if (frozen)
                attempt(RuntimePhase::Setup, [&] { run(frozen); });
            Management                  action;
            std::function<ToolStatus()> work;
            {
                std::lock_guard lock(control);
                finishing = true;
                action    = management;
                work      = std::move(managementWork);
                if (action == Management::Reset)
                    state = RuntimeState::Resetting;
                if (action == Management::Refresh || action == Management::Synchronize)
                    state = RuntimeState::Refreshing;
            }
            if (action != Management::None)
                attempt(RuntimePhase::Management, [&] { manage(action, std::move(work)); });
            frozen.reset();
            std::function<void(RuntimeCompletion)> notify;
            RuntimeCompletion                      finished;
            {
                std::lock_guard lock(control);
                sessionEndTime = Clock::now();
                sessionElapsedMs = milliseconds(*sessionEndTime - sessionStartTime);
                management = Management::None;
                for (auto &[id, runtime] : nodes) runtime->active.store(false, std::memory_order_release);
                finished   = {generation, ++completionToken};
                completion = finished;
                finishing  = false;
                notify     = callback;
            }
            if (notify)
                attempt(RuntimePhase::Notification, [&] { notify(finished); });
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

RuntimeState RuntimeEngine::state() const
{
    std::lock_guard lock(impl_->control);
    return impl_->state;
}

ErrorInfo RuntimeEngine::start(const ProjectDefinition &project, const ToolRegistry &registry, RunMode mode,
                               std::optional<NodeId> target)
{
    std::lock_guard lock(impl_->control);
    if (impl_->state != RuntimeState::Idle || impl_->shutdown || impl_->readback.has_value())
        return {ErrorCode::Busy, "运行时忙或读回尚未完成，不能开始执行"};
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
                throw ContractError("流程标识重复：" + flow.id);
            bool containsTarget = false;
            for (const auto &node : flow.nodes)
            {
                if (!frozen->nodes.emplace(node.id, &node).second)
                    throw ContractError("节点标识重复：" + std::to_string(node.id));
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
                    throw ContractError("执行节点的工具定义缺失：" + node.toolId);
                frozen->contracts.emplace(node.id, nodeContract(node, *definition));
            }
            if (mode != RunMode::Node && hasDependencyCycle(flow))
                throw ContractError("执行流程存在依赖循环：" + flow.id);
        }
        if (mode == RunMode::Node && (!target || !targetFound))
            return {ErrorCode::NotFound, "目标节点不存在：" + (target ? std::to_string(*target) : "未指定")};
        if (frozen->selected.empty() || !nonempty)
            return {ErrorCode::FailedPrecondition, "请至少选择一个非空流程"};
        if (auto receiverError = validateReceiverPlan(*frozen))
            return {ErrorCode::InvalidArgument, receiverError->message, receiverError->detail};
        if (mode == RunMode::Node)
        {
            for (const auto &flow : frozen->project.flows)
            {
                for (const auto &node : flow.nodes)
                {
                    if (node.id != *target)
                        continue;
                    for (const auto &[input, binding] : node.inputs)
                    {
                        if (binding.mode == BindingMode::Upstream && binding.source)
                        {
                            const auto check = validateBinding(flow, binding.source->node, binding.source->port, node.id, input);
                            if (!check)
                                return {ErrorCode::InvalidArgument, check.message};
                        }
                    }
                }
            }
            if (auto error = upstreamValidityError(*frozen, *target, impl_->generation, impl_->nodes))
                return {ErrorCode::InvalidArgument, *error};
        }
        impl_->sessionMode   = mode;
        impl_->state         = mode == RunMode::Node ? RuntimeState::RunningNode
                             : mode == RunMode::Loop ? RuntimeState::RunningLoop
                                                     : RuntimeState::RunningOnce;
        impl_->stopRequested = false;
        impl_->activeStop    = std::make_shared<std::stop_source>();
        impl_->completion.reset();
        impl_->flowTiming.clear();
        impl_->roundMilliseconds.reset();
        impl_->sessionStartTime = Clock::now();
        impl_->sessionEndTime.reset();
        impl_->sessionElapsedMs = 0;
        ++impl_->session;
        impl_->round          = 0;
        impl_->command        = std::move(frozen);
        impl_->commandPending = true;
        impl_->commandReady.notify_one();
        return {};
    }
    catch (const RegistryError &error)
    {
        return {ErrorCode::FailedPrecondition, error.status.message, error.status.detail};
    }
    catch (const ContractError &error)
    {
        return {ErrorCode::InvalidArgument, error.what()};
    }
    catch (const std::exception &error)
    {
        return {ErrorCode::Internal, "运行准备异常", error.what()};
    }
}

void RuntimeEngine::setReadbackEnabled(bool enabled)
{
    {
        std::lock_guard lock(impl_->control);
        impl_->readbackEnabled = enabled;
        if (!enabled)
            impl_->readback.reset();
    }
    impl_->commandReady.notify_all();
}

bool RuntimeEngine::acknowledgeReadback(RuntimeReadback readback)
{
    {
        std::lock_guard lock(impl_->control);
        if (!impl_->readback || *impl_->readback != readback)
            return false;
        impl_->readback.reset();
    }
    impl_->commandReady.notify_all();
    return true;
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
    impl_->commandReady.notify_all();
}

ErrorInfo RuntimeEngine::requestReset()
{
    std::unique_lock lock(impl_->control);
    if (impl_->shutdown || impl_->finishing || impl_->management != Impl::Management::None || impl_->completion)
        return {ErrorCode::Busy, "已有运行时管理操作尚未完成"};
    if (impl_->state == RuntimeState::Refreshing || impl_->state == RuntimeState::Resetting)
        return {ErrorCode::Busy, "已有运行时管理操作尚未完成"};
    impl_->management    = Impl::Management::Reset;
    impl_->stopRequested = true;
    if (impl_->state == RuntimeState::Idle)
    {
        impl_->state = RuntimeState::Resetting;
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

ErrorInfo RuntimeEngine::requestSynchronize(const ProjectDefinition &project, const ToolRegistry &registry)
{
    std::unique_lock lock(impl_->control);
    if (impl_->shutdown || impl_->finishing || impl_->management != Impl::Management::None || impl_->completion)
        return {ErrorCode::Busy, "运行时已有管理任务"};
    const bool       idle    = impl_->state == RuntimeState::Idle;
    bool             changed = !idle;
    std::set<NodeId> identities;
    for (const auto &flow : project.flows)
        for (const auto &node : flow.nodes)
        {
            if (!identities.insert(node.id).second)
                return {ErrorCode::InvalidArgument, "节点标识重复：" + std::to_string(node.id)};
            if (idle)
            {
                const auto found = impl_->nodes.find(node.id);
                if (found == impl_->nodes.end() || !found->second->configuration
                    || *found->second->configuration
                           != InstanceConfiguration(node, registry, project.path.parent_path()))
                    changed = true;
            }
        }
    if (!changed && identities.size() == impl_->nodes.size())
        return {};
    impl_->management     = Impl::Management::Synchronize;
    impl_->managementWork = [state = impl_.get(), project, registry]
    {
        state->synchronize(project, registry);
        return ToolStatus{};
    };
    if (idle)
    {
        impl_->state          = RuntimeState::Refreshing;
        impl_->commandPending = true;
        impl_->commandReady.notify_one();
    }
    else
    {
        impl_->stopRequested = true;
        impl_->state         = RuntimeState::Stopping;
    }
    const auto stop = idle ? std::shared_ptr<std::stop_source>{} : impl_->activeStop;
    lock.unlock();
    if (stop)
        stop->request_stop();
    return {};
}

ErrorInfo RuntimeEngine::requestRefresh(std::function<ToolStatus()> work)
{
    std::lock_guard lock(impl_->control);
    if (impl_->state != RuntimeState::Idle || impl_->shutdown)
        return {ErrorCode::FailedPrecondition, "只能在空闲状态刷新工具目录"};
    impl_->management     = Impl::Management::Refresh;
    impl_->managementWork = std::move(work);
    impl_->state          = RuntimeState::Refreshing;
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
    update.mode              = impl_->sessionMode;
    update.flows             = impl_->flowTiming;
    update.roundMilliseconds = impl_->roundMilliseconds;
    if (impl_->session > 0)
    {
        if (impl_->sessionEndTime)
            update.sessionElapsedMilliseconds = impl_->sessionElapsedMs;
        else
            update.sessionElapsedMilliseconds = milliseconds(Clock::now() - impl_->sessionStartTime);
    }
    else
    {
        update.sessionElapsedMilliseconds = 0;
    }
    update.diagnostics.reserve(impl_->diagnostics.size());
    for (auto &diagnostic : impl_->diagnostics) update.diagnostics.push_back(std::move(diagnostic));
    impl_->diagnostics.clear();
    update.droppedDiagnostics = impl_->droppedDiagnostics;
    impl_->droppedDiagnostics = 0;
    update.completion         = impl_->completion;
    update.readback           = impl_->readback;
    update.nodes.reserve(impl_->nodes.size());
    for (const auto &[id, runtime] : impl_->nodes)
    {
        NodeUpdate item{id, runtime->active.load(std::memory_order_acquire), runtime->slot.latest(), {}};
        if (const auto status = runtime->lifecycle.load(std::memory_order_acquire))
            item.lifecycleDiagnostic = status;
        if (const auto status = runtime->preparation.load(std::memory_order_acquire))
            item.instanceDiagnostic = status;
        update.nodes.push_back(std::move(item));
    }
    return update;
}

bool RuntimeEngine::acknowledgeCompletion(RuntimeCompletion completion)
{
    std::lock_guard lock(impl_->control);
    if (!impl_->completion || completion.generation != impl_->generation
        || completion.token != impl_->completion->token || impl_->readback.has_value())
        return false;
    impl_->completion.reset();
    impl_->state = RuntimeState::Idle;
    return true;
}

void RuntimeEngine::setReadbackCallback(std::function<void(RuntimeReadback)> callback)
{
    std::lock_guard lock(impl_->control);
    impl_->readbackCallback = std::move(callback);
}

void RuntimeEngine::setCompletionCallback(std::function<void(RuntimeCompletion)> callback)
{
    std::lock_guard lock(impl_->control);
    impl_->callback = std::move(callback);
}
} // namespace visionrt
