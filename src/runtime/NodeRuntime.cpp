#include "core/NodeContracts.h"
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

std::string phaseText(const char *phase)
{
    const std::string_view value = phase ? phase : "";
    if (value == "prepare") return "准备输入";
    if (value == "create")
        return "创建实例";
    if (value == "initialize") return "初始化";
    if (value == "execute") return "执行";
    if (value == "convert") return "校验输出";
    if (value == "release") return "释放";
    return std::string(value);
}

std::string location(NodeId node, const std::string &tool, const char *phase, const std::string &message)
{
    return "节点 " + std::to_string(node) + "（" + tool + "）" + phaseText(phase) + "失败：" + message;
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
        for (const auto &port : definition->initParams)
        {
            const auto value = node.initParams.find(port.id);
            parameters.emplace(port.id, value == node.initParams.end() ? port.defaultValue : value->second);
        }
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
    configuration = std::move(next);
    preparation.store({}, std::memory_order_release);
    ToolStatus status;
    try
    {
        const auto *definition = registry.find(node.toolId);
        if (!definition)
            throw RegistryError(ToolAvailability::DefinitionMissing, "工具定义缺失：" + node.toolId);
        PlatformToolServices services{PathResolver(configuration->directory), reader};
        instance = registry.create(nodeContract(node, *definition), services);
    }
    catch (const RegistryError &error)
    {
        status = error.status;
    }
    catch (const ContractError &error)
    {
        status = {-2, "节点工具合同不可用", error.what()};
    }
    catch (const std::exception &error)
    {
        status = {-1, "工具实例创建异常", error.what()};
    }
    catch (...)
    {
        status = {-1, "工具实例创建发生未知异常"};
    }
    if (!status)
    {
        status.message = location(id, node.toolId, "create", status.message);
        preparation.store(std::make_shared<const ToolStatus>(std::move(status)), std::memory_order_release);
    }
}

std::optional<ToolStatus> NodeRuntime::release() noexcept
{
    initialized = false;
    if (!instance)
        return std::nullopt;
    ToolStatus status;
    try
    {
        status = instance->release();
    }
    catch (const std::exception &error)
    {
        status = {-4, error.what()};
    }
    catch (...)
    {
        status = {-4, "unknown release exception"};
    }
    instance.reset();
    if (!status)
    {
        status.code    = -4;
        status.message = location(id, configuration->toolId, "release", status.message);
        lifecycle.store(std::make_shared<const ToolStatus>(status), std::memory_order_release);
        return status;
    }
    return std::nullopt;
}

void NodeRuntime::reset() noexcept
{
    lifecycle.store({}, std::memory_order_release);
    release();
    configuration.reset();
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
        bool              active = true;

        explicit ScopeTimer(double &destination)
            : target(destination)
        {
            target = 0.0;
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
    snapshot->executionRevision = config.executionRevision;
    ToolResult  result;
    const char *phase            = "prepare";
    bool        frameworkFailure = false;
    bool        enteredExecute   = false;
    try
    {
        Values inputs;
        {
            ScopeTimer prepareTimer(snapshot->timing.prepareMs);
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
                            value = found->second;
                        }
                    }
                }
                inputs.emplace(port.id, std::move(value));
            }
            validateToolValues(definition->inputs, inputs);
            prepareTimer.stop();
        }
        phase = "initialize";
        reader->bind(context);
        const auto &parameters = configuration->parameters;
        if (instance && !initialized)
        {
            validateToolValues(definition->initParams, parameters);
            ToolStatus status;
            {
                ScopeTimer initTimer(snapshot->timing.initMs);
                try
                {
                    if (definition->kind != ToolKind::PythonFunction)
                        status = instance->initialize(parameters);
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
                    status = {-1, error.what()};
                }
                catch (...)
                {
                    status = {-1, "未知的初始化异常"};
                }
                initTimer.stop();
            }
            if (!status)
            {
                status.message = location(id, config.toolId, "initialize", status.message);
                release();
                preparation.store(std::make_shared<const ToolStatus>(std::move(status)), std::memory_order_release);
            }
            else
                initialized = true;
        }
        phase = "execute";
        if (instance)
        {
            enteredExecute = true;
            snapshot->timing.toolCallMeasured = true;
            {
                ScopeTimer execTimer(snapshot->timing.executeMs);
                result = instance->execute(inputs);
                execTimer.stop();
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
            result.status    = *preparation.load(std::memory_order_acquire);
            frameworkFailure = true;
        }
        snapshot->timing.adapterExecuteMs = snapshot->timing.executeMs;
        if (result.status.code < 0)
        {
            frameworkFailure = true;
            if (instance && result.status.code != kCancelledStatus)
                result.status.message = location(id, config.toolId, "execute", result.status.message);
        }
        phase = "convert";
        if (!frameworkFailure)
        {
            ScopeTimer convertTimer(snapshot->timing.convertMs);
            validateUtf8(result.status.message);
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
                validateValue(port.type, output->second);
                if (result.status.code == 0 && isMissing(output->second))
                    throw ContractError("成功输出不能为 Missing：" + port.id);
            }
            for (const auto &output : definition->outputs)
            {
                if (output.sameGeometryAsInput.empty())
                    continue;
                for (const auto &input : definition->inputs)
                {
                    if ((input.type.type == Type::RoiBatch || input.type.type == Type::Rect)
                        && input.imageInput == output.sameGeometryAsInput)
                        snapshot->roiInputs.emplace(input.id, inputs.at(input.id));
                }
            }
            convertTimer.stop();
        }
        snapshot->timing.validateMs = snapshot->timing.convertMs;
    }
    catch (const RegistryError &error)
    {
        result.status         = error.status;
        result.status.message = location(id, config.toolId, phase, result.status.message);
        frameworkFailure      = true;
    }
    catch (const ContractError &error)
    {
        result.status    = {-2, location(id, config.toolId, phase, error.what())};
        frameworkFailure = true;
    }
    catch (const std::exception &error)
    {
        result.status    = {-1, location(id, config.toolId, phase, error.what())};
        frameworkFailure = true;
    }
    catch (...)
    {
        result.status    = {-1, location(id, config.toolId, phase, "未知异常")};
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
    snapshot->outputs = std::move(result.outputs);
    snapshot->outputs.emplace("__status_code", snapshot->status.code);
    snapshot->outputs.emplace("__status_message", Utf8(snapshot->status.message));
    snapshot->timing.totalMs = milliseconds(Clock::now() - started);
    slot.publish(std::move(snapshot));
}
} // namespace visionrt::runtime_detail
