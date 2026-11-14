#include "core/DefinitionJson.h"
#include "core/Error.h"
#include "ToolResultAssertions.h"
#include "core/ToolRegistry.h"
#include "python/PythonRuntime.h"
#include "TestPythonEnvironment.h"
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
        tool.nameEn             = id;
        tool.nameZh    = "Contract tool";
        tool.kind           = kind;
        tool.definitionPath = declarations / (id + ".json");
        tool.source         = source();
        tool.entry          = {"math", "entry.py", symbol};
        tool.outputs.push_back({2, "Value", {Type::Int}});
        if (secondOutput)
            tool.outputs.push_back({3, "Other", {Type::Float}});
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
    return {'outputs': {2: helper.value, 0: 0, 1: ''}, 'detail': ''}
class Counter:
    def initialize(self, inputs):
        self.count = 0
        return {'code': 0, 'message': '', 'detail': ''}
    def execute(self, inputs):
        self.count += 1
        return {'outputs': {2: self.count, 0: 0, 1: ''}, 'detail': ''}
    def release(self):
        return {'code': 0, 'message': '', 'detail': ''}
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
    return {'code': 0, 'message': '', 'detail': ''}
def extra(inputs):
    return {'outputs': {2: 1, 9: 2, 0: 0, 1: ''}, 'detail': ''}
def wrong(inputs):
    return {'outputs': {2: True, 0: 0, 1: ''}, 'detail': ''}
def business(inputs):
    return {'outputs': {2: 7, 0: 5, 1: 'expected failure'}, 'detail': ''}
def partial(inputs):
    return {'outputs': {2: 9, 3: True, 0: 0, 1: ''}, 'detail': ''}
def absent(inputs):
    return {'outputs': {2: None, 0: 0, 1: ''}, 'detail': ''}
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
        if (port.id == 2)
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
    {
        auto       instance = runtime.factory(optional)();
        const auto result   = instance->execute({});
        assert(testview::status(result).code == 0 && isMissing(result.outputs.at(2)));
        assert(instance->release());
    }
    {
        auto badInstance = runtime.factory(constructor)();
        assert(badInstance != nullptr);
        const auto status = badInstance->initialize({});
        assert(status.code == -1);
        assert(status.detail.find("constructor detail") != std::string::npos);
    }
    auto first  = runtime.factory(counter)();
    auto second = runtime.factory(counter)();
    assert(first->initialize({}).code == 0 && second->initialize({}).code == 0);
    assert(std::get<std::int64_t>(first->execute({}).outputs.at(2)) == 1);
    assert(std::get<std::int64_t>(first->execute({}).outputs.at(2)) == 2);
    assert(std::get<std::int64_t>(second->execute({}).outputs.at(2)) == 1);
    rejects([&] { runtime.refresh(definitions); });
    assert(first->release().code == 0 && second->release().code == 0);
    first.reset();
    second.reset();

    auto       failures       = runtime.factory(broken)();
    const auto initialization = failures->initialize({});
    assert(initialization.code == -1 && initialization.message == "Python 初始化异常");
    assert(initialization.detail.find("initialization detail") != std::string::npos);
    assert(initialization.detail.find("entry.py") != std::string::npos);
    const auto execution = testview::executionFailure(*failures);
    assert(execution.find("Python 执行异常") != std::string::npos);
    assert(execution.find("execution detail") != std::string::npos);
    const auto cleanup = failures->release();
    // A release that throws is an internal defect. Reporting it as NotFound used to send an
    // operator looking for a missing tool after what was actually a resource failure.
    assert(cleanup.code == hostCode(ErrorCode::Internal) && cleanup.message == "Python 释放异常");
    assert(cleanup.detail.find("release detail") != std::string::npos);
    failures.reset();
    for (const auto &definition : {missing, extra, wrong})
    {
        auto        instance = runtime.factory(definition)();
        const auto failed    = testview::executionFailure(*instance);
        assert(failed.find("技术详情") == std::string::npos);
        if (definition.nameEn == wrong.nameEn)
            assert(failed.find("Python int") != std::string::npos);
        else if (definition.nameEn == extra.nameEn)
            // A name the declaration never mentions is still refused; an omitted declared port is not.
            assert(failed.find("输出键不在声明中") != std::string::npos);
        else
            // A status dict is a lifecycle answer: execute owes the two-field envelope.
            assert(failed.find("execute 必须返回 outputs 与 detail") != std::string::npos);
    }
    {
        auto       instance = runtime.factory(business)();
        const auto result   = instance->execute({});
        assert(testview::status(result).code == 5 && std::get<std::int64_t>(result.outputs.at(2)) == 7);
    }
    {
        // A declared port the tool answered with the wrong type is a broken answer, not a value.
        auto       instance = runtime.factory(partial)();
        const auto failed   = testview::executionFailure(*instance);
        assert(failed.find("端口 3") != std::string::npos);
    }
    const auto oldFactory = runtime.factory(function);
    {
        auto instance = oldFactory();
        assert(std::get<std::int64_t>(instance->execute({}).outputs.at(2)) == 11);
    }
    const auto timestamp = std::filesystem::last_write_time(sources.directory / "helper.py");
    sources.put("helper.py", "value = 22\nobsolete = True\n");
    std::filesystem::last_write_time(sources.directory / "helper.py", timestamp);
    runtime.refresh(std::span<const ToolDefinition>(&function, 1));
    {
        auto instance = runtime.factory(function)();
        assert(std::get<std::int64_t>(instance->execute({}).outputs.at(2)) == 22);
    }
    sources.put("helper.py", "value = 33\n");
    sources.put("entry.py", R"(
from . import helper
def execute(inputs):
    assert not hasattr(helper, 'obsolete')
    return {'outputs': {2: helper.value, 0: 0, 1: ''}, 'detail': ''}
)");
    runtime.refresh(std::span<const ToolDefinition>(&function, 1));
    {
        auto instance = runtime.factory(function)();
        assert(std::get<std::int64_t>(instance->execute({}).outputs.at(2)) == 33);
        py::gil_scoped_acquire gil;
        assert(reinterpret_cast<std::uintptr_t>(py::module_::import("numpy").ptr()) == numpyIdentity);
    }
    sources.put("entry.py", "def execute(:\n");
    const auto invalid = runtime.refresh(std::span<const ToolDefinition>(&function, 1));
    assert(invalid[0].status.code == 0);
    {
        auto inst = runtime.factory(function)();
        assert(inst != nullptr);
        const auto status = inst->initialize({});
        assert(status.code == hostCode(ErrorCode::PythonUnavailable)
               && status.detail.find("SyntaxError") != std::string::npos);
        try
        {
            runtime.factory(function)();
            assert(false);
        }
        catch (const RegistryError &error)
        {
            assert(testview::status(error).code == hostCode(ErrorCode::PythonUnavailable)
                   && testview::status(error).detail.find("SyntaxError") != std::string::npos);
        }
    }
    {
        rejects([&] { oldFactory(); });
    }
    runtime.refresh({});
}

