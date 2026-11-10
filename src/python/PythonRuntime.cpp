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
#include <set>
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

ToolStatus errorStatus(ErrorCode code, const char *summary, const std::string &detail)
{
    return makeStatus(code, summary, detail);
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
                const auto resolution = services.paths.tryResolve(std::get<std::string>(value));
                if (!resolution)
                    throw ContractError(resolution.issue().describe());
                const auto resolved = resolution.value().u8string();
                result[py::int_(id)] = py::str(reinterpret_cast<const char *>(resolved.data()), resolved.size());
                continue;
            }
        }
        result[py::int_(id)] = toPython(value);
    }
    return result;
}

ToolStatus decodeStatus(const py::dict &status, const PythonStorage &storage)
{
    for (const auto *key : {"code", "message", "detail"})
        if (!status.contains(key))
            throw ContractError(std::string("状态缺少字段：") + key);
    if (status.size() != 3)
        throw ContractError("状态只能包含 code/message/detail");
    ToolStatus result;
    result.code    = std::get<std::int64_t>(fromPython({Type::Int}, status["code"], storage, "status.code"));
    result.message = std::get<std::string>(fromPython({Type::String}, status["message"], storage, "status.message"));
    result.detail  = std::get<std::string>(fromPython({Type::String}, status["detail"], storage, "status.detail"));

    return result;
}

py::dict resultDictionary(py::handle result)
{
    if (!py::isinstance<py::dict>(result))
        throw ContractError("工具返回值必须是字典");
    return py::reinterpret_borrow<py::dict>(result);
}

