#include "runtime/RuntimeInternal.h"

#include <algorithm>
#include <chrono>
#include <stdexcept>

namespace visionrt::runtime_detail {
namespace {
using Clock = std::chrono::steady_clock;
double milliseconds(Clock::duration duration) {
    return std::chrono::duration<double, std::milli>(duration).count();
}
bool sameParameters(const Values& a, const Values& b) {
    if (a.size() != b.size()) return false;
    auto right = b.begin();
    for (const auto& [key, value] : a) {
        if (key != right->first || !equalValues(value, right->second)) return false;
        ++right;
    }
    return true;
}
std::string location(NodeId node, const std::string& tool, const char* phase,
                     const std::string& message) {
    return "node " + std::to_string(node) + " / " + tool + " / " + phase + ": " + message;
}
}
void NodeRuntime::release() noexcept {
    if (!instance) return;
    ToolStatus status;
    try {
        status = instance->release();
    } catch (const std::exception& error) {
        status = {-4, error.what()};
    } catch (...) {
        status = {-4, "unknown release exception"};
    }
    instance.reset();
    if (!status) {
        status.code = -4;
        status.message = location(id, toolId, "release", status.message);
        lifecycle.store(std::make_shared<const ToolStatus>(std::move(status)), std::memory_order_release);
    }
}
void NodeRuntime::reset() noexcept {
    lifecycle.store({}, std::memory_order_release);
    release();
    initializationFailure.reset();
    failureSession = 0;
    configurationSession = 0;
    initializedWith.clear();
    slot.publish({});
    active.store(false, std::memory_order_release);
}
void NodeRuntime::run(const std::shared_ptr<const RoundContext>& context) {
    active.store(true, std::memory_order_release);
    struct Activity {
        std::atomic<bool>& active;
        ~Activity() { active.store(false, std::memory_order_release); }
    } activity{active};
    const auto started = Clock::now();
    auto phaseStarted = started;
    auto snapshot = std::make_shared<NodeSnapshot>();
    snapshot->node = id;
    snapshot->generation = context->generation;
    snapshot->session = context->session;
    snapshot->round = context->round;
    snapshot->publication = ++publication;
    const auto& frozen = *context->frozen;
    const auto& config = *frozen.nodes.at(id);
    const auto* definition = frozen.registry.find(config.toolId);
    snapshot->executionRevision = config.executionRevision;
    ToolResult result;
    const char* phase = "prepare";
    double* elapsed = &snapshot->timing.prepareMs;
    bool frameworkFailure = false;
    try {
        if (!definition) throw RegistryError(ToolAvailability::DefinitionMissing, "tool definition missing");
        Values inputs;
        for (const auto& port : definition->inputs) {
            PortValue value = port.defaultValue;
            const auto binding = config.inputs.find(port.id);
            if (binding != config.inputs.end()) {
                const auto& input = binding->second;
                if (input.mode == BindingMode::Literal) value = input.literal;
                else if (!input.source) value = Missing{};
                else {
                    const auto upstream = frozen.runtimes.at(input.source->node)->slot.latest();
                    if (frozen.mode != RunMode::Node && (!upstream ||
                        upstream->generation != context->generation || upstream->session != context->session ||
                        upstream->round != context->round)) {
                        throw ContractError("predecessor is not from this session/round");
                    }
                    if (!upstream) value = Missing{};
                    else {
                        const auto found = upstream->outputs.find(input.source->port);
                        if (found == upstream->outputs.end()) throw ContractError("predecessor output is absent");
                        value = found->second;
                    }
                }
            }
            validateValue(port.type, value);
            inputs.emplace(port.id, std::move(value));
        }
        snapshot->timing.prepareMs = milliseconds(Clock::now() - phaseStarted);
        phase = "initialize"; elapsed = &snapshot->timing.initMs; phaseStarted = Clock::now();
        reader->bind(context);
        const auto& parameters = frozen.initParameters.at(id);
        if (configurationSession != context->session) {
            for (const auto& port : definition->initParams)
                validateValue(port.type, parameters.at(port.id));
            const auto directory = frozen.project.path.parent_path();
            if (toolId != config.toolId || kind != definition->kind || toolVersion != definition->version ||
                servicesDirectory != directory || !sameParameters(initializedWith, parameters)) {
                release();
                initializationFailure.reset();
                initializedWith = parameters;
                servicesDirectory = directory;
                toolVersion = definition->version;
            }
            configurationSession = context->session;
        }
        toolId = config.toolId;
        kind = definition->kind;
        bool suppressed = !instance && initializationFailure && failureSession == context->session;
        if (!instance && !suppressed) {
            initializationFailure.reset();
            ToolStatus status;
            try {
                ToolServices services{PathResolver(frozen.project.path.parent_path()), reader};
                instance = frozen.registry.create(toolId, services);
                if (!instance) throw RegistryError(ToolAvailability::ImplementationUnavailable, "factory returned no instance");
                if (kind == ToolKind::CppClass || kind == ToolKind::PythonClass)
                    status = instance->initialize(parameters);
            } catch (const RegistryError& error) {
                status = {-3, error.what()};
            } catch (const ContractError& error) {
                status = {-2, error.what()};
            } catch (const std::exception& error) {
                status = {-1, error.what()};
            } catch (...) {
                status = {-1, "unknown initialization exception"};
            }
            if (!status) {
                status.message = location(id, toolId, "initialize", status.message);
                release();
                initializationFailure = status;
                failureSession = context->session;
            }
        }
        snapshot->timing.initMs = suppressed ? 0 : milliseconds(Clock::now() - phaseStarted);
        phase = "execute"; elapsed = &snapshot->timing.executeMs; phaseStarted = Clock::now();
        if (instance) result = instance->execute(inputs);
        else {
            result.status = *initializationFailure;
            frameworkFailure = true;
        }
        snapshot->timing.executeMs = instance ? milliseconds(Clock::now() - phaseStarted) : 0;
        snapshot->timing.waitGilMs = result.waitGilMs;
        if (result.status.code < 0) {
            frameworkFailure = true;
            if (instance) result.status.message = location(id, config.toolId, "execute", result.status.message);
        }
        phase = "convert"; elapsed = &snapshot->timing.convertMs; phaseStarted = Clock::now();
        if (!frameworkFailure) {
            validateUtf8(result.status.message);
            const auto businessCount = std::count_if(definition->outputs.begin(), definition->outputs.end(),
                [](const auto& port) { return port.id != "__status_code" && port.id != "__status_message"; });
            if (result.outputs.size() != static_cast<std::size_t>(businessCount))
                throw ContractError("output keys do not exactly match declared business ports");
            for (const auto& port : definition->outputs) {
                if (port.id == "__status_code" || port.id == "__status_message") continue;
                const auto output = result.outputs.find(port.id);
                if (output == result.outputs.end()) throw ContractError("missing output key: " + port.id);
                validateValue(port.type, output->second);
                if (result.status.code == 0 && isMissing(output->second))
                    throw ContractError("success output is Missing: " + port.id);
            }
            for (const auto& output : definition->outputs) {
                if (output.sameGeometryAsInput.empty()) continue;
                for (const auto& input : definition->inputs) {
                    if (input.type.type == Type::RoiBatch && input.imageInput == output.sameGeometryAsInput)
                        snapshot->roiInputs.emplace(input.id, inputs.at(input.id));
                }
            }
        }
        snapshot->timing.convertMs = milliseconds(Clock::now() - phaseStarted);
    } catch (const RegistryError& error) {
        *elapsed = milliseconds(Clock::now() - phaseStarted);
        result.status = {-3, location(id, config.toolId, phase, error.what())};
        frameworkFailure = true;
    } catch (const ContractError& error) {
        *elapsed = milliseconds(Clock::now() - phaseStarted);
        result.status = {-2, location(id, config.toolId, phase, error.what())};
        frameworkFailure = true;
    } catch (const std::exception& error) {
        *elapsed = milliseconds(Clock::now() - phaseStarted);
        result.status = {-1, location(id, config.toolId, phase, error.what())};
        frameworkFailure = true;
    } catch (...) {
        *elapsed = milliseconds(Clock::now() - phaseStarted);
        result.status = {-1, location(id, config.toolId, phase, "unknown exception")};
        frameworkFailure = true;
    }
    if (frameworkFailure) {
        result.outputs.clear();
        snapshot->roiInputs.clear();
        if (definition) for (const auto& port : definition->outputs)
            if (port.id != "__status_code" && port.id != "__status_message")
                result.outputs.emplace(port.id, Missing{});
    }
    snapshot->status = std::move(result.status);
    snapshot->outputs = std::move(result.outputs);
    snapshot->outputs.emplace("__status_code", snapshot->status.code);
    snapshot->outputs.emplace("__status_message", Utf8(snapshot->status.message));
    snapshot->timing.totalMs = milliseconds(Clock::now() - started);
    slot.publish(std::move(snapshot));
}
} // namespace visionrt::runtime_detail
