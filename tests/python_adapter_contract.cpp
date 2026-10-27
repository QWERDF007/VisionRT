#include "core/DefinitionJson.h"
#include "result_view.h"
#include "core/ToolRegistry.h"
#include "python/PythonRuntime.h"
#include <pybind11/embed.h>

#include "runtime/Snapshot.h"

#include <QTemporaryDir>
#include <cassert>
#include <filesystem>
#include <fstream>
#include <future>
#include <iostream>

namespace py = pybind11;
using namespace visionrt;

namespace {
template<class Operation>
void rejects(Operation &&operation)
{
    bool failed = false;
    try
    {
        operation();
    }
    catch (const std::exception &)
    {
        failed = true;
    }
    assert(failed);
}

struct Sources
{
    QTemporaryDir         temporary;
    std::filesystem::path root         = std::filesystem::path(temporary.path().toStdWString());
    std::filesystem::path directory    = root / "python/custom/math";
    std::filesystem::path declarations = root / "tools/custom";

    ToolSource source() const
    {
        return {
            ToolOrigin::Custom,
            {declarations, root / "python/custom"}
        };
    }

    static void write(const std::filesystem::path &path, const std::string &content)
    {
        std::filesystem::create_directories(path.parent_path());
        std::ofstream stream(path, std::ios::binary | std::ios::trunc);
        stream.exceptions(std::ios::badbit | std::ios::failbit);
        stream << content;
    }

    void put(const std::string &name, const std::string &content)
    {
        write(directory / name, content);
    }

