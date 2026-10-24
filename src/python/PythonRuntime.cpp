#include "python/PythonRuntime.h"

#include "PythonLoader.h"

#include <python/PythonInterop.h>

#include "core/ToolRegistry.h"
#include "core/ToolValueValidation.h"
#include "runtime/Snapshot.h"

#include <pybind11/embed.h>
#include <pybind11/stl.h>

#include <algorithm>
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <mutex>
#include <thread>
#include <unordered_set>

namespace visionrt {
namespace py = pybind11;

namespace {

struct Implementation
{
    std::shared_ptr<PythonLoadState> loadState = std::make_shared<PythonLoadState>();
    py::object                       symbol;
};

struct ToolObjects;

struct RuntimeState
{
    py::dict                                           loader;
    std::map<std::string, Implementation, std::less<>> implementations;
    PythonStorage                                      storage;
    std::uint64_t                                      generation = 0;
    std::atomic<std::size_t>                           instances{0};
    bool        alive     = true;

    std::mutex                                         toolsMutex;
    std::vector<std::weak_ptr<ToolObjects>>            tools;

    void registerTool(const std::shared_ptr<ToolObjects> &tool)
    {
        std::lock_guard lock(toolsMutex);
        tools.push_back(tool);
    }

    std::vector<std::shared_ptr<ToolObjects>> activeTools()
    {
        std::lock_guard lock(toolsMutex);
        std::vector<std::shared_ptr<ToolObjects>> result;
        result.reserve(tools.size());
        auto it = tools.begin();
        while (it != tools.end())
        {
            if (auto locked = it->lock())
            {
                result.push_back(std::move(locked));
                ++it;
            }
            else
            {
                it = tools.erase(it);
            }
        }
        return result;
    }
};

bool isBusinessOutput(const PortDefinition &output)
{
    return output.id != "__status_code" && output.id != "__status_message";
}

ToolStatus errorStatus(std::int64_t code, const char *summary, const std::string &detail)
{
    return {code, summary, detail};
}

py::dict inputDictionary(const Values &values, const std::vector<PortDefinition> &declarations,
                         const ToolServices &services)
{
    py::dict result;
    for (const auto &[id, value] : values)
    {
        const auto definition = std::find_if(declarations.begin(), declarations.end(),
                                             [&](const PortDefinition &port) { return port.id == id; });
        if (definition != declarations.end())
        {
            validateValue(definition->type, value);
            if (isPathPort(*definition) && std::holds_alternative<std::string>(value)
                && !std::get<std::string>(value).empty())
            {
                const auto resolved = services.paths.resolve(std::get<std::string>(value)).u8string();
                result[py::str(id)] = py::str(reinterpret_cast<const char *>(resolved.data()), resolved.size());
                continue;
            }
        }
        result[py::str(id)] = toPython(value);
    }
    return result;
}

ToolStatus parseStatus(const py::dict &result, const PythonStorage &storage)
{
    if (!result.contains("__status_code") || !result.contains("__status_message"))
        throw ContractError("工具返回值必须包含 __status_code 与 __status_message");
    ToolStatus status;
    status.code = std::get<std::int64_t>(fromPython({Type::Int}, result["__status_code"], storage, "__status_code"));
    status.message
        = std::get<std::string>(fromPython({Type::String}, result["__status_message"], storage, "__status_message"));
    if (status.code < 0)
        throw ContractError("工具不得返回负状态码，负状态码由宿主框架保留");
    return status;
}

py::dict resultDictionary(py::handle result)
{
    if (!py::isinstance<py::dict>(result))
        throw ContractError("工具返回值必须是字典");
    return py::reinterpret_borrow<py::dict>(result);
}

ToolStatus lifecycleStatus(py::handle returned, const PythonStorage &storage)
{
    auto result = resultDictionary(returned);
    auto status = parseStatus(result, storage);
    if (result.size() != 2)
        throw ContractError("initialize/release 只能返回两个状态键");
    return status;
}

enum class ReleaseState : int
{
    Unreleased = 0,
    Releasing  = 1,
    Released   = 2
};

struct ToolObjects
{
    std::atomic<ReleaseState> state{ReleaseState::Unreleased};
    std::mutex                mutex;
    std::condition_variable   cv;
    std::thread::id           releasingThread{};
    py::object                instance;
    py::object                execute;

