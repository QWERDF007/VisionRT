#include "core/Error.h"
#include "core/InputNormalizer.h"
#include "core/NodeContracts.h"
#include "core/StatusCodes.h"
#include "runtime/RuntimeInternal.h"

#include <core/ToolValueValidation.h>

#include <algorithm>
#include <chrono>
#include <stdexcept>

namespace visionrt::runtime_detail {
namespace {
using Clock = std::chrono::steady_clock;

double milliseconds(Clock::duration duration)
{
    return std::chrono::duration<double, std::milli>(duration).count();
}

bool sameParameters(const Values &a, const Values &b)
{
    if (a.size() != b.size())
        return false;
    auto right = b.begin();
    for (const auto &[key, value] : a)
    {
        if (key != right->first || !equalValues(value, right->second))
            return false;
        ++right;
    }
    return true;
}

const char *exceptionSummary(NodePhase phase)
{
    switch (phase)
    {
    case NodePhase::Create:
        return "工具实例创建异常";
    case NodePhase::Prepare:
        return "准备输入异常";
    case NodePhase::Initialize:
        return "工具初始化异常";
    case NodePhase::Execute:
        return "工具执行异常";
    case NodePhase::Convert:
        return "输出校验异常";
    case NodePhase::Release:
        return "工具释放异常";
    }
    std::terminate();
}

std::unique_ptr<IToolInstance> createInstance(const NodeDefinition &node, const ToolRegistry &registry,
                                              const InstanceConfiguration &configuration,
                                              const std::shared_ptr<IResultReader> &reader, ToolStatus &status)
{
    try
    {
        const auto *definition = registry.find(node.toolId);
        if (!definition)
            throw RegistryError(ToolAvailability::DefinitionMissing, "工具定义缺失：" + node.toolId);
        PlatformToolServices services{PathResolver(configuration.directory), reader};
        return registry.create(nodeContract(node, *definition), services);
    }
    catch (const RegistryError &error)
    {
        status = error.status;
    }
    catch (const ContractError &error)
    {
        status = {hostCode(ErrorCode::InvalidArgument), error.what()};
    }
    catch (const std::exception &error)
    {
        status = {hostCode(ErrorCode::Internal), exceptionSummary(NodePhase::Create), error.what()};
    }
    catch (...)
    {
        status = {hostCode(ErrorCode::Internal), "工具实例创建发生未知异常"};
    }
    return {};
}
} // namespace

InstanceConfiguration::InstanceConfiguration(const NodeDefinition &node, const ToolRegistry &registry,
                                             const std::filesystem::path &directory)
    : toolId(node.toolId)
    , availability(registry.availability(node.toolId).state)
    , outputs(node.signatures.outputs)
    , directory(directory)
{
    if (const auto *definition = registry.find(toolId))
    {
        version = definition->version;
        kind    = definition->kind;
        parameters = nodeParameters(node, *definition);
    }
}

bool InstanceConfiguration::operator==(const InstanceConfiguration &other) const
{
    return toolId == other.toolId && version == other.version && kind == other.kind
        && availability == other.availability && outputs == other.outputs && directory == other.directory
        && sameParameters(parameters, other.parameters);
}

void NodeRuntime::configure(const NodeDefinition &node, const ToolRegistry &registry, InstanceConfiguration next,
                            bool retry)
{
    if (configuration && *configuration == next && (instance || !retry))
        return;
    release();
    configuration     = std::move(next);
    // Python declarations import and construct on the first execution; configuring a node must not load code.
    deferredCreate    = configuration->kind == ToolKind::PythonFunction
                   || configuration->kind == ToolKind::PythonClass;
    preparation.store({}, std::memory_order_release);
    if (deferredCreate)
        return;
    ToolStatus status;
    instance = createInstance(node, registry, *configuration, reader, status);
    if (!status)
    {
        preparation.store(std::make_shared<const NodeDiagnostic>(
                              NodeDiagnostic{id, node.toolId, NodePhase::Create, std::move(status)}),
                          std::memory_order_release);
    }
}

std::shared_ptr<const NodeDiagnostic> NodeRuntime::release() noexcept
{
    initialized = false;
    if (!instance)
        return {};
    ToolStatus status;
    try
    {
        status = instance->release();
    }
    catch (const std::exception &error)
    {
        status = {hostCode(ErrorCode::Internal), exceptionSummary(NodePhase::Release), error.what()};
    }
    catch (...)
    {
        status = {hostCode(ErrorCode::Internal), "工具释放发生未知异常"};
    }
    instance.reset();
    if (!status)
    {
        // The tool's own release verdict is kept as reported; the host only forwards it.
        auto diagnostic = std::make_shared<const NodeDiagnostic>(
            NodeDiagnostic{id, configuration->toolId, NodePhase::Release, std::move(status)});
        lifecycle.store(diagnostic, std::memory_order_release);
        return diagnostic;
    }
    return {};
}

void NodeRuntime::reset() noexcept
{
    lifecycle.store({}, std::memory_order_release);
    release();
    configuration.reset();
    deferredCreate = false;
    preparation.store({}, std::memory_order_release);
    slot.publish({});
    active.store(false, std::memory_order_release);
}

void NodeRuntime::run(const std::shared_ptr<const RoundContext> &context)
{
    active.store(true, std::memory_order_release);
    // A stop that landed before this task started must not enter execute or
    // publish a fabricated result for a node the user never observed running.
    if (context->stop && context->stop->stop_requested())
    {
        active.store(false, std::memory_order_release);
        return;
    }

    struct Activity
    {
        std::atomic<bool> &active;

        ~Activity()
        {
            active.store(false, std::memory_order_release);
        }
    } activity{active};

    struct ScopeTimer
    {
        Clock::time_point start = Clock::now();
        double           &target;
        bool             *measured = nullptr;
        bool              active   = true;

        explicit ScopeTimer(double &destination, bool *measuredFlag = nullptr)
            : target(destination)
            , measured(measuredFlag)
        {
            target = 0.0;
            if (measured)
                *measured = true;
        }

        ~ScopeTimer()
        {
            if (active)
                target = milliseconds(Clock::now() - start);
        }

        void stop()
        {
            if (active)
            {
                target = milliseconds(Clock::now() - start);
                active = false;
            }
        }
    };

    // One contiguous window per node execution: it starts when input preparation starts and ends
    // when the last measured stage ends. Framework work outside it (snapshot assembly, publishing)
    // belongs to the scheduler and shows up in the flow and session numbers instead, so the node
    // card, the hover card and the flow timing cannot disagree about the same node.
    Clock::time_point windowStart;
    Clock::time_point windowEnd;
    // Only a completed last stage closes the window; an exception closes it where it escaped, so the
    // stages that did run are never dropped and the cleanup after a failure is never counted.
    bool              windowClosed = false;
    auto              snapshot     = std::make_shared<NodeSnapshot>();
    snapshot->node              = id;
    snapshot->generation        = context->generation;
    snapshot->session           = context->session;
    snapshot->round             = context->round;
    snapshot->publication       = ++publication;
    const auto &frozen          = *context->frozen;
    const auto &config          = *frozen.nodes.at(id);
    const auto *definition      = &frozen.contracts.at(id);
    snapshot->toolId            = definition->id;
    snapshot->outputTypes       = resolvedOutputTypes(*definition);
    snapshot->executionRevision = config.executionRevision;
    // Python tools report their own inner stages; copying them in one place keeps a stage from being
    // counted twice or inherited from a previous round.
    const auto applyPythonMetrics = [&]
    {
        auto *python = dynamic_cast<IPythonExecutionMetrics *>(instance.get());
        if (!python)
            return;
        const auto metrics                      = python->pythonMetrics();
        snapshot->timing.pythonBridge           = metrics.pythonBridge;
        snapshot->timing.pythonInputMeasured    = metrics.pythonInputMeasured;
        snapshot->timing.pythonCallMeasured     = metrics.pythonCallMeasured;
        snapshot->timing.pythonOutputMeasured   = metrics.pythonOutputMeasured;
        snapshot->timing.waitGilMs              = metrics.waitGilMs;
        snapshot->timing.pythonInputMs          = metrics.pythonInputMs;
        snapshot->timing.pythonCallMs           = metrics.pythonCallMs;
        snapshot->timing.pythonOutputMs         = metrics.pythonOutputMs;
    };

    ToolResult      result;
    NodeDisposition disposition  = NodeDisposition::Running;
    std::string     gateMessage;
    auto            phase            = NodePhase::Prepare;
    // The framework's own verdict for this node. It is never derived from a tool's business status:
    // a tool that ran and answered is a success even when it reports a non-zero code.
    ToolStatus      frameworkStatus;
    ToolStatus      businessStatus;
    bool            frameworkFailure = false;
    bool            enteredExecute   = false;

    // Host gate: only an explicitly configured logical condition can skip a node.
    // Upstream business status never gates a normal tool; the tool decides.
    if (config.executionGuard && config.executionGuard->mode == BindingMode::Upstream
        && supportsExecutionGuard(definition->nodeModel))
    {
        const auto &guard = *config.executionGuard;
        if (!guard.source)
        {
            // Upstream mode without a bound source is a parameter error, not "always execute".
            disposition = NodeDisposition::Blocked;
            gateMessage = "执行条件尚未选择上层来源";
        }
        else
        {
            const auto upstream = frozen.runtimes.at(guard.source->node)->slot.latest();
            const bool current  = upstream && upstream->generation == context->generation
                               && upstream->session == context->session && upstream->round == context->round;
            const PortValue *published = nullptr;
            if (current)
            {
                const auto found = upstream->outputs.find(guard.source->port);
                if (found != upstream->outputs.end())
                    published = &found->second;
            }
            const auto *flag = published ? std::get_if<bool>(published) : nullptr;
            if (!current || upstream->disposition == NodeDisposition::Skipped
                || upstream->disposition == NodeDisposition::Blocked)
            {
                disposition = NodeDisposition::Blocked;
                gateMessage = "执行条件在本轮不可用，节点已阻塞";
            }
            else if (!flag)
            {
                disposition = NodeDisposition::Blocked;
                gateMessage = "执行条件必须是布尔值，节点已阻塞";
            }
            else if (!*flag)
            {
                // The user configured this logical control; a false flag is its normal result.
                disposition = NodeDisposition::Skipped;
                gateMessage = "执行条件为假";
            }
        }
    }
    const auto publishTerminal = [&](std::string message)
    {
        snapshot->disposition         = disposition;
        snapshot->status              = {kNotExecutedStatus, std::move(message)};
        snapshot->toolResultPublished = false;
        snapshot->phase               = NodePhase::Prepare;
        for (const auto &port : definition->outputs)
            if (port.id != kStatusCodePort && port.id != kStatusMessagePort)
                snapshot->outputs.emplace(port.id, Missing{});
        snapshot->outputs.emplace(std::string(kStatusCodePort), kNotExecutedStatus);
        snapshot->outputs.emplace(std::string(kStatusMessagePort), snapshot->status.message);
        // A gated node never entered a stage, so it has no execution window at all.
        snapshot->timing.totalMs = 0.0;
        slot.publish(std::move(snapshot));
    };

    if (disposition != NodeDisposition::Running)
    {
        publishTerminal(gateMessage);
        return;
    }
    try
    {
        Values inputs;
        {
            windowStart = Clock::now();
            ScopeTimer prepareTimer(snapshot->timing.prepareMs, &snapshot->timing.prepareMeasured);
            for (const auto &port : definition->inputs)
            {
                PortValue  value   = port.defaultValue;
                const auto binding = config.inputs.find(port.id);
                if (binding != config.inputs.end())
                {
                    const auto &input = binding->second;
                    if (input.mode == BindingMode::Literal)
                        value = input.literal;
                    else if (!input.source)
                        value = Missing{};
                    else
                    {
                        const auto upstream = frozen.runtimes.at(input.source->node)->slot.latest();
                        snapshot->inputUses.push_back(
                            {port.id, input.source->node, input.source->port, upstream ? upstream->publication : 0});
                        if (frozen.mode != RunMode::Node
                            && (!upstream || upstream->generation != context->generation
                                || upstream->session != context->session || upstream->round != context->round))
                        {
                            throw ContractError("上游结果不属于本次执行轮次");
                        }
                        if (!upstream)
                            value = Missing{};
                        else
                        {
                            const auto found = upstream->outputs.find(input.source->port);
                            if (found == upstream->outputs.end())
                                throw ContractError("上游输出端口不存在");
                            // A failed tool may still publish valid diagnostics or partial results;
                            // dropping them would hide data the tool actually produced.
                            value = found->second;
                        }
                    }
                }
                inputs.emplace(port.id, std::move(value));
            }
            inputs = normalizeInputs(definition->inputs, std::move(inputs));
            prepareTimer.stop();
            windowEnd = Clock::now();
        }
        if (disposition != NodeDisposition::Running)
        {
            publishTerminal(gateMessage);
            return;
        }
        double deferredCreateMs = 0.0;
        if (!instance && deferredCreate)
        {
            snapshot->timing.initMeasured = true;
            const auto createStart = Clock::now();
            ToolStatus status;
            instance = createInstance(config, frozen.registry, *configuration, reader, status);
            deferredCreateMs = milliseconds(Clock::now() - createStart);
            if (!status)
                preparation.store(std::make_shared<const NodeDiagnostic>(
                                      NodeDiagnostic{id, config.toolId, NodePhase::Create, std::move(status)}),
                                  std::memory_order_release);
            else
                preparation.store({}, std::memory_order_release);
        }
        phase = NodePhase::Initialize;
        reader->bind(context);
        const auto &parameters = configuration->parameters;
        if (instance && !initialized)
        {
            const auto normalizedParams = normalizeInputs(definition->initParams, parameters);
            ToolStatus status;
            {
                ScopeTimer initTimer(snapshot->timing.initMs, &snapshot->timing.initMeasured);
                try
                {
                    status = instance->initialize(normalizedParams);
                }
                catch (const RegistryError &error)
                {
                    status = error.status;
                }
                catch (const ContractError &error)
                {
                    status = {hostCode(ErrorCode::InvalidArgument), error.what()};
                }
                catch (const std::exception &error)
                {
                    status = {hostCode(ErrorCode::Internal), exceptionSummary(NodePhase::Initialize), error.what()};
                }
                catch (...)
                {
                    status = {hostCode(ErrorCode::Internal), "未知的初始化异常"};
                }
                initTimer.stop();
                windowEnd = Clock::now();
            }
            if (!status)
            {
                release();
                preparation.store(std::make_shared<const NodeDiagnostic>(
                                      NodeDiagnostic{id, config.toolId, NodePhase::Initialize, std::move(status)}),
                                  std::memory_order_release);
            }
            else
                initialized = true;
        }
        snapshot->timing.initMs += deferredCreateMs;
        phase = NodePhase::Execute;
        if (instance)
        {
            enteredExecute = true;
            snapshot->timing.toolCallMeasured = true;
            {
                ScopeTimer execTimer(snapshot->timing.executeMs);
                result = instance->execute(inputs);
                execTimer.stop();
                windowEnd = Clock::now();
            }
            snapshot->timing.adapterExecuteMs = snapshot->timing.executeMs;
            applyPythonMetrics();
        }
        else
        {
            // No instance ever existed, so there is no tool answer to show. The gate that stopped
            // the node owns its terminal state instead.
            const auto diagnostic = preparation.load(std::memory_order_acquire);
            frameworkStatus       = diagnostic->status;
            phase                 = diagnostic->phase;
            frameworkFailure      = true;
        }
        snapshot->timing.adapterExecuteMs = snapshot->timing.executeMs;
        {
            const auto invocationPhase = phase;
            phase                      = NodePhase::Convert;
            ScopeTimer convertTimer(snapshot->timing.convertMs, &snapshot->timing.validateMeasured);
            if (!frameworkFailure)
            {
                // The tool owns its declared business ports and its status. A non-zero business
                // code is legal data and never turns into a framework failure.
                businessStatus = validateToolExecution(definition->outputs, result.outputs, result.status);
                for (const auto &output : definition->outputs)
                {
                    if (output.sameGeometryAsInput.empty())
                        continue;
                    for (const auto &input : definition->inputs)
                    {
                        if (input.type.type == Type::ROI && input.imageInput == output.sameGeometryAsInput)
                            snapshot->roiInputs.emplace(input.id, inputs.at(input.id));
                    }
                }
            }
            convertTimer.stop();
            windowEnd    = Clock::now();
            windowClosed = true;
            phase = invocationPhase;
        }
        snapshot->timing.validateMs = snapshot->timing.convertMs;
    }
    catch (const RegistryError &error)
    {
        frameworkStatus  = error.status;
        frameworkFailure = true;
    }
    catch (const ContractError &error)
    {
        // A contract broken while the tool runs or its answer is published is bad data, not a bad
        // call: the caller passed nothing invalid, the tool answered outside its declaration. A
        // contract broken while the inputs are assembled is the caller's argument.
        const bool dataPhase = phase == NodePhase::Execute || phase == NodePhase::Convert;
        const auto code      = dataPhase ? ErrorCode::DataInvalid : ErrorCode::InvalidArgument;
        frameworkStatus  = {hostCode(code), error.what()};
        frameworkFailure = true;
    }
    catch (const std::exception &error)
    {
        frameworkStatus  = {hostCode(ErrorCode::Internal), exceptionSummary(phase), error.what()};
        frameworkFailure = true;
    }
    catch (...)
    {
        frameworkStatus  = {hostCode(ErrorCode::Internal), exceptionSummary(phase), "未知异常"};
        frameworkFailure = true;
    }
    snapshot->timing.adapterExecuteMs = snapshot->timing.executeMs;
    snapshot->timing.validateMs       = snapshot->timing.convertMs;
    if (enteredExecute && instance)
        applyPythonMetrics();
    if (frameworkFailure)
    {
        // The framework stopped the node, so it owns the terminal state. There is no tool answer to
        // publish: business ports are structurally Missing, but the status keys are only created by
        // a tool result and must never be synthesised here.
        result.outputs.clear();
        snapshot->roiInputs.clear();
        snapshot->status = std::move(frameworkStatus);
        for (const auto &port : definition->outputs)
            if (port.id != kStatusCodePort && port.id != kStatusMessagePort)
                result.outputs.emplace(port.id, Missing{});
    }
    else
    {
        // Only a tool that actually returned a result owns the two status ports. They are the
        // existing output ports the UI already shows, filled from the one status the tool reported.
        snapshot->status              = materialiseToolStatus(result.outputs, businessStatus);
        snapshot->toolResultPublished = true;
    }
    if (disposition == NodeDisposition::Running)
    {
        if (frameworkFailure)
            disposition = NodeDisposition::Failed;
        else if (context->stop && context->stop->stop_requested() && snapshot->status.code == kCancelledStatus)
            // Cancellation needs the scheduler fact; a tool's negative code alone never means the
            // user cancelled. The host receiver reports kCancelledStatus only after observing that
            // same stop, so both signals together identify a genuinely cancelled invocation.
            disposition = NodeDisposition::Canceled;
        else
            disposition = NodeDisposition::Succeeded;
    }
    snapshot->disposition = disposition;
    snapshot->phase   = phase;
    snapshot->outputs = std::move(result.outputs);
    if (windowStart != Clock::time_point{})
    {
        if (!windowClosed)
            windowEnd = Clock::now();
        snapshot->timing.totalMs = milliseconds(windowEnd - windowStart);
        snapshot->startedNs = std::chrono::duration_cast<std::chrono::nanoseconds>(windowStart.time_since_epoch()).count();
        snapshot->endedNs   = std::chrono::duration_cast<std::chrono::nanoseconds>(windowEnd.time_since_epoch()).count();
    }
    slot.publish(std::move(snapshot));
}
} // namespace visionrt::runtime_detail
