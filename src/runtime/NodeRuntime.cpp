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
        status = {-2, error.what()};
    }
    catch (const std::exception &error)
    {
        status = {-1, exceptionSummary(NodePhase::Create), error.what()};
    }
    catch (...)
    {
        status = {-1, "工具实例创建发生未知异常"};
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
        status = {-4, exceptionSummary(NodePhase::Release), error.what()};
    }
    catch (...)
    {
        status = {-4, "工具释放发生未知异常"};
    }
    instance.reset();
    if (!status)
    {
        status.code    = -4;
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

    const auto started          = Clock::now();
    auto       snapshot         = std::make_shared<NodeSnapshot>();
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
    ToolResult      result;
    NodeDisposition disposition  = NodeDisposition::Running;
    std::string     gateMessage;
    auto            phase            = NodePhase::Prepare;
    bool        frameworkFailure = false;
    bool        enteredExecute   = false;

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
        snapshot->disposition = disposition;
        snapshot->status      = {kNotExecutedStatus, std::move(message)};
        snapshot->phase       = NodePhase::Prepare;
        for (const auto &port : definition->outputs)
            if (port.id != "__status_code" && port.id != "__status_message")
                snapshot->outputs.emplace(port.id, Missing{});
        snapshot->outputs.emplace("__status_code", kNotExecutedStatus);
        snapshot->outputs.emplace("__status_message", snapshot->status.message);
        snapshot->timing.totalMs = milliseconds(Clock::now() - started);
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
                    status = {-2, error.what()};
                }
                catch (const std::exception &error)
                {
                    status = {-1, exceptionSummary(NodePhase::Initialize), error.what()};
                }
                catch (...)
                {
                    status = {-1, "未知的初始化异常"};
                }
                initTimer.stop();
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
            }
            if (!result.status)
            {
                if (const auto *diagnostics = dynamic_cast<const IToolFailureContext *>(instance.get()))
                    phase = diagnostics->failurePhase();
            }
            snapshot->timing.adapterExecuteMs = snapshot->timing.executeMs;
            if (auto *py = dynamic_cast<IPythonExecutionMetrics *>(instance.get()))
            {
                const auto m                    = py->pythonMetrics();
                snapshot->timing.waitGilMs      = m.waitGilMs;
                snapshot->timing.pythonInputMs  = m.pythonInputMs;
                snapshot->timing.pythonCallMs   = m.pythonCallMs;
                snapshot->timing.pythonOutputMs = m.pythonOutputMs;
            }
        }
        else
        {
            const auto diagnostic = preparation.load(std::memory_order_acquire);
            result.status         = diagnostic->status;
            phase                 = diagnostic->phase;
            frameworkFailure      = true;
        }
        snapshot->timing.adapterExecuteMs = snapshot->timing.executeMs;
        frameworkFailure = frameworkFailure || result.status.code < 0;
        if (!frameworkFailure)
        {
            const auto invocationPhase = phase;
            phase                      = NodePhase::Convert;
            ScopeTimer convertTimer(snapshot->timing.convertMs, &snapshot->timing.validateMeasured);
            validateUtf8(result.status.message);
            validateUtf8(result.status.detail);
            const auto businessCount
                = std::count_if(definition->outputs.begin(), definition->outputs.end(), [](const auto &port)
                                { return port.id != "__status_code" && port.id != "__status_message"; });
            if (result.outputs.size() != static_cast<std::size_t>(businessCount))
                throw ContractError("输出端口与声明不一致");
            for (const auto &port : definition->outputs)
            {
                if (port.id == "__status_code" || port.id == "__status_message")
                    continue;
                const auto output = result.outputs.find(port.id);
                if (output == result.outputs.end())
                    throw ContractError("缺少输出端口：" + port.id);
                validateToolOutput(port, output->second, result.status);
            }
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
            convertTimer.stop();
            phase = invocationPhase;
        }
        snapshot->timing.validateMs = snapshot->timing.convertMs;
    }
    catch (const RegistryError &error)
    {
        result.status         = error.status;
        frameworkFailure      = true;
    }
    catch (const ContractError &error)
    {
        result.status    = {-2, error.what()};
        frameworkFailure = true;
    }
    catch (const std::exception &error)
    {
        result.status    = {-1, exceptionSummary(phase), error.what()};
        frameworkFailure = true;
    }
    catch (...)
    {
        result.status    = {-1, exceptionSummary(phase), "未知异常"};
        frameworkFailure = true;
    }
    snapshot->timing.adapterExecuteMs = snapshot->timing.executeMs;
    snapshot->timing.validateMs       = snapshot->timing.convertMs;
    if (enteredExecute && instance)
    {
        if (auto *py = dynamic_cast<IPythonExecutionMetrics *>(instance.get()))
        {
            const auto m                    = py->pythonMetrics();
            snapshot->timing.waitGilMs      = m.waitGilMs;
            snapshot->timing.pythonInputMs  = m.pythonInputMs;
            snapshot->timing.pythonCallMs   = m.pythonCallMs;
            snapshot->timing.pythonOutputMs = m.pythonOutputMs;
        }
    }
    if (frameworkFailure)
    {
        result.outputs.clear();
        snapshot->roiInputs.clear();
        if (definition)
            for (const auto &port : definition->outputs)
                if (port.id != "__status_code" && port.id != "__status_message")
                    result.outputs.emplace(port.id, Missing{});
    }
    snapshot->status  = normalizeExecutionStatus(std::move(result.status));
    if (disposition == NodeDisposition::Running)
    {
        if (result.status.code == kCancelledStatus)
            disposition = NodeDisposition::Canceled;
        else if (result.status.code == 0)
            disposition = NodeDisposition::Succeeded;
        else
            disposition = NodeDisposition::Failed;
    }
    snapshot->disposition = disposition;
    snapshot->phase   = phase;
    snapshot->outputs = std::move(result.outputs);
    snapshot->outputs.emplace("__status_code", snapshot->status.code);
    snapshot->outputs.emplace("__status_message", std::string(snapshot->status.message));
    snapshot->timing.totalMs = milliseconds(Clock::now() - started);
    slot.publish(std::move(snapshot));
}
} // namespace visionrt::runtime_detail