    bool isReleased() const noexcept
    {
        return state.load(std::memory_order_acquire) == ReleaseState::Released;
    }

    bool isReleasingOrReleased() const noexcept
    {
        auto s = state.load(std::memory_order_acquire);
        return s == ReleaseState::Releasing || s == ReleaseState::Released;
    }

    void markReleased() noexcept
    {
        std::lock_guard lock(mutex);
        state.store(ReleaseState::Released, std::memory_order_release);
        cv.notify_all();
    }

    ToolStatus releaseWithGil(const PythonStorage &storage) noexcept
    {
        std::unique_lock lock(mutex);
        if (state.load(std::memory_order_acquire) == ReleaseState::Released)
            return {};

        if (state.load(std::memory_order_acquire) == ReleaseState::Releasing)
        {
            if (releasingThread == std::this_thread::get_id())
                return {};
            {
                py::gil_scoped_release releaseGil;
                cv.wait(lock, [this] {
                    return state.load(std::memory_order_acquire) == ReleaseState::Released;
                });
                lock.unlock();
            }
            return {};
        }

        state.store(ReleaseState::Releasing, std::memory_order_release);
        releasingThread = std::this_thread::get_id();
        lock.unlock();

        ToolStatus status{};
        try
        {
            if (instance)
            {
                status = lifecycleStatus(instance.attr("release")(), storage);
            }
        }
        catch (const py::error_already_set &error)
        {
            status = errorStatus(-4, "Python 释放异常", error.what());
        }
        catch (const ContractError &error)
        {
            status = {-4, error.what()};
        }
        catch (const std::exception &error)
        {
            status = errorStatus(-4, "Python 释放异常", error.what());
        }

        instance = py::none();
        execute  = py::none();

        {
            std::lock_guard finishLock(mutex);
            state.store(ReleaseState::Released, std::memory_order_release);
        }
        cv.notify_all();
        return status;
    }
};

class PythonTool final
    : public IToolInstance
    , public IPythonExecutionMetrics
    , public IToolFailureContext
{
public:
    PythonTool(std::shared_ptr<RuntimeState> state, std::shared_ptr<const ToolDefinition> definition,
               ToolServices services, std::shared_ptr<PythonLoadState> sharedLoadState)
        : state_(std::move(state))
        , definitionOwner_(std::move(definition))
        , definition_(*definitionOwner_)
        , services_(std::move(services))
        , sharedLoadState_(std::move(sharedLoadState))
        , generation_(state_ ? state_->generation : 0)
        , objects_(std::make_shared<ToolObjects>())
    {
        if (state_)
        {
            state_->registerTool(objects_);
            ++state_->instances;
        }
    }

    ~PythonTool() override
    {
        if (state_)
        {
            if (loaded_ && objects_)
            {
                if (state_->alive && Py_IsInitialized())
                {
                    py::gil_scoped_acquire gil;
                    objects_->releaseWithGil(state_->storage);
                }
            }
            --state_->instances;
        }
    }

    ExecutionMetrics pythonMetrics() const override
    {
        return metrics_;
    }

    NodePhase failurePhase() const noexcept override
    {
        return phase_;
    }

    ToolStatus initialize(const Values &parameters) override
    {
        phase_ = NodePhase::Initialize;
        if (!state_ || !state_->alive || !Py_IsInitialized() || generation_ != state_->generation)
            return {-1, "Python 实例跨代或运行时不可用，不能初始化"};
        py::gil_scoped_acquire gil;
        if (!objects_ || objects_->isReleasingOrReleased())
            return {-1, "Python 实例已释放，不能初始化"};
        auto loadStatus = ensureLoaded();
        if (!loadStatus)
            return loadStatus;
        if (definition_.kind == ToolKind::PythonFunction)
            return {};
        try
        {
            auto inputs = inputDictionary(parameters, definition_.initParams, services_);
            return lifecycleStatus(objects_->instance.attr("initialize")(inputs), state_->storage);
        }
        catch (const py::error_already_set &error)
        {
            return errorStatus(-1, "Python 初始化异常", error.what());
        }
        catch (const ContractError &error)
        {
            return {-2, error.what()};
        }
        catch (const std::exception &error)
        {
            return errorStatus(-1, "Python 初始化异常", error.what());
        }
    }

    ToolResult execute(const Values &inputs) override
    {
        metrics_                       = {};
        phase_                         = NodePhase::Execute;
        const auto fail = [&](ToolStatus status)
        {
            ToolResult res;
            res.status = std::move(status);
            for (const auto &output : definition_.outputs)
                if (isBusinessOutput(output))
                    res.outputs.emplace(output.id, Missing{});
            return res;
        };
        if (!state_ || !state_->alive || !Py_IsInitialized() || generation_ != state_->generation)
            return fail({-1, "Python 实例跨代或运行时不可用，不能执行"});
        const auto             waiting = std::chrono::steady_clock::now();
        py::gil_scoped_acquire gil;
        metrics_.waitGilMs
            = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - waiting).count();
        if (!objects_ || objects_->isReleasingOrReleased())
            return fail({-1, "Python 实例已释放，不能执行"});
        auto loadStatus = ensureLoaded();
        if (!loadStatus)
            return fail(loadStatus);

        ToolResult result;

        struct ScopeTimer
        {
            using Clock             = std::chrono::steady_clock;
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
                    target = std::chrono::duration<double, std::milli>(Clock::now() - start).count();
            }

            void stop()
            {
                if (active)
                {
                    target = std::chrono::duration<double, std::milli>(Clock::now() - start).count();
                    active = false;
                }
            }
        };

