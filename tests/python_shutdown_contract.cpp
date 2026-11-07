#include "core/DefinitionJson.h"
#include "ToolResultAssertions.h"
#include "python/PythonRuntime.h"
#include "TestPythonEnvironment.h"

#include <pybind11/embed.h>
#include <QTemporaryDir>
#include <cassert>
#include <filesystem>
#include <fstream>
#include <future>
#include <iostream>
#include <thread>

namespace py = pybind11;
using namespace visionrt;

int main()
try
{
    QTemporaryDir temporary;
    assert(temporary.isValid());
    const auto root = std::filesystem::path(temporary.path().toStdWString());

    // C1: Concurrent destruction of adapters during runtime shutdown executes release for all adapters
    {
        const auto package = root / "python/custom/concurrent_shutdown";
        std::filesystem::create_directories(package);
        {
            std::ofstream script(package / "tool.py");
            script << "from pathlib import Path\n"
                      "import time\n"
                      "next_id = 0\n"
                      "class Tool:\n"
                      " def __init__(self):\n"
                      "  global next_id\n"
                      "  self.identity = next_id\n"
                      "  next_id += 1\n"
                      " def initialize(self, params): return {'code': 0, 'message': '', 'detail': ''}\n"
                      " def execute(self, inputs): return {'outputs': {2:1, 0: 0, 1: ''}, 'detail': ''}\n"
                      " def release(self):\n"
                      "  directory = Path(__file__).parent\n"
                      "  first = directory / 'first.flag'\n"
                      "  if not first.exists(): first.write_text(str(self.identity))\n"
                      "  deadline = time.monotonic() + 8\n"
                      "  while not (directory / 'finished.flag').exists():\n"
                      "   if time.monotonic() > deadline: raise TimeoutError('worker did not finish')\n"
                      "   time.sleep(0.005)\n"
                      "  with (directory / 'released.log').open('a') as stream:\n"
                      "   stream.write(str(self.identity) + '\\n')\n"
                      "  return {'code': 0, 'message': '', 'detail': ''}\n";
        }
        const auto definition = parseToolDefinitionJson(R"({"schema_version":6,"name_en":"audit.concurrent.shutdown","name_zh":"Shutdown","version":"1","implementation":{"kind":"python_class","package":"concurrent_shutdown","file":"tool.py","symbol":"Tool"},"next_port_id":{"init":0,"inputs":1,"outputs":3},"ports":{"outputs":[{"id":2,"name_en":"value","name_zh":"Value","type":"int"}]}})", root / "tools/custom/concurrent_shutdown.json", {ToolOrigin::Custom,
                                                            {root / "tools/custom", root / "python/custom"}});
        auto runtime = std::make_unique<PythonRuntime>(testsupport::pythonEnvironment());
        runtime->refresh(std::span(&definition, 1));
        std::vector<std::unique_ptr<IToolInstance>> instances;
        for (int index = 0; index < 3; ++index)
        {
            auto instance = runtime->factory(definition)({});
            assert(instance->initialize({}).code == 0);
            instances.push_back(std::move(instance));
        }
        bool workerCompleted = false;
        std::thread worker([&] {
            const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(10);
            int firstIdentity = -1;
            while (firstIdentity < 0)
            {
                if (std::chrono::steady_clock::now() > deadline) return;
                std::ifstream marker(package / "first.flag");
                std::string identity;
                if (marker >> identity) firstIdentity = std::stoi(identity);
                if (firstIdentity < 0) std::this_thread::sleep_for(std::chrono::milliseconds(5));
            }
            for (int index = 0; index < 3; ++index)
                if (index != firstIdentity) instances[index].reset();
            workerCompleted = true;
            std::ofstream(package / "finished.flag") << "finished";
        });
        runtime.reset();
        worker.join();
        instances.clear();
        assert(workerCompleted);
        int releaseCount = 0;
        std::string line;
        std::ifstream released(package / "released.log");
        while (std::getline(released, line)) ++releaseCount;
        assert(releaseCount == 3);
    }

    // D1: In-flight Python release in worker thread completes before runtime shutdown returns
    {
        const auto package = root / "python/custom/inflight_shutdown";
        std::filesystem::create_directories(package);
        {
            std::ofstream script(package / "tool.py");
            script << "from pathlib import Path\n"
                      "import time\n"
                      "class Tool:\n"
                      " def initialize(self, params): return {'code': 0, 'message': '', 'detail': ''}\n"
                      " def execute(self, inputs): return {'outputs': {2:1, 0: 0, 1: ''}, 'detail': ''}\n"
                      " def release(self):\n"
                      "  directory = Path(__file__).parent\n"
                      "  (directory / 'entered.flag').write_text('entered')\n"
                      "  time.sleep(0.3)\n"
                      "  (directory / 'finished.flag').write_text('finished')\n"
                      "  return {'code': 0, 'message': '', 'detail': ''}\n";
        }
        const auto definition = parseToolDefinitionJson(R"({"schema_version":6,"name_en":"audit.release.inflight","name_zh":"Shutdown","version":"1","implementation":{"kind":"python_class","package":"inflight_shutdown","file":"tool.py","symbol":"Tool"},"next_port_id":{"init":0,"inputs":1,"outputs":3},"ports":{"outputs":[{"id":2,"name_en":"value","name_zh":"Value","type":"int"}]}})", root / "tools/custom/inflight_shutdown.json", {ToolOrigin::Custom,
                                                            {root / "tools/custom", root / "python/custom"}});
        auto runtime = std::make_unique<PythonRuntime>(testsupport::pythonEnvironment());
        runtime->refresh(std::span(&definition, 1));
        auto instance = runtime->factory(definition)({});
        assert(instance->initialize({}).code == 0);
        std::atomic<bool> workerFinished{false};
        std::thread worker([owned = std::move(instance), &workerFinished]() mutable {
            owned.reset();
            workerFinished.store(true);
        });
        const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(5);
        while (!std::filesystem::exists(package / "entered.flag"))
        {
            assert(std::chrono::steady_clock::now() < deadline);
            std::this_thread::sleep_for(std::chrono::milliseconds(5));
        }
        runtime.reset();
        assert(std::filesystem::exists(package / "finished.flag"));
        worker.join();
        assert(workerFinished.load());
    }
    const auto package = root / "python/custom/memory";
    std::filesystem::create_directories(package);
    {
        std::ofstream script(package / "tool.py");
        script << R"(
from pathlib import Path
import weakref
import numpy as np

def execute(inputs):
    image = np.full((3,4), 71, dtype=np.uint16)
    weakref.finalize(image, lambda: Path(__file__).with_name('released.txt').write_text('released'))
    return {'outputs': {2:[image], 3:__name__, 0: 0, 1: ''}, 'detail': ''}
)";
    }
    const auto definition = parseToolDefinitionJson(R"({"schema_version":6,"name_en":"python.memory","name_zh":"Memory","version":"1","implementation":{"kind":"python_function","package":"memory","file":"tool.py","symbol":"execute"},"next_port_id":{"init":0,"inputs":1,"outputs":4},"ports":{"outputs":[{"id":2,"name_en":"images","name_zh":"Images","type":"image"},{"id":3,"name_en":"namespace","name_zh":"Namespace","type":"string"}]}})",
                                                    root / "tools/custom/memory.json",
                                                    {
                                                        ToolOrigin::Custom,
                                                        {root / "tools/custom", root / "python/custom"}
    });
    auto       runtime    = std::make_unique<PythonRuntime>(testsupport::pythonEnvironment());
    auto       execution  = std::async(std::launch::async,
                                       [&]
                                       {
                                    runtime->refresh(std::span(&definition, 1));
                                    auto instance = runtime->factory(definition)({});
                                    assert(instance->initialize({}).code == 0);
                                    auto result = instance->execute({});
                                    assert(testview::status(result).code == 0);
                                    return result;
                                });
    auto       output     = execution.get();
    assert(!std::filesystem::exists(package / "released.txt"));
    auto release = std::async(std::launch::async,
                              [value = std::move(output)]() mutable
                              {
                                  const auto name     = std::get<std::string>(value.outputs.at(3));
                                  const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(5);
                                  bool       cleared  = false;
                                  while (!cleared && std::chrono::steady_clock::now() < deadline)
                                  {
                                      assert(Py_IsInitialized());
                                      {
                                          py::gil_scoped_acquire gil;
                                          auto modules = py::module_::import("sys").attr("modules").cast<py::dict>();
                                          cleared      = !modules.contains(py::str(name));
                                          assert(modules.contains("numpy"));
                                      }
                                      if (!cleared)
                                          std::this_thread::sleep_for(std::chrono::milliseconds(1));
                                  }
                                  assert(cleared);
                                  auto &images = std::get<DataRef<Image>>(value.outputs.at(2)).get();
                                  assert(images[0].at<std::uint16_t>(1, 2) == 71);
                                  images[0].at<std::uint16_t>(1, 2) = 91;
                                  value.outputs.clear();
                              });
    runtime.reset();
    release.get();
    assert(!Py_IsInitialized());
    std::ifstream marker(package / "released.txt");
    std::string   content;
    marker >> content;
    assert(content == "released");
    std::cout << "Shutdown clears only owned modules and keeps Python alive through final worker-owned image release\n";
    return 0;
}
catch (const std::exception &error)
{
    std::cerr << error.what() << '\n';
    return 1;
}
