#include "core/DefinitionJson.h"
#include "python/PythonRuntime.h"

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
    const auto root    = std::filesystem::path(temporary.path().toStdWString());
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
    return {'__status_code':0, '__status_message':'', 'images':[image], 'namespace':__name__}
)";
    }
    const auto definition = parseToolDefinitionJson(R"({
        "schema_version":2,"tool_id":"python.memory","display_name":"Memory","version":"1",
        "kind":"python_function","entry":{"package":"memory","file":"tool.py","symbol":"execute"},
        "init_params":[],"inputs":[],"outputs":[
            {"id":"images","name":"Images","type":"Image"},
            {"id":"namespace","name":"Namespace","type":"String"}]
    })",
                                                    root / "tools/custom/memory.json",
                                                    {
                                                        ToolOrigin::Custom,
                                                        {root / "tools/custom", root / "python/custom"}
    });
    auto       runtime    = std::make_unique<PythonRuntime>();
    auto       execution  = std::async(std::launch::async,
                                       [&]
                                       {
                                    runtime->refresh(std::span(&definition, 1));
                                    auto instance = runtime->factory(definition)({});
                                    assert(instance->initialize({}).code == 0);
                                    auto result = instance->execute({});
                                    assert(result.status.code == 0);
                                    return result;
                                });
    auto       output     = execution.get();
    assert(!std::filesystem::exists(package / "released.txt"));
    auto release = std::async(std::launch::async,
                              [value = std::move(output)]() mutable
                              {
                                  const auto name     = std::get<std::string>(value.outputs.at("namespace"));
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
                                  auto &images = std::get<DataRef<Image>>(value.outputs.at("images")).get();
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