        py::object returned;
        phase_ = NodePhase::Prepare;
        try
        {
            {
                ScopeTimer inputTimer(metrics_.pythonInputMs);
                auto       parameters = inputDictionary(inputs, definition_.inputs, services_);
                inputTimer.stop();

                ScopeTimer callTimer(metrics_.pythonCallMs);
                phase_   = NodePhase::Execute;
                returned = objects_->execute(parameters);
                callTimer.stop();
            }
        }
        catch (const py::error_already_set &error)
        {
            return fail(errorStatus(-1, phase_ == NodePhase::Prepare ? "Python 输入转换异常" : "Python 执行异常",
                                    error.what()));
        }
        catch (const ContractError &error)
        {
            return fail({-2, error.what()});
        }
        catch (const std::exception &error)
        {
            return fail(errorStatus(-1, phase_ == NodePhase::Prepare ? "Python 输入转换异常" : "Python 执行异常",
                                    error.what()));
        }
        phase_ = NodePhase::Convert;
        try
        {
            ScopeTimer outputTimer(metrics_.pythonOutputMs);
            auto       dictionary = resultDictionary(returned);
            result.status         = parseStatus(dictionary, state_->storage);
            if (dictionary.size() != businessOutputCount_ + 2)
                throw ContractError("输出键必须与声明端口及两个状态键完全一致");
            for (const auto &output : definition_.outputs)
            {
                if (!isBusinessOutput(output))
                    continue;
                if (!dictionary.contains(py::str(output.id)))
                    throw ContractError("缺少声明的输出键：" + output.id);
                PortValue value;
                try
                {
                    value = fromPython(output.type, dictionary[py::str(output.id)], state_->storage, output.id);
                }
                catch (const std::exception &error)
                {
                    throw ContractError("输出端口 " + output.id + "：" + error.what());
                }
                validateToolOutput(output, value, result.status);
                result.outputs.emplace(output.id, std::move(value));
            }
            outputTimer.stop();
        }
        catch (const py::error_already_set &error)
        {
            return fail(errorStatus(-2, "Python 输出转换异常", error.what()));
        }
        catch (const ContractError &error)
        {
            return fail({-2, error.what()});
        }
        catch (const std::exception &error)
        {
            return fail(errorStatus(-2, "Python 输出转换异常", error.what()));
        }
        phase_ = NodePhase::Execute;
        return result;
    }

