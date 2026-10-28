#include "core/DefinitionJson.h"
#include "result_view.h"
#include "core/Error.h"
#include "core/ToolRegistry.h"
#include "python/PythonRuntime.h"

#include <pybind11/embed.h>
#include <QCoreApplication>
#include <QDir>
#include <QProcess>
#include <QTemporaryDir>
#include <array>
#include <cassert>
#include <filesystem>
#include <fstream>
#include <future>
#include <iostream>
#include <thread>

using namespace visionrt;
namespace py = pybind11;

namespace {

void write(const std::filesystem::path &path, std::string_view content)
{
    std::filesystem::create_directories(path.parent_path());
    std::ofstream stream(path, std::ios::binary);
    stream << content;
    assert(stream.good());
}

ToolDefinition definitionFor(const std::filesystem::path &root, std::string id, std::filesystem::path file,
                             ToolOrigin origin = ToolOrigin::Custom, std::string package = "math")
{
    const auto folder = origin == ToolOrigin::Builtin ? "builtin" : "custom";
    auto       result = parseToolDefinitionJson(R"({
        "schema_version":4,"tool_id":"python.fixture","display_name":"Fixture","version":"1",
        "kind":"python_function","entry":{"package":"math","file":"tool.py","symbol":"execute"},
        "init_params":[],"inputs":[],"outputs":[{"id":"value","name":"Value","type":"Int"}]
    })",
                                                root / "tools" / folder / "tool.json",
                                                {
                                              origin,
                                              {root / "tools" / folder, root / "python" / folder}
    });
    result.id            = std::move(id);
    result.entry.package = std::move(package);
    result.entry.file    = std::move(file);
    return result;
}

std::int64_t execute(IToolInstance &instance)
{
    const auto result = instance.execute({});
    assert(testview::status(result).code == 0);
    return std::get<std::int64_t>(result.outputs.at("value"));
}

// The expected code is part of the contract: "the implementation is gone" and "the runtime closed
// under this instance" are different failures, and the test says which one it is asserting.
void unavailable(const ToolFactory &factory, ErrorCode expected)
{
    bool         failed = false;
    std::int64_t actual = 0;
    try
    {
        auto instance = factory({});
        if (instance)
        {
            const auto status = instance->initialize({});
            failed = !status && (actual = status.code) == hostCode(expected);
        }
    }
    catch (const RegistryError &error)
    {
        actual = testview::status(error).code;
        failed = error.reason == ToolAvailability::ImplementationUnavailable && actual == hostCode(expected);
    }
    if (!failed)
        std::cerr << "unavailable: expected " << hostCode(expected) << " actual " << actual << '\n';
    assert(failed);
}

void sharedPackages(PythonRuntime &runtime, const std::filesystem::path &root)
{
    const auto  source  = root / "python/custom/math";
    const auto  builtin = root / "python/builtin/math";
    const char *tool
        = "from .helper import next_value\ndef execute(inputs): return {'status':{'code':0,'message':'','detail':''}, 'outputs':{"
          "'value':next_value()}}\n";
    write(source / "helper.py", "count=0\ndef next_value():\n global count\n count+=1\n return count\n");
    write(source / "first.py", tool);
    write(source / "second.py", tool);
    write(builtin / "helper.py", "def next_value(): return 101\n");
    write(builtin / "first.py", tool);
    const std::array definitions{definitionFor(root, "python.first", "first.py"),
                                 definitionFor(root, "python.second", "second.py"),
                                 definitionFor(root, "python.builtin", "first.py", ToolOrigin::Builtin)};
    runtime.refresh(definitions);
    auto first  = runtime.factory(definitions[0])({});
    auto second = runtime.factory(definitions[1])({});
    auto built  = runtime.factory(definitions[2])({});
    assert(execute(*first) == 1 && execute(*second) == 2 && execute(*built) == 101 && execute(*first) == 3);
}

void classInstances(PythonRuntime &runtime, const std::filesystem::path &root)
{
    write(root / "python/custom/math/counter.py", R"(
class Counter:
    def initialize(self, parameters):
        self.count = 0
        return {'code': 0, 'message': '', 'detail': ''}
    def execute(self, inputs):
        self.count += 1
        return {'status': {'code': 0, 'message': '', 'detail': ''}, 'outputs': {'value':self.count}}
    def release(self):
        return {'code': 0, 'message': '', 'detail': ''}
)");
    auto definition         = definitionFor(root, "python.counter", "counter.py");
    definition.kind         = ToolKind::PythonClass;
    definition.entry.symbol = "Counter";
    runtime.refresh(std::span(&definition, 1));
    auto first  = runtime.factory(definition)({});
    auto second = runtime.factory(definition)({});
    assert(first->initialize({}).code == 0 && second->initialize({}).code == 0);
    assert(execute(*first) == 1 && execute(*second) == 1 && execute(*first) == 2);
}