ToolStatus lifecycleStatus(py::handle returned, const PythonStorage &storage)
{
    return decodeStatus(resultDictionary(returned), storage);
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
            status = errorStatus(ErrorCode::Internal, "Python 释放异常", error.what());
        }
        catch (const ContractError &error)
        {
            status = makeStatus(ErrorCode::Internal, error.what());
        }
        catch (const std::exception &error)
        {
            status = errorStatus(ErrorCode::Internal, "Python 释放异常", error.what());
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

    ToolStatus initialize(const Values &parameters) override
    {
        phase_ = NodePhase::Initialize;
        if (!state_ || !state_->alive || !Py_IsInitialized() || generation_ != state_->generation)
            return makeStatus(ErrorCode::PythonUnavailable, "Python 实例跨代或运行时不可用，不能初始化");
        py::gil_scoped_acquire gil;
        if (!objects_ || objects_->isReleasingOrReleased())
            return makeStatus(ErrorCode::FailedPrecondition, "Python 实例已释放，不能初始化");
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
            return errorStatus(ErrorCode::Internal, "Python 初始化异常", error.what());
        }
        catch (const ContractError &error)
        {
            return makeStatus(ErrorCode::InvalidArgument, error.what());
        }
        catch (const std::exception &error)
        {
            return errorStatus(ErrorCode::Internal, "Python 初始化异常", error.what());
        }
    }

    ToolResult execute(const Values &inputs) override
    {
        metrics_ = {};
        phase_   = NodePhase::Execute;
        // The bridge reports a failure by throwing: a node whose bridge could not produce a full
        // output set has no tool answer, so the host turns it into a framework diagnostic.
        if (!state_ || !state_->alive || !Py_IsInitialized() || generation_ != state_->generation)
            throw ContractError("Python 实例跨代或运行时不可用，不能执行");
        const auto             waiting        = std::chrono::steady_clock::now();
        metrics_.pythonBridge                 = true;
        metrics_.pythonInputMeasured          = true;
        py::gil_scoped_acquire gil;
        metrics_.waitGilMs
            = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - waiting).count();
        if (!objects_ || objects_->isReleasingOrReleased())
            throw ContractError("Python 实例已释放，不能执行");
        if (const auto loadStatus = ensureLoaded(); !loadStatus)
            throw ContractError(loadStatus.message);

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

                metrics_.pythonCallMeasured = true;
                ScopeTimer callTimer(metrics_.pythonCallMs);
                phase_   = NodePhase::Execute;
                returned = objects_->execute(parameters);
                callTimer.stop();
            }
        }
        catch (const py::error_already_set &error)
        {
            throw ContractError(std::string(phase_ == NodePhase::Prepare ? "Python 输入转换异常：" : "Python 执行异常：")
                                + error.what());
        }
        catch (const ContractError &error)
        {
            throw ContractError(error.what());
        }
        catch (const std::exception &error)
        {
            throw ContractError(std::string(phase_ == NodePhase::Prepare ? "Python 输入转换异常：" : "Python 执行异常：")
                                + error.what());
        }

        phase_                          = NodePhase::Convert;
        metrics_.pythonOutputMeasured   = true;
        Values      outputs;
        std::string detail;
        {
            ScopeTimer outputTimer(metrics_.pythonOutputMs);
            try
            {
                auto envelope = resultDictionary(returned);
                if (envelope.size() != 2 || !envelope.contains("outputs") || !envelope.contains("detail"))
                    throw ContractError("execute 必须返回 outputs 与 detail");
                if (!py::isinstance<py::dict>(envelope["outputs"]) || !py::isinstance<py::str>(envelope["detail"]))
                    throw ContractError("outputs 必须是字典且 detail 必须是字符串");
                const auto outputsDictionary = py::reinterpret_borrow<py::dict>(envelope["outputs"]);
                detail = envelope["detail"].cast<std::string>();
                // Only what the tool actually returned is converted: the host's completion entry owns
                // the declared defaults, so an omitted business port is not an author error here.
                // Keys are port numbers; a string, a bool or a float key is an author error, never a
                // value to coerce back into an identity.
                std::set<PortId> declared;
                for (const auto &output : definition_.outputs)
                {
                    declared.insert(output.id);
                    const auto key = py::int_(output.id);
                    if (!outputsDictionary.contains(key))
                        continue;
                    PortValue value;
                    try
                    {
                        value = fromPython(output.type, outputsDictionary[key], state_->storage, portLabel(output.id));
                    }
                    catch (const std::exception &error)
                    {
                        throw ContractError(portLabel(output.id) + "：" + error.what());
                    }
                    outputs.emplace(output.id, std::move(value));
                }
                for (auto item : outputsDictionary)
                {
                    const auto key = py::reinterpret_borrow<py::object>(item.first);
                    if (!py::isinstance<py::int_>(key) || py::isinstance<py::bool_>(key))
                        throw ContractError("输出键必须是整数端口编号");
                    const auto id = static_cast<PortId>(key.cast<std::uint64_t>());
                    if (!declared.contains(id))
                        throw ContractError("输出键不在声明中：" + portLabel(id));
                }
                outputTimer.stop();
            }
            catch (const py::error_already_set &error)
            {
                throw ContractError(std::string("Python 输出转换异常：") + error.what());
            }
            catch (const ContractError &error)
            {
                throw ContractError(error.what());
            }
            catch (const std::exception &error)
            {
                throw ContractError(std::string("Python 输出转换异常：") + error.what());
            }
        }
        phase_ = NodePhase::Execute;
        return ToolResult{std::move(outputs), std::move(detail)};
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
            return makeStatus(ErrorCode::FailedPrecondition, "Python 实例已释放");
        if (!state_ || !state_->alive || generation_ != state_->generation)
        {
            ToolStatus err = makeStatus(ErrorCode::PythonUnavailable,
                                        "Python 实例跨代失效或运行时已关闭：" + definition_.nameEn);
            return err;
        }
        const auto it = state_->implementations.find(definition_.nameEn);
        if (it == state_->implementations.end())
        {
            ToolStatus err = makeStatus(ErrorCode::NotFound, "Python 实现未注册：" + definition_.nameEn);
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
                auto err = errorStatus(ErrorCode::PythonUnavailable, "Python 加载异常", error.what());
                impl.loadState->update(err);
                impl.symbol = py::object{};
                if (sharedLoadState_)
                    sharedLoadState_->update(err);
                return err;
            }
            catch (const std::exception &error)
            {
                auto err = errorStatus(ErrorCode::PythonUnavailable, "Python 加载异常", error.what());
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
            auto err = errorStatus(ErrorCode::Internal, "Python 实例创建异常", error.what());
            impl.loadState->update(err);
            if (sharedLoadState_)
                sharedLoadState_->update(err);
            return err;
        }
        catch (const std::exception &error)
        {
            objects_->instance = py::none();
            objects_->execute  = py::none();
            auto err = errorStatus(ErrorCode::Internal, "Python 实例创建异常", error.what());
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

    explicit Impl(const PythonEnvironment &environment)
    {
        PyConfig config;
        PyConfig_InitIsolatedConfig(&config);
        config.parse_argv              = 0;
        config.install_signal_handlers = 0;
        // The selected environment is read-only for this product: no bytecode, no user site and no
        // PYTHON* variable may change what the interpreter loads. The search paths below come from
        // the interpreter's own answer to the probe, so prefix detection never runs again.
        config.write_bytecode      = 0;
        config.user_site_directory = 0;
        config.use_environment     = 0;

        const auto setText = [&config](wchar_t *&target, const std::filesystem::path &value)
        {
            const auto text   = value.wstring();
            const auto status = PyConfig_SetString(&config, &target, text.c_str());
            if (PyStatus_Exception(status))
            {
                const std::string message = status.err_msg ? status.err_msg : "Cannot configure Python path";
                PyConfig_Clear(&config);
                throw std::runtime_error(message);
            }
        };
        setText(config.executable, environment.executable);
        setText(config.base_executable, environment.executable);
        setText(config.program_name, environment.executable);
        setText(config.prefix, environment.prefix);
        setText(config.base_prefix, environment.basePrefix);
        setText(config.exec_prefix, environment.prefix);
        setText(config.base_exec_prefix, environment.basePrefix);

        config.module_search_paths_set = 1;
        for (const auto &entry : environment.searchPaths)
        {
            const auto text   = entry.wstring();
            const auto status = PyWideStringList_Append(&config.module_search_paths, text.c_str());
            if (PyStatus_Exception(status))
            {
                const std::string message = status.err_msg ? status.err_msg : "Cannot configure Python search path";
                PyConfig_Clear(&config);
                throw std::runtime_error(message);
            }
        }

        interpreter = std::make_unique<py::scoped_interpreter>(&config, 0, nullptr, false);
        try
        {
            // Start from the environment that was actually created, not from the one requested: a
            // different executable, prefix or NumPy would mean the user's choice was not honoured.
            verifyEnvironment(environment);
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

    static void verifyEnvironment(const PythonEnvironment &environment)
    {
        const auto text = [](const std::filesystem::path &path)
        {
            const auto utf8 = path.u8string();
            return std::string(reinterpret_cast<const char *>(utf8.data()), utf8.size());
        };
        // CPython canonicalises the paths it reports: which separator it prints and whether it keeps
        // a trailing one are formatting, not identity. Comparing the raw strings rejected a standard
        // venv, whose prefix CPython answered with backslashes while the probe had normalised it.
        const auto same = [](const std::string &reported, const std::filesystem::path &expected)
        {
            return environmentPathEquals(reported, expected);
        };
        const auto mismatch = [&text](const char *label, const std::string &reported,
                                      const std::filesystem::path &expected)
        {
            throw std::runtime_error(std::string("Python ") + label + " 与所选环境不一致：期望 " + text(expected)
                                     + "，实际 " + reported);
        };

        auto sys = py::module_::import("sys");
        const auto executable = sys.attr("executable").cast<std::string>();
        if (!same(executable, environment.executable))
            mismatch("解释器路径", executable, environment.executable);
        const auto prefix = sys.attr("prefix").cast<std::string>();
        if (!same(prefix, environment.prefix))
            mismatch("sys.prefix", prefix, environment.prefix);
        const auto basePrefix = sys.attr("base_prefix").cast<std::string>();
        if (!same(basePrefix, environment.basePrefix))
            mismatch("sys.base_prefix", basePrefix, environment.basePrefix);

        // NumPy is not imported here on purpose. It is a single-phase extension module, so CPython
        // cannot initialise it a second time in one process; importing it at startup would make an
        // interpreter restart impossible. The probe already imports NumPy in the selected
        // environment and records its version, and the first tool that uses it loads it in-process.
    }


    bool                                     preparedShutdown = false;

    void prepareShutdown()
    {
        if (preparedShutdown || !state)
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
        if (state)
        {
            {
                py::gil_scoped_acquire gil;
                state->loader.release();
            }
            state->storage.waitForRelease();
        }
        releaseMainGil.reset();
        state.reset();
        interpreter.reset();
    }
};

PythonRuntime::PythonRuntime(const PythonEnvironment &environment)
    : impl_(std::make_unique<Impl>(environment))
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
                initStatus = makeStatus(ErrorCode::NotFound, "Python 脚本缺失", text(script));
            else if (definition.entry.file.extension() != ".py")
                initStatus = makeStatus(ErrorCode::DataInvalid, "Python 入口必须是 .py 文件", text(script));
            else if (definition.entry.symbol.empty())
                initStatus = makeStatus(ErrorCode::DataInvalid, "Python 入口缺少 symbol", text(script));
        }
        catch (const ContractError &error)
        {
            initStatus = makeStatus(ErrorCode::InvalidArgument, error.what());
        }
        catch (const std::exception &error)
        {
            initStatus = makeStatus(ErrorCode::InvalidArgument, error.what());
        }
        implementation.loadState->set(std::move(initStatus), state.generation);
        const auto [entry, inserted] = state.implementations.emplace(definition.nameEn, std::move(implementation));
        if (!inserted)
            throw ContractError("Python 目录有重复工具 ID：" + definition.nameEn);
        statuses.push_back({definition.nameEn, entry->second.loadState->status()});
    }
    return statuses;
}