    ToolStatus releaseWithGil() noexcept
    {
        if (!objects_)
            return {};
        return objects_->releaseWithGil(state_->storage);
    }

    ToolStatus release() override
    {
        if (!objects_)
            return {};
        if (objects_->isReleased())
            return {};
        if (!Py_IsInitialized())
        {
            objects_->markReleased();
            return {};
        }
        py::gil_scoped_acquire gil;
        return objects_->releaseWithGil(state_->storage);
    }

private:
    ToolStatus ensureLoaded()
    {
        if (loaded_)
            return {};
        if (!objects_ || objects_->isReleasingOrReleased())
            return {-1, "Python 实例已释放"};
        if (!state_ || !state_->alive || generation_ != state_->generation)
        {
            ToolStatus err{-3, "Python 实例跨代失效或运行时已关闭：" + definition_.id};
            return err;
        }
        const auto it = state_->implementations.find(definition_.id);
        if (it == state_->implementations.end())
        {
            ToolStatus err{-3, "Python 实现未注册：" + definition_.id};
            if (sharedLoadState_)
                sharedLoadState_->update(err);
            return err;
        }
        auto &impl = it->second;
        auto snap = impl.loadState->snapshot();
        if (!snap.status)
            return snap.status;

        if (!impl.symbol)
        {
            try
            {
                const auto directory = definition_.pythonPackage().u8string();
                const auto script    = definition_.entry.file.u8string();
                impl.symbol          = state_->loader["finder"].attr("load")(
                    definition_.source.origin == ToolOrigin::Builtin ? "builtin" : "custom",
                    py::str(reinterpret_cast<const char *>(directory.data()), directory.size()),
                    py::str(reinterpret_cast<const char *>(script.data()), script.size()),
                    definition_.entry.symbol,
                    generation_);
                if (!PyCallable_Check(impl.symbol.ptr()))
                    throw ContractError("Python 入口不可调用");
                if (definition_.kind == ToolKind::PythonClass)
                {
                    if (!PyType_Check(impl.symbol.ptr()))
                        throw ContractError("Python 类入口不是类");
                    for (const char *method : {"initialize", "execute", "release"})
                        if (!py::hasattr(impl.symbol, method) || !PyCallable_Check(impl.symbol.attr(method).ptr()))
                            throw ContractError(std::string("Python 类缺少可调用方法：") + method);
                }
                else if (PyType_Check(impl.symbol.ptr()))
                    throw ContractError("Python 函数入口不能是类");
            }
            catch (const py::error_already_set &error)
            {
                auto err = errorStatus(-3, "Python 加载异常", error.what());
                impl.loadState->update(err);
                impl.symbol = py::object{};
                if (sharedLoadState_)
                    sharedLoadState_->update(err);
                return err;
            }
            catch (const std::exception &error)
            {
                auto err = errorStatus(-3, "Python 加载异常", error.what());
                impl.loadState->update(err);
                impl.symbol = py::object{};
                if (sharedLoadState_)
                    sharedLoadState_->update(err);
                return err;
            }
        }

        try
        {
            if (definition_.kind == ToolKind::PythonClass)
            {
                objects_->instance = impl.symbol();
                objects_->execute  = objects_->instance.attr("execute");
            }
            else
            {
                objects_->execute = impl.symbol;
            }
        }
        catch (const py::error_already_set &error)
        {
            objects_->instance = py::none();
            objects_->execute  = py::none();
            auto err = errorStatus(-1, "Python 实例创建异常", error.what());
            impl.loadState->update(err);
            if (sharedLoadState_)
                sharedLoadState_->update(err);
            return err;
        }
        catch (const std::exception &error)
        {
            objects_->instance = py::none();
            objects_->execute  = py::none();
            auto err = errorStatus(-1, "Python 实例创建异常", error.what());
            impl.loadState->update(err);
            if (sharedLoadState_)
                sharedLoadState_->update(err);
            return err;
        }

        loaded_ = true;
        return {};
    }



