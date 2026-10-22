#include "python/PythonRuntime.h"

#include <python/PythonInterop.h>

#include "core/ToolRegistry.h"
#include "core/ToolValueValidation.h"
#include "runtime/Snapshot.h"

#include <pybind11/embed.h>
#include <pybind11/stl.h>

#include <algorithm>
#include <atomic>
#include <chrono>
#include <thread>

namespace visionrt {
namespace py = pybind11;

namespace {

// This finder owns only explicitly registered namespaces. Both entry modules
// and every relative import compile the current source, never timestamp pyc.
constexpr const char *loaderSource = R"PY(
import importlib.abc
import importlib.machinery
import importlib.util
import importlib
import pathlib
import sys

class FreshSourceLoader(importlib.machinery.SourceFileLoader):
    def get_code(self, fullname):
        return self.source_to_code(self.get_data(self.path), self.path)

class ManagedFinder(importlib.abc.MetaPathFinder):
    def __init__(self):
        self.roots = {}
    def find_spec(self, fullname, path=None, target=None):
        root, _, suffix = fullname.partition('.')
        directory = self.roots.get(root)
        if directory is None:
            return None
        location = directory.joinpath(*suffix.split('.')) if suffix else directory
        package = location / '__init__.py'
        if package.is_file():
            return importlib.util.spec_from_file_location(fullname, str(package),
                loader=FreshSourceLoader(fullname, str(package)),
                submodule_search_locations=[str(location)])
        source = location.with_suffix('.py')
        if suffix and source.is_file():
            return importlib.util.spec_from_file_location(fullname, str(source),
                loader=FreshSourceLoader(fullname, str(source)))
        if location.is_dir():
            spec = importlib.machinery.ModuleSpec(fullname, loader=None, is_package=True)
            spec.submodule_search_locations = [str(location)]
            return spec
        # Do not allow PathFinder to recover an old pyc or native module from
        # a managed namespace after a source file has disappeared.
        raise ModuleNotFoundError('Managed Python source is missing: ' + fullname)
    def clear(self):
        roots = tuple(self.roots)
        for name in tuple(sys.modules):
            if any(name == root or name.startswith(root + '.') for root in roots):
                del sys.modules[name]
        self.roots.clear()
    def load(self, root, directory, script, symbol):
        directory = pathlib.Path(directory).resolve()
        entry = (directory / script).resolve()
        relative = entry.relative_to(directory)
        if entry.suffix != '.py':
            raise ImportError('Managed entry must be a Python source file')
        parts = list(relative.with_suffix('').parts)
        if parts[-1] == '__init__':
            parts.pop()
        if any(not part.isidentifier() for part in parts):
            raise ImportError('Managed package path components must be identifiers')
        self.roots[root] = directory
        fullname = '.'.join([root] + parts)
        module = importlib.import_module(fullname)
        return getattr(module, symbol)

finder = ManagedFinder()
sys.meta_path.insert(0, finder)
)PY";

struct Implementation
{
    ToolStatus status;
    py::object symbol;
};

struct RuntimeState
{
    py::dict                                           loader;
    std::map<std::string, Implementation, std::less<>> implementations;
    std::size_t instances = 0; // Accessed only while holding the interpreter GIL.
    bool        alive     = true;
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
            if (isPathPort(*definition)
                && std::holds_alternative<Utf8>(value) && !std::get<Utf8>(value).str().empty())
            {
                const auto resolved = services.paths.resolve(std::get<Utf8>(value).str()).u8string();
                result[py::str(id)] = py::str(reinterpret_cast<const char *>(resolved.data()), resolved.size());
                continue;
            }
        }
        result[py::str(id)] = toPythonReadonly(value);
    }
    return result;
}