void freshGenerations(PythonRuntime &runtime, const std::filesystem::path &root)
{
    const auto source     = root / "python/custom/math/fresh.py";
    auto       definition = definitionFor(root, "python.fresh", "fresh.py");
    write(source, "raise RuntimeError('import failure')\n");
    assert(runtime.refresh(std::span(&definition, 1))[0].status.code == 0);
    const auto old = runtime.factory(definition);
    unavailable(old, ErrorCode::PythonUnavailable);
    const char *fixed = "def execute(inputs): return {'status': {'code': 0, 'message': '', 'detail': ''}, 'outputs': {'value':13}}\n";
    write(source, fixed);
    unavailable(old, ErrorCode::PythonUnavailable);
    runtime.refresh(std::span(&definition, 1));
    unavailable(old, ErrorCode::PythonUnavailable);
    {
        auto instance = runtime.factory(definition)({});
        assert(execute(*instance) == 13);
    }
    const auto timestamp = std::filesystem::last_write_time(source);
    write(source, "def execute(inputs): return {'status': {'code': 0, 'message': '', 'detail': ''}, 'outputs': {'value':14}}\n");
    std::filesystem::last_write_time(source, timestamp);
    runtime.refresh(std::span(&definition, 1));
    {
        auto instance = runtime.factory(definition)({});
        assert(execute(*instance) == 14);
    }
    auto missing = definitionFor(root, "python.missing", "first_only_builtin.py");
    write(root / "python/builtin/math/first_only_builtin.py", fixed);
    const auto missingStatus = runtime.refresh(std::span(&missing, 1));
    assert(missingStatus.size() == 1 && missingStatus.front().status.code == hostCode(ErrorCode::NotFound));
    unavailable(runtime.factory(missing), ErrorCode::NotFound);
}

void nativeHelper(PythonRuntime &runtime, const std::filesystem::path &root)
{
    auto definition                = definitionFor(root, "python.native", "native.py");
    definition.source.roots.python = root / "python-extension-contract";
    const auto package             = definition.pythonPackage();
    std::filesystem::create_directories(package);
    {
        py::gil_scoped_acquire gil;
        const auto             filename  = py::module_::import("_queue").attr("__file__").cast<std::string>();
        const auto             extension = std::filesystem::path(std::u8string(filename.begin(), filename.end()));
        std::filesystem::copy_file(extension, package / extension.filename(),
                                   std::filesystem::copy_options::overwrite_existing);
    }
    write(package / "native.py",
          "from . import _queue\ndef execute(inputs): return {'status':{'code':0,'message':'','detail':''}, 'outputs':{"
          "'value':int(_queue.SimpleQueue().empty())}}\n");
    runtime.refresh(std::span(&definition, 1));
    auto instance = runtime.factory(definition)({});
    assert(execute(*instance) == 1);
}

void packageBoundaries(PythonRuntime &runtime, const std::filesystem::path &root)
{
    const auto package  = root / "python/custom/math";
    const auto external = root / "external";
    write(external / "helper.py",
          "from pathlib import Path\nPath(__file__).with_name('imported.txt').write_text('escaped')\nvalue=71\n");
    const auto link = package / "linked";
#ifdef _WIN32
    QProcess command;
    command.start("cmd.exe",
                  {"/d", "/c", "mklink", "/J", QDir::toNativeSeparators(QString::fromStdWString(link.wstring())),
                   QDir::toNativeSeparators(QString::fromStdWString(external.wstring()))});
    assert(command.waitForFinished(30000));
    if (command.exitCode() != 0)
        std::cerr << command.readAllStandardError().toStdString();
    assert(command.exitStatus() == QProcess::NormalExit && command.exitCode() == 0);
#else
    std::filesystem::create_directory_symlink(external, link);
#endif
    write(package / "escape.py",
          "from .linked.helper import value\ndef execute(inputs): return {'status':{'code':0,'message':'','detail':''}, 'outputs':{"
          "'value':value}}\n");
    const auto escaped = definitionFor(root, "python.escape", "escape.py");
    runtime.refresh(std::span(&escaped, 1));
    unavailable(runtime.factory(escaped), ErrorCode::PythonUnavailable);
    assert(!std::filesystem::exists(external / "imported.txt"));
    assert(std::filesystem::remove(link));
    assert(std::filesystem::exists(external / "helper.py"));

    write(package / "__init__.py",
          "value=81\ndef execute(inputs): return {'status': {'code': 0, 'message': '', 'detail': ''}, 'outputs': {'value':value}}\n");
    const auto entry = definitionFor(root, "python.package_entry", "__init__.py");
    runtime.refresh(std::span(&entry, 1));
    {
        auto instance = runtime.factory(entry)({});
        assert(execute(*instance) == 81);
    }
    const auto named = definitionFor(root, "python.unicode_entry", std::filesystem::path(u8"模块.py"));
    write(package / named.entry.file,
          "from . import value\ndef execute(inputs): return {'status':{'code':0,'message':'','detail':''}, 'outputs':{"
          "'value':value+1}}\n");
    runtime.refresh(std::span(&named, 1));
    {
        auto instance = runtime.factory(named)({});
        assert(execute(*instance) == 82);
    }
}