    std::shared_ptr<RuntimeState>         state_;
    std::shared_ptr<const ToolDefinition> definitionOwner_;
    const ToolDefinition                 &definition_;
    const std::size_t                     businessOutputCount_ = static_cast<std::size_t>(
        std::count_if(definition_.outputs.begin(), definition_.outputs.end(), isBusinessOutput));
    ToolServices                 services_;
    std::shared_ptr<PythonLoadState> sharedLoadState_;
    std::shared_ptr<ToolObjects> objects_;
    ExecutionMetrics             metrics_;
    NodePhase                    phase_      = NodePhase::Execute;
    bool                         loaded_     = false;
    std::uint64_t                generation_ = 0;
};
} // namespace

struct PythonRuntime::Impl
{
    std::thread::id                         ownerThread = std::this_thread::get_id();
    std::unique_ptr<py::scoped_interpreter> interpreter;
    std::shared_ptr<RuntimeState>           state;
    std::unique_ptr<py::gil_scoped_release> releaseMainGil;

    explicit Impl(const std::filesystem::path &pythonHome)
    {
        PyConfig config;
        PyConfig_InitIsolatedConfig(&config);
        config.parse_argv                  = 0;
        config.install_signal_handlers     = 0;
        std::filesystem::path resolvedHome = pythonHome;
        if (resolvedHome.empty())
        {
            if (const char *env = std::getenv("VISIONRT_PYTHON_HOME"); env && *env)
            {
                resolvedHome = env;
            }
            else if (const char *envPy = std::getenv("PYTHONHOME"); envPy && *envPy)
            {
                resolvedHome = envPy;
            }
#ifdef VISIONRT_PYTHON_DEFAULT_HOME
            else
            {
                resolvedHome = VISIONRT_PYTHON_DEFAULT_HOME;
            }
#endif
        }
        if (!resolvedHome.empty())
        {
            const auto home   = resolvedHome.wstring();
            const auto status = PyConfig_SetString(&config, &config.home, home.c_str());
            if (PyStatus_Exception(status))
            {
                const std::string message = status.err_msg ? status.err_msg : "Cannot configure Python home";
                PyConfig_Clear(&config);
                throw std::runtime_error(message);
            }
        }
        interpreter = std::make_unique<py::scoped_interpreter>(&config, 0, nullptr, false);
        try
        {
            state = std::make_shared<RuntimeState>();
            static std::atomic<std::uint64_t> nextRuntime{0};
            py::exec(python::loaderSource, state->loader);
            state->loader["finder"] = state->loader["ManagedPackages"](nextRuntime.fetch_add(1));
            py::module_::import("sys").attr("meta_path").attr("insert")(0, state->loader["finder"]);
            releaseMainGil = std::make_unique<py::gil_scoped_release>();
        }
        catch (const py::error_already_set &error)
        {
            const std::string message = error.what();
            state.reset();
            throw std::runtime_error(message);
        }
        catch (...)
        {
            state.reset();
            throw;
        }
    }

    bool                                     preparedShutdown = false;

    void prepareShutdown()
    {
        if (preparedShutdown)
            return;
        preparedShutdown = true;
        py::gil_scoped_acquire gil;
        state->alive = false;
        for (auto &tool : state->activeTools())
        {
            tool->releaseWithGil(state->storage);
        }
        state->implementations.clear();
        try
        {
            if (state->loader && state->loader.contains("finder"))
            {
                state->loader["finder"].attr("clear")();
                py::module_::import("sys").attr("meta_path").attr("remove")(state->loader["finder"]);
            }
            state->loader.clear();
            py::module_::import("gc").attr("collect")();
        }
        catch (py::error_already_set &error)
        {
            error.discard_as_unraisable("PythonRuntime prepareShutdown");
        }
    }

    ~Impl()
    {
        if (std::this_thread::get_id() != ownerThread)
            std::terminate();
        prepareShutdown();
        {
            py::gil_scoped_acquire gil;
            state->loader.release();
        }
        state->storage.waitForRelease();
        releaseMainGil.reset();
        state.reset();
        interpreter.reset();
    }
};