void pathInputs(PythonRuntime &runtime)
{
    Sources sources;
    sources.put("entry.py", R"(
class PathProbe:
    def initialize(self, params):
        self.base = params[1]
        return {'code': 0, 'message': 'ok', 'detail': ''}
    def execute(self, inputs):
        return {'outputs': {2: self.base + '|' + inputs[2], 0: 0, 1: 'ok'},
                'detail': ''}
    def release(self):
        return {'code': 0, 'message': 'ok', 'detail': ''}
)");
    Sources::write(sources.declarations / "paths.json", R"json({
  "name_en": "python.paths",
  "name_zh": "Path probe",
  "version": "1.0.0",
  "implementation": {
    "kind": "python_class",
    "package": "math",
    "file": "entry.py",
    "symbol": "PathProbe"
  },
  "init_params": [
    {
      "id": 0,
      "name_en": "init_mode",
      "name_zh": "Mode",
      "type": "string",
      "ui": {
        "editor": "text"
      }
    },
    {
      "id": 1,
      "name_en": "base_dir",
      "name_zh": "Base",
      "type": "string",
      "ui": {
        "editor": "path"
      },
      "relations": {
        "path_mode": {
          "stage": "init",
          "id": 0
        }
      }
    }
  ],
  "inputs": [
    {
      "id": 1,
      "name_en": "input_mode",
      "name_zh": "Mode",
      "type": "string",
      "ui": {
        "editor": "text"
      }
    },
    {
      "id": 2,
      "name_en": "input_path",
      "name_zh": "Input",
      "type": "string",
      "ui": {
        "editor": "path"
      },
      "relations": {
        "path_mode": {
          "stage": "inputs",
          "id": 1
        }
      }
    }
  ],
  "outputs": [
    {
      "id": 2,
      "name_en": "resolved",
      "name_zh": "Resolved",
      "type": "string"
    }
  ]
})json");
    const auto definition = parseToolDefinitionFile(sources.declarations / "paths.json", sources.source());
    const auto loaded     = runtime.refresh(std::span<const ToolDefinition>(&definition, 1));
    assert(loaded.size() == 1 && loaded[0].status.code == 0);

    // A path-editor string reaches Python exactly as the user entered it: the bridge does not know
    // which strings name files, and the tool decides what to do with the text.
    auto instance = runtime.factory(definition)();
    assert(instance
               ->initialize({
                   {0, std::string("file")},
                   {1, std::string("数据/模型.bin")}
    })
               .code
           == 0);
    const auto result = instance->execute({
        {1, std::string("file")},
        {2, std::string("图像/输入.png")}
    });
    assert(testview::status(result).code == 0);
    assert(std::get<std::string>(result.outputs.at(2)) == "数据/模型.bin|图像/输入.png");

    const auto empty = instance->execute({
        {1, std::string("file")},
        {2, std::string("")}
    });
    assert(testview::status(empty).code == 0);
    assert(std::get<std::string>(empty.outputs.at(2)) == "数据/模型.bin|");
    instance.reset();
    runtime.refresh({});
}
}

int main()
{
    PythonRuntime runtime(testsupport::pythonEnvironment());
    try
    {
        auto worker = std::async(std::launch::async,
                                 [&]
                                 {
                                     adaptersAndRefresh(runtime);
                                     pathInputs(runtime);
                                 });
        worker.get();
        std::cout << "Python adapter status, lifecycle, atomic outputs, refreshed sources and verbatim path text "
                     "passed\n";
        return 0;
    }
    catch (const std::exception &error)
    {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