void coldConcurrentRollback(PythonRuntime &runtime, const std::filesystem::path &root)
{
    const auto package = root / "python/custom/cold_concurrent";
    std::filesystem::create_directories(package);
    write(package / "__init__.py",
          "import builtins\n"
          "if not hasattr(builtins, 'visionrt_cold_root_count'):\n"
          " builtins.visionrt_cold_root_count = 0\n"
          "builtins.visionrt_cold_root_count += 1\n"
          "TOKEN = builtins.visionrt_cold_root_count\n");
    write(package / "good.py",
          "from . import TOKEN\n"
          "def execute(inputs): return {'status': {'code': 0, 'message': '', 'detail': ''}, 'outputs': {'value':TOKEN}}\n");
    write(package / "sibling.py",
          "from . import TOKEN\n"
          "def execute(inputs): return {'status': {'code': 0, 'message': '', 'detail': ''}, 'outputs': {'value':TOKEN}}\n");
    write(package / "broken.py",
          "from pathlib import Path\n"
          "import time\n"
          "directory = Path(__file__).parent\n"
          "(directory / 'broken_entered.flag').write_text('entered')\n"
          "deadline = time.monotonic() + 5\n"
          "while not (directory / 'broken_resume.flag').exists():\n"
          " if time.monotonic() > deadline: raise TimeoutError('resume timeout')\n"
          " time.sleep(0.005)\n"
          "raise RuntimeError('controlled first-load failure in worker A')\n");

    const std::array definitions{
        definitionFor(root, "python.cold_good", "good.py", ToolOrigin::Custom, "cold_concurrent"),
        definitionFor(root, "python.cold_sibling", "sibling.py", ToolOrigin::Custom, "cold_concurrent"),
        definitionFor(root, "python.cold_broken", "broken.py", ToolOrigin::Custom, "cold_concurrent")
    };
    runtime.refresh(definitions);

    std::promise<void> brokenStarted;
    auto brokenFuture = brokenStarted.get_future();
    std::thread workerBroken([&] {
        brokenStarted.set_value();
        unavailable(runtime.factory(definitions[2]), ErrorCode::PythonUnavailable);
    });
    brokenFuture.wait();

    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(5);
    while (!std::filesystem::exists(package / "broken_entered.flag"))
    {
        assert(std::chrono::steady_clock::now() < deadline);
        std::this_thread::sleep_for(std::chrono::milliseconds(5));
    }

    auto good = runtime.factory(definitions[0])({});
    assert(execute(*good) == 1);

    write(package / "broken_resume.flag", "resume");
    workerBroken.join();

    auto sibling = runtime.factory(definitions[1])({});
    assert(execute(*sibling) == 1);
}