PythonRuntime::PythonRuntime(const std::filesystem::path &pythonHome)
    : impl_(std::make_unique<Impl>(pythonHome))
{
}

PythonRuntime::~PythonRuntime() = default;

std::vector<PythonImplementation> PythonRuntime::refresh(std::span<const ToolDefinition> definitions)
{
    py::gil_scoped_acquire gil;
    auto                  &state = *impl_->state;
    if (state.instances != 0)
        throw std::logic_error("Python refresh requires all Python instances to be destroyed");
    state.implementations.clear();
    state.loader["finder"].attr("clear")();
    ++state.generation;
    std::vector<PythonImplementation> statuses;
    const auto                        text = [](const std::filesystem::path &path)
    {
        const auto utf8 = path.u8string();
        return std::string(reinterpret_cast<const char *>(utf8.data()), utf8.size());
    };
    for (const auto &definition : definitions)
    {
        if (definition.kind != ToolKind::PythonFunction && definition.kind != ToolKind::PythonClass)
            continue;
        Implementation implementation;
        ToolStatus initStatus{};
        try
        {
            const auto script = definition.pythonScript();
            if (!std::filesystem::is_regular_file(script))
                initStatus = {-3, "Python 脚本缺失", text(script)};
            else if (definition.entry.file.extension() != ".py")
                initStatus = {-3, "Python 入口必须是 .py 文件", text(script)};
            else if (definition.entry.symbol.empty())
                initStatus = {-3, "Python 入口缺少 symbol", text(script)};
        }
        catch (const ContractError &error)
        {
            initStatus = {-2, error.what()};
        }
        catch (const std::exception &error)
        {
            initStatus = {-2, error.what()};
        }
        implementation.loadState->set(std::move(initStatus), state.generation);
        const auto [entry, inserted] = state.implementations.emplace(definition.id, std::move(implementation));
        if (!inserted)
            throw ContractError("Python 目录有重复工具 ID：" + definition.id);
        statuses.push_back({definition.id, entry->second.loadState->status()});
    }
    return statuses;
}

ToolFactory PythonRuntime::factory(const ToolDefinition &definition) const
{
    py::gil_scoped_acquire gil;
    auto state = impl_->state;
    std::shared_ptr<PythonLoadState> loadState;
    const auto it = state->implementations.find(definition.id);
    if (it != state->implementations.end())
        loadState = it->second.loadState;
    else
        loadState = std::make_shared<PythonLoadState>(ToolStatus{-3, "Python 实现未注册：" + definition.id});

    auto defPtr = std::make_shared<const ToolDefinition>(definition);
    auto createFunc = [weak = std::weak_ptr<RuntimeState>(state), defPtr, loadState,
                       generation = state->generation](
                          const PlatformToolServices &services) -> std::unique_ptr<IToolInstance>
    {
        auto lockedState = weak.lock();
        if (!lockedState || !lockedState->alive)
            throw RegistryError(ToolAvailability::ImplementationUnavailable, "Python 运行时已关闭：" + defPtr->id);
        if (lockedState->generation != generation)
            throw RegistryError(ToolAvailability::ImplementationUnavailable, "Python 运行时已刷新：" + defPtr->id);
        if (loadState)
        {
            auto status = loadState->status();
            if (!status)
                throw RegistryError(ToolAvailability::ImplementationUnavailable, status);
        }
        return std::make_unique<PythonTool>(std::move(lockedState), defPtr, services, loadState);
    };

    return ToolFactory(PythonFactoryFunctor{std::move(loadState), std::move(createFunc)});
}

bool PythonRuntime::isCloseReady() const noexcept
{
    if (!impl_)
        return true;
    return impl_->state->storage.isReadyForClose();
}

void PythonRuntime::prepareShutdown()
{
    if (impl_)
        impl_->prepareShutdown();
}

std::size_t PythonRuntime::activeReferences() const noexcept
{
    return impl_ ? impl_->state->storage.activeCount() : 0;
}

} // namespace visionrt