    ToolDefinition definition(const std::string &id, ToolKind kind, const std::string &symbol,
                              bool secondOutput = false)
    {
        ToolDefinition tool;
        tool.id             = id;
        tool.displayName    = "Contract tool";
        tool.kind           = kind;
        tool.definitionPath = declarations / (id + ".json");
        tool.source         = source();
        tool.entry          = {"math", "entry.py", symbol};
        tool.outputs.push_back({"value", "Value", {Type::Int}});
        if (secondOutput)
            tool.outputs.push_back({"other", "Other", {Type::Float}});
        write(tool.definitionPath, emitNormalizedToolJson(tool));
        return parseToolDefinitionFile(tool.definitionPath, source());
    }
};

void adaptersAndRefresh(PythonRuntime &runtime)
{
    Sources sources;
    sources.put("helper.py", "value = 11\nobsolete = True\n");
    sources.put("entry.py", R"(
from . import helper
import numpy
def execute(inputs):
    return {'__status_code': 0, '__status_message': '', 'value': helper.value}
class Counter:
    def initialize(self, inputs):
        self.count = 0
        return {'__status_code': 0, '__status_message': ''}
    def execute(self, inputs):
        self.count += 1
        return {'__status_code': 0, '__status_message': '', 'value': self.count}
    def release(self):
        return {'__status_code': 0, '__status_message': ''}
class Broken:
    def initialize(self, inputs):
        raise RuntimeError('initialization detail')
    def execute(self, inputs):
        raise RuntimeError('execution detail')
    def release(self):
        raise RuntimeError('release detail')
class BadConstructor(Counter):
    def __init__(self):
        raise RuntimeError('constructor detail')
def missing(inputs):
    return {'__status_code': 0, '__status_message': ''}
def extra(inputs):
    return {'__status_code': 0, '__status_message': '', 'value': 1, 'typo': 2}
def wrong(inputs):
    return {'__status_code': 0, '__status_message': '', 'value': True}
def business(inputs):
    return {'__status_code': 5, '__status_message': 'expected failure', 'value': 7}
def partial(inputs):
    return {'__status_code': 0, '__status_message': '', 'value': 9, 'other': True}
def absent(inputs):
    return {'__status_code': 0, '__status_message': '', 'value': None}
)");
    auto function    = sources.definition("python.function", ToolKind::PythonFunction, "execute");
    auto counter     = sources.definition("python.counter", ToolKind::PythonClass, "Counter");
    auto broken      = sources.definition("python.broken", ToolKind::PythonClass, "Broken");
    auto constructor = sources.definition("python.constructor", ToolKind::PythonClass, "BadConstructor");
    auto missing     = sources.definition("python.missing", ToolKind::PythonFunction, "missing");
    auto extra       = sources.definition("python.extra", ToolKind::PythonFunction, "extra");
    auto wrong       = sources.definition("python.wrong", ToolKind::PythonFunction, "wrong");
    auto business    = sources.definition("python.business", ToolKind::PythonFunction, "business");
    auto partial     = sources.definition("python.partial", ToolKind::PythonFunction, "partial", true);
    auto optional    = sources.definition("python.optional", ToolKind::PythonFunction, "absent");
    for (auto &port : optional.outputs)
        if (port.id == "value")
            port.required = false;
    sources.put("python.optional.json", emitNormalizedToolJson(optional));
    std::vector<ToolDefinition> definitions{function, counter, broken,   constructor, missing,
                                            extra,    wrong,   business, partial,     optional};
    std::uintptr_t              numpyIdentity;
    {
        py::gil_scoped_acquire gil;
        numpyIdentity = reinterpret_cast<std::uintptr_t>(py::module_::import("numpy").ptr());
        py::module_::import("py_compile")
            .attr("compile")((sources.directory / "helper.py").string(), py::arg("doraise") = true);
    }
    const auto loaded = runtime.refresh(definitions);
    for (const auto &record : loaded) assert(testview::status(record).code == 0);
    const ToolServices services;
    {
        auto       instance = runtime.factory(optional)(services);
        const auto result   = instance->execute({});
        assert(testview::status(result).code == 0 && isMissing(result.outputs.at("value")));
        assert(instance->release());
    }
    {
        auto badInstance = runtime.factory(constructor)(services);
        assert(badInstance != nullptr);
        const auto status = badInstance->initialize({});
        assert(status.code == -1);
        assert(status.detail.find("constructor detail") != std::string::npos);
    }
    auto first  = runtime.factory(counter)(services);
    auto second = runtime.factory(counter)(services);
    assert(first->initialize({}).code == 0 && second->initialize({}).code == 0);
    assert(std::get<std::int64_t>(first->execute({}).outputs.at("value")) == 1);
    assert(std::get<std::int64_t>(first->execute({}).outputs.at("value")) == 2);
    assert(std::get<std::int64_t>(second->execute({}).outputs.at("value")) == 1);
    rejects([&] { runtime.refresh(definitions); });
    assert(first->release().code == 0 && second->release().code == 0);
    first.reset();
    second.reset();

    auto       failures       = runtime.factory(broken)(services);
    const auto initialization = failures->initialize({});
    assert(initialization.code == -1 && initialization.message == "Python 初始化异常");
    assert(initialization.detail.find("initialization detail") != std::string::npos);
    assert(initialization.detail.find("entry.py") != std::string::npos);
    const auto execution = testview::executionFailure(*failures);
    assert(execution.find("Python 执行异常") != std::string::npos);
    assert(execution.find("execution detail") != std::string::npos);
    const auto cleanup = failures->release();
    assert(cleanup.code == -4 && cleanup.message == "Python 释放异常");
    assert(cleanup.detail.find("release detail") != std::string::npos);
    failures.reset();
    for (const auto &definition : {missing, extra, wrong})
    {
        auto        instance = runtime.factory(definition)(services);
        const auto failed    = testview::executionFailure(*instance);
        assert(failed.find("技术详情") == std::string::npos);
        if (definition.id == wrong.id)
            assert(failed.find("Python int") != std::string::npos);
        else
            // A key set that does not match the declaration is refused before any value is read.
            assert(failed.find("输出键必须与声明端口") != std::string::npos);
    }
    {
        auto       instance = runtime.factory(business)(services);
        const auto result   = instance->execute({});
        assert(testview::status(result).code == 5 && std::get<std::int64_t>(result.outputs.at("value")) == 7);
    }
    {
        // A declared port the tool answered with the wrong type is a broken answer, not a value.
        auto       instance = runtime.factory(partial)(services);
        const auto failed   = testview::executionFailure(*instance);
        assert(failed.find("other") != std::string::npos);
    }
    const auto oldFactory = runtime.factory(function);
    {
        auto instance = oldFactory(services);
        assert(std::get<std::int64_t>(instance->execute({}).outputs.at("value")) == 11);
    }
    const auto timestamp = std::filesystem::last_write_time(sources.directory / "helper.py");
    sources.put("helper.py", "value = 22\nobsolete = True\n");
    std::filesystem::last_write_time(sources.directory / "helper.py", timestamp);
    runtime.refresh(std::span<const ToolDefinition>(&function, 1));
    {
        auto instance = runtime.factory(function)(services);
        assert(std::get<std::int64_t>(instance->execute({}).outputs.at("value")) == 22);
    }
    sources.put("helper.py", "value = 33\n");
    sources.put("entry.py", R"(
from . import helper
def execute(inputs):
    assert not hasattr(helper, 'obsolete')
    return {'__status_code': 0, '__status_message': '', 'value': helper.value}
)");
    runtime.refresh(std::span<const ToolDefinition>(&function, 1));
    {
        auto instance = runtime.factory(function)(services);
        assert(std::get<std::int64_t>(instance->execute({}).outputs.at("value")) == 33);
        py::gil_scoped_acquire gil;
        assert(reinterpret_cast<std::uintptr_t>(py::module_::import("numpy").ptr()) == numpyIdentity);
    }
    sources.put("entry.py", "def execute(:\n");
    const auto invalid = runtime.refresh(std::span<const ToolDefinition>(&function, 1));
    assert(invalid[0].status.code == 0);
    {
        auto inst = runtime.factory(function)(services);
        assert(inst != nullptr);
        const auto status = inst->initialize({});
        assert(status.code == -3 && status.detail.find("SyntaxError") != std::string::npos);
        try
        {
            runtime.factory(function)(services);
            assert(false);
        }
        catch (const RegistryError &error)
        {
            assert(testview::status(error).code == -3 && testview::status(error).detail.find("SyntaxError") != std::string::npos);
        }
    }
    {
        rejects([&] { oldFactory(services); });
    }
    runtime.refresh({});
}

void pathInputs(PythonRuntime &runtime)
{
    Sources sources;
    sources.put("entry.py", R"(
class PathProbe:
    def initialize(self, params):
        self.base = params['base_dir']
        return {'__status_code': 0, '__status_message': 'ok'}
    def execute(self, inputs):
        return {'__status_code': 0, '__status_message': 'ok',
                'resolved': self.base + '|' + inputs['input_path']}
    def release(self):
        return {'__status_code': 0, '__status_message': 'ok'}
)");
    Sources::write(sources.declarations / "paths.json", R"json({
  "schema_version": 4,
  "tool_id": "python.paths",
  "display_name": "Path probe",
  "version": "1.0.0",
  "kind": "python_class",
  "entry": {"package": "math", "file": "entry.py", "symbol": "PathProbe"},
  "init_params": [
    {"id": "init_mode", "name": "Mode", "type": "String", "editor": "text"},
    {"id": "base_dir", "name": "Base", "type": "String", "editor": "path", "path_mode_from": "init_mode"}
  ],
  "inputs": [
    {"id": "input_mode", "name": "Mode", "type": "String", "editor": "text"},
    {"id": "input_path", "name": "Input", "type": "String", "editor": "path", "path_mode_from": "input_mode"}
  ],
  "outputs": [
    {"id": "resolved", "name": "Resolved", "type": "String"}
  ]
})json");
    const auto definition = parseToolDefinitionFile(sources.declarations / "paths.json", sources.source());
    const auto loaded     = runtime.refresh(std::span<const ToolDefinition>(&definition, 1));
    assert(loaded.size() == 1 && loaded[0].status.code == 0);