void sharedHelperRollback(PythonRuntime &runtime, const std::filesystem::path &root)
{
    const auto package = root / "python/custom/shared_helper";
    std::filesystem::create_directories(package);
    write(package / "__init__.py", "");
    write(package / "helper.py",
          "import builtins\n"
          "if not hasattr(builtins, 'visionrt_helper_count'):\n"
          " builtins.visionrt_helper_count = 0\n"
          "builtins.visionrt_helper_count += 1\n"
          "VALUE = builtins.visionrt_helper_count\n");
    write(package / "good.py",
          "from . import helper\n"
          "def execute(inputs): return {'status': {'code': 0, 'message': '', 'detail': ''}, 'outputs': {'value':helper.VALUE}}\n");
    write(package / "sibling.py",
          "from . import helper\n"
          "def execute(inputs): return {'status': {'code': 0, 'message': '', 'detail': ''}, 'outputs': {'value':helper.VALUE}}\n");
    write(package / "broken.py",
          "from . import helper\n"
          "from pathlib import Path\n"
          "import time\n"
          "directory = Path(__file__).parent\n"
          "(directory / 'broken_entered.flag').write_text('entered')\n"
          "deadline = time.monotonic() + 5\n"
          "while not (directory / 'broken_resume.flag').exists():\n"
          " if time.monotonic() > deadline: raise TimeoutError('resume timeout')\n"
          " time.sleep(0.005)\n"
          "raise RuntimeError('controlled failure after shared helper import')\n");

    const std::array definitions{
        definitionFor(root, "python.shared_good", "good.py", ToolOrigin::Custom, "shared_helper"),
        definitionFor(root, "python.shared_sibling", "sibling.py", ToolOrigin::Custom, "shared_helper"),
        definitionFor(root, "python.shared_broken", "broken.py", ToolOrigin::Custom, "shared_helper")
    };
    runtime.refresh(definitions);

    std::promise<void> brokenStarted;
    auto brokenFuture = brokenStarted.get_future();
    std::thread workerBroken([&] {
        brokenStarted.set_value();
        unavailable(runtime.factory(definitions[2]), ErrorCode::PythonUnavailable);
    });
    brokenFuture.wait();

    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(5);
    while (!std::filesystem::exists(package / "broken_entered.flag"))
    {
        assert(std::chrono::steady_clock::now() < deadline);
        std::this_thread::sleep_for(std::chrono::milliseconds(5));
    }

    auto good = runtime.factory(definitions[0])({});
    assert(execute(*good) == 1);

    write(package / "broken_resume.flag", "resume");
    workerBroken.join();

    auto sibling = runtime.factory(definitions[1])({});
    assert(execute(*sibling) == 1);
}

void constantAndNumpyRollback(PythonRuntime &runtime, const std::filesystem::path &root)
{
    const auto package = root / "python/custom/constant_numpy";
    std::filesystem::create_directories(package);
    write(package / "__init__.py", "");
    write(package / "helper.py",
          "import builtins\n"
          "if not hasattr(builtins, 'visionrt_cn_helper_count'):\n"
          " builtins.visionrt_cn_helper_count = 0\n"
          "builtins.visionrt_cn_helper_count += 1\n"
          "VALUE = builtins.visionrt_cn_helper_count\n");
    write(package / "good.py",
          "from .helper import VALUE\n"
          "def execute(inputs): return {'status': {'code': 0, 'message': '', 'detail': ''}, 'outputs': {'value':VALUE}}\n");
    write(package / "sibling.py",
          "from .helper import VALUE\n"
          "def execute(inputs): return {'status': {'code': 0, 'message': '', 'detail': ''}, 'outputs': {'value':VALUE}}\n");
    write(package / "broken.py",
          "from . import helper\n"
          "from pathlib import Path\n"
          "import time\n"
          "directory = Path(__file__).parent\n"
          "(directory / 'cn_broken_entered.flag').write_text('entered')\n"
          "deadline = time.monotonic() + 5\n"
          "while not (directory / 'cn_broken_resume.flag').exists():\n"
          " if time.monotonic() > deadline: raise TimeoutError('resume timeout')\n"
          " time.sleep(0.005)\n"
          "raise RuntimeError('controlled failure after helper import')\n");

    write(package / "numpy_warm.py",
          "import numpy as np\n"
          "FRAME = np.arange(4).reshape(2, 2)\n"
          "def execute(inputs): return {'status': {'code': 0, 'message': '', 'detail': ''}, 'outputs': {'value':int(FRAME.sum())}}\n");
    write(package / "numpy_broken.py",
          "from . import helper\n"
          "raise RuntimeError('controlled original failure')\n");

    const std::array definitions{
        definitionFor(root, "python.cn_good", "good.py", ToolOrigin::Custom, "constant_numpy"),
        definitionFor(root, "python.cn_sibling", "sibling.py", ToolOrigin::Custom, "constant_numpy"),
        definitionFor(root, "python.cn_broken", "broken.py", ToolOrigin::Custom, "constant_numpy"),
        definitionFor(root, "python.cn_np_warm", "numpy_warm.py", ToolOrigin::Custom, "constant_numpy"),
        definitionFor(root, "python.cn_np_broken", "numpy_broken.py", ToolOrigin::Custom, "constant_numpy")
    };
    runtime.refresh(definitions);

    auto npWarm = runtime.factory(definitions[3])({});
    assert(execute(*npWarm) == 6);

    bool brokenFailed = false;
    try
    {
        auto inst = runtime.factory(definitions[4])({});
        if (inst)
        {
            auto status = inst->initialize({});
            brokenFailed = !status && status.detail.find("controlled original failure") != std::string::npos;
        }
    }
    catch (const RegistryError &error)
    {
        brokenFailed = testview::status(error).detail.find("controlled original failure") != std::string::npos;
    }
    assert(brokenFailed);

    std::promise<void> brokenStarted;
    auto brokenFuture = brokenStarted.get_future();
    std::thread workerBroken([&] {
        brokenStarted.set_value();
        unavailable(runtime.factory(definitions[2]), ErrorCode::PythonUnavailable);
    });
    brokenFuture.wait();

    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(5);
    while (!std::filesystem::exists(package / "cn_broken_entered.flag"))
    {
        assert(std::chrono::steady_clock::now() < deadline);
        std::this_thread::sleep_for(std::chrono::milliseconds(5));
    }

    auto good = runtime.factory(definitions[0])({});
    assert(execute(*good) == 1);

    write(package / "cn_broken_resume.flag", "resume");
    workerBroken.join();

    auto sibling = runtime.factory(definitions[1])({});
    assert(execute(*sibling) == 1);

    write(package / "helper.py", "VALUE = 99\n");
    auto sibling2 = runtime.factory(definitions[1])({});
    assert(execute(*sibling2) == 1);

    good.reset();
    sibling.reset();
    sibling2.reset();
    npWarm.reset();

    runtime.refresh(definitions);
    auto siblingFresh = runtime.factory(definitions[1])({});
    assert(execute(*siblingFresh) == 99);
}

}