ToolStatus parseStatus(const py::dict &result)
{
    if (!result.contains("__status_code") || !result.contains("__status_message"))
        throw ContractError("工具返回值必须包含 __status_code 与 __status_message");
    ToolStatus status;
    status.code    = std::get<std::int64_t>(fromPython({Type::Int}, result["__status_code"]));
    status.message = std::get<Utf8>(fromPython({Type::String}, result["__status_message"])).str();
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

ToolStatus lifecycleStatus(py::handle returned)
{
    auto result = resultDictionary(returned);
    auto status = parseStatus(result);
    if (result.size() != 2)
        throw ContractError("initialize/release 只能返回两个状态键");
    return status;
}

class PythonTool final
    : public IToolInstance
    , public IPythonExecutionMetrics
    , public IToolFailureContext
{
public:
    PythonTool(std::shared_ptr<RuntimeState> state, std::shared_ptr<const ToolDefinition> definition,
               ToolServices services, const Implementation &implementation)
        : state_(std::move(state))
        , definitionOwner_(std::move(definition))
        , definition_(*definitionOwner_)
        , services_(std::move(services))
    {
        // Factory holds the GIL, including member construction and failure cleanup.
        objects_ = std::make_unique<Objects>();
        ToolStatus status;
        try
        {
            if (definition_.kind == ToolKind::PythonClass)
            {
                objects_->instance = implementation.symbol();
                objects_->execute  = objects_->instance.attr("execute");
            }
            else
                objects_->execute = implementation.symbol;
        }
        catch (const py::error_already_set &error)
        {
            status = {-1, "Python 实例创建异常", error.what()};
        }
        catch (const std::exception &error)
        {
            status = {-1, "Python 实例创建异常", error.what()};
        }
        if (!status)
        {
            releaseWithGil();
            throw RegistryError(ToolAvailability::ImplementationUnavailable, std::move(status));
        }
        ++state_->instances;
    }

    ~PythonTool() override
    {
        py::gil_scoped_acquire gil;
        if (!released_)
            releaseWithGil();
        objects_.reset(); // No py::object member survives the GIL guard.
        --state_->instances;
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
        py::gil_scoped_acquire gil;
        if (released_)
            return {-1, "Python 实例已释放，不能初始化"};
        if (definition_.kind == ToolKind::PythonFunction)
            return {};
        try
        {
            auto inputs = inputDictionary(parameters, definition_.initParams, services_);
            return lifecycleStatus(objects_->instance.attr("initialize")(inputs));
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
        const auto             waiting = std::chrono::steady_clock::now();
        py::gil_scoped_acquire gil;
        metrics_.waitGilMs
            = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - waiting).count();
        ToolResult result;
        const auto fail = [&](ToolStatus status)
        {
            result.status = std::move(status);
            result.outputs.clear();
            for (const auto &output : definition_.outputs)
                if (isBusinessOutput(output))
                    result.outputs.emplace(output.id, Missing{});
            return std::move(result);
        };
        if (released_)
            return fail({-1, "Python 实例已释放，不能执行"});

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
            result.status         = parseStatus(dictionary);
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
                    value = fromPython(output.type, dictionary[py::str(output.id)]);
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

    ToolStatus release() override
    {
        py::gil_scoped_acquire gil;
        return releaseWithGil();
    }

private:
    struct Objects
    {
        py::object instance;
        py::object execute;
    };

    ToolStatus releaseWithGil() noexcept
    {
        if (released_)
            return {};
        released_ = true;
        try
        {
            if (objects_->instance)
            {
                return lifecycleStatus(objects_->instance.attr("release")());
            }
        }
        catch (const py::error_already_set &error)
        {
            return errorStatus(-4, "Python 释放异常", error.what());
        }
        catch (const ContractError &error)
        {
            return {-4, error.what()};
        }
        catch (const std::exception &error)
        {
            return errorStatus(-4, "Python 释放异常", error.what());
        }
        return {};
    }

    std::shared_ptr<RuntimeState>         state_;
    std::shared_ptr<const ToolDefinition> definitionOwner_;
    const ToolDefinition                 &definition_;
    const std::size_t                     businessOutputCount_ = static_cast<std::size_t>(
        std::count_if(definition_.outputs.begin(), definition_.outputs.end(), isBusinessOutput));
    ToolServices             services_;
    std::unique_ptr<Objects> objects_;
    ExecutionMetrics         metrics_;
    NodePhase                phase_    = NodePhase::Execute;
    bool                     released_ = false;
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
            py::exec(loaderSource, state->loader);
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

    ~Impl()
    {
        // Violating this order cannot be repaired after interpreter finalization.
        if (std::this_thread::get_id() != ownerThread)
            std::terminate();
        releaseMainGil.reset();
        if (state->instances != 0)
            std::terminate();
        state->alive = false;
        state->implementations.clear();
        try
        {
            state->loader["finder"].attr("clear")();
            py::module_::import("sys").attr("meta_path").attr("remove")(state->loader["finder"]);
        }
        catch (py::error_already_set &error)
        {
            error.discard_as_unraisable("PythonRuntime shutdown");
        }
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
    std::vector<PythonImplementation> statuses;
    static std::atomic<std::uint64_t> nextNamespace{0};
    for (const auto &definition : definitions)
    {
        if (definition.kind != ToolKind::PythonFunction && definition.kind != ToolKind::PythonClass)
            continue;
        Implementation implementation;
        try
        {
            const auto root       = "_visionrt_tool_" + std::to_string(nextNamespace.fetch_add(1));
            const auto directory  = std::filesystem::absolute(definition.definitionPath).parent_path().u8string();
            const auto script     = std::filesystem::absolute(definition.script).u8string();
            implementation.symbol = state.loader["finder"].attr("load")(
                root, py::str(reinterpret_cast<const char *>(directory.data()), directory.size()),
                py::str(reinterpret_cast<const char *>(script.data()), script.size()), definition.symbol);
            if (!PyCallable_Check(implementation.symbol.ptr()))
                throw ContractError("Python 入口不可调用");
            if (definition.kind == ToolKind::PythonClass)
            {
                if (!PyType_Check(implementation.symbol.ptr()))
                    throw ContractError("Python 类入口不是类");
                for (const char *method : {"initialize", "execute", "release"})
                    if (!py::hasattr(implementation.symbol, method)
                        || !PyCallable_Check(implementation.symbol.attr(method).ptr()))
                        throw ContractError(std::string("Python 类缺少可调用方法：") + method);
            }
            else if (PyType_Check(implementation.symbol.ptr()))
            {
                throw ContractError("Python 函数入口不能是类");
            }
        }
        catch (const py::error_already_set &error)
        {
            implementation.symbol = py::object{};
            implementation.status = errorStatus(-3, "Python 加载异常", error.what());
        }
        catch (const ContractError &error)
        {
            implementation.status = {-3, error.what()};
            implementation.symbol = py::object{};
        }
        catch (const std::exception &error)
        {
            implementation.symbol = py::object{};
            implementation.status = errorStatus(-3, "Python 加载异常", error.what());
        }
        statuses.push_back({definition.id, implementation.status});
        state.implementations.insert_or_assign(definition.id, std::move(implementation));
    }
    return statuses;
}

ToolFactory PythonRuntime::factory(const ToolDefinition &definition) const
{
    return [weak       = std::weak_ptr<RuntimeState>(impl_->state),
            definition = std::make_shared<const ToolDefinition>(definition)](
               const ToolServices &services) -> std::unique_ptr<IToolInstance>
    {
        auto state = weak.lock();
        if (!state)
            throw RegistryError(ToolAvailability::ImplementationUnavailable, "Python 运行时已关闭：" + definition->id);
        py::gil_scoped_acquire gil;
        const auto             implementation = state->implementations.find(definition->id);
        if (!state->alive || implementation == state->implementations.end())
            throw RegistryError(ToolAvailability::ImplementationUnavailable, "Python 实现未加载：" + definition->id);
        if (!implementation->second.status)
            throw RegistryError(ToolAvailability::ImplementationUnavailable, implementation->second.status);
        return std::make_unique<PythonTool>(std::move(state), definition, services, implementation->second);
    };
}

} // namespace visionrt