    const auto project = sources.directory / std::filesystem::path(u8"工程 目录");
    std::filesystem::create_directories(project);
    const auto text = [](const std::filesystem::path &value)
    {
        const auto utf8 = value.lexically_normal().u8string();
        return std::string(reinterpret_cast<const char *>(utf8.data()), utf8.size());
    };
    const auto         expectedBase  = text(project / std::filesystem::path(L"数据") / L"模型.bin");
    const auto         expectedInput = text(project / std::filesystem::path(L"图像") / L"输入.png");
    const ToolServices services{PathResolver(project)};
    auto               instance = runtime.factory(definition)(services);
    assert(instance
               ->initialize({
                   {"init_mode",              std::string("file")},
                   { "base_dir", std::string("数据/模型.bin")}
    })
               .code
           == 0);
    const auto result = instance->execute({
        {"input_mode",              std::string("file")},
        {"input_path", std::string("图像/输入.png")}
    });
    assert(testview::status(result).code == 0);
    assert(std::get<std::string>(result.outputs.at("resolved")) == expectedBase + "|" + expectedInput);

    const auto empty = instance->execute({
        {"input_mode", std::string("file")},
        {"input_path",     std::string("")}
    });
    assert(testview::status(empty).code == 0);
    assert(std::get<std::string>(empty.outputs.at("resolved")) == expectedBase + "|");
    instance.reset();
    runtime.refresh({});
}
}

int main()
{
    PythonRuntime runtime;
    try
    {
        auto worker = std::async(std::launch::async,
                                 [&]
                                 {
                                     adaptersAndRefresh(runtime);
                                     pathInputs(runtime);
                                 });
        worker.get();
        std::cout << "Python adapter status, lifecycle, atomic outputs, refreshed sources and project-relative paths "
                     "passed\n";
        return 0;
    }
    catch (const std::exception &error)
    {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