namespace {
// A tool instance always works on the normalised contract, so the output key set it demands from
// the Python function matches what the host will validate. The definition is a parameter, not a
// capture, because an expression or condition node adds ports its packaged declaration cannot state.
PythonFactoryFunctor pythonToolFactory(const std::shared_ptr<RuntimeState> &state,
                                       const ToolDefinition &definition,
                                       const std::shared_ptr<PythonLoadState> &loadState)
{
    ToolDefinition normalized = definition;
    normalizeToolDefinition(normalized);
    auto       defPtr     = std::make_shared<const ToolDefinition>(std::move(normalized));
    const auto generation = state->generation;
    auto       createFunc = [weak = std::weak_ptr<RuntimeState>(state), defPtr, loadState, generation](
                            const PlatformToolServices &services) -> std::unique_ptr<IToolInstance>
    {
        auto lockedState = weak.lock();
        if (!lockedState || !lockedState->alive)
            throw RegistryError(ToolAvailability::ImplementationUnavailable,
                                makeStatus(ErrorCode::PythonUnavailable, "Python 运行时已关闭：" + defPtr->nameEn));
        if (lockedState->generation != generation)
            throw RegistryError(ToolAvailability::ImplementationUnavailable,
                                makeStatus(ErrorCode::PythonUnavailable, "Python 运行时已刷新：" + defPtr->nameEn));
        if (loadState)
        {
            auto status = loadState->status();
            if (!status)
                throw RegistryError(ToolAvailability::ImplementationUnavailable, status);
        }
        return std::make_unique<PythonTool>(std::move(lockedState), defPtr, services, loadState);
    };
    return PythonFactoryFunctor{loadState, std::move(createFunc), {}};
}
} // namespace

ToolFactory PythonRuntime::factory(const ToolDefinition &definition) const
{
    py::gil_scoped_acquire gil;
    auto state = impl_->state;
    std::shared_ptr<PythonLoadState> loadState;
    const auto it = state->implementations.find(definition.nameEn);
    if (it != state->implementations.end())
        loadState = it->second.loadState;
    else
        loadState = std::make_shared<PythonLoadState>(
            makeStatus(ErrorCode::NotFound, "Python 实现未注册：" + definition.nameEn));

    auto functor = pythonToolFactory(state, definition, loadState);
    // The registry rebuilds the factory for the exact contract it creates; this factory stays bound
    // to the declaration it was asked for.
    functor.resolve = [state, loadState](const ToolDefinition &requested)
    {
        return ToolFactory(pythonToolFactory(state, requested, loadState));
    };
    return ToolFactory(std::move(functor));
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