int main(int argc, char **argv)
try
{
    QCoreApplication application(argc, argv);
    QTemporaryDir    temporary;
    assert(temporary.isValid());
    const auto root = std::filesystem::path(temporary.path().toStdWString());
    const auto code = root / "python/custom/math";
    std::filesystem::create_directories(code);
    {
        std::ofstream script(code / "tool.py");
        script << "from pathlib import Path\n"
                  "Path(__file__).with_name('imported.txt').write_text('imported')\n"
                  "def execute(inputs): return {'status': {'code': 0, 'message': '', 'detail': ''}, 'outputs': {'value':23}}\n";
    }
    const auto    definition = parseToolDefinitionJson(R"({
        "schema_version":4, "tool_id":"python.lazy", "display_name":"Lazy", "version":"1",
        "kind":"python_function", "entry":{"package":"math", "file":"tool.py", "symbol":"execute"},
        "init_params":[], "inputs":[], "outputs":[{"id":"value","name":"Value","type":"Int"}]
    })",
                                                       root / "tools/custom/math/tool.json",
                                                       {
                                                        ToolOrigin::Custom,
                                                        {root / "tools/custom", root / "python/custom"}
    });
    PythonRuntime runtime;
    auto          worker = std::async(std::launch::async,
                                      [&]
                                      {
                                 const auto statuses = runtime.refresh(std::span(&definition, 1));
                                 assert(statuses.size() == 1 && statuses[0].status.code == 0);
                                 assert(!std::filesystem::exists(code / "imported.txt"));
                                 auto instance = runtime.factory(definition)({});
                                 assert(!std::filesystem::exists(code / "imported.txt"));
                                 assert(instance->initialize({}).code == 0);
                                 assert(std::filesystem::exists(code / "imported.txt"));
                                 const auto result = instance->execute({});
                                 assert(testview::status(result).code == 0);
                                 assert(std::get<std::int64_t>(result.outputs.at("value")) == 23);
                                 assert(instance->release().code == 0);
                                 instance.reset();
                                 sharedPackages(runtime, root);
                                 classInstances(runtime, root);
                                 freshGenerations(runtime, root);
                                 nativeHelper(runtime, root);
                                 packageBoundaries(runtime, root);
                                  coldConcurrentRollback(runtime, root);
                                  sharedHelperRollback(runtime, root);
                                  constantAndNumpyRollback(runtime, root);
                             });
    worker.get();
    std::cout << "Metadata refresh does not import Python; direct adapter construction loads its code root\n";
    return 0;
}
catch (const std::exception &error)
{
    std::cerr << error.what() << '\n';
    return 1;
}
