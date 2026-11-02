#include "plugins/PluginLoader.h"
#include "python/PythonInterop.h"
#include "ToolResultAssertions.h"

#include <pybind11/embed.h>
#include <pybind11/numpy.h>

#include <QTemporaryDir>
#include <cassert>
#include <fstream>
#include <iostream>

namespace py = pybind11;
using namespace visionrt;

// A NumPy view of a plugin Array must outlive every C++ handle: the capsule keeps the Array, whose
// storage owns the buffer and the module dependency. Releasing the view then unloads the module
// after the buffer deleter has run.
int main(int argc, char **argv)
{
    assert(argc == 2);
    const auto    library = std::filesystem::absolute(argv[1]);
    QTemporaryDir directory;
    assert(directory.isValid());
    const auto    markerPath = directory.filePath("array.txt");
    const auto    marker     = markerPath.toStdString();
    const auto    markerFile = std::filesystem::path(markerPath.toStdWString());

    py::scoped_interpreter interpreter;
    py::object            row;
    try
    {
        std::weak_ptr<const LoadedModule> alive;
        {
            auto       loader = std::make_shared<PluginLoader>();
            ToolStatus status;
            auto       module = loader->loadModule(library, status);
            assert(status && module);
            alive = module;
            {
                DomainToolAdapter adapter(module, "test.value.lifetime", PlatformToolServices{});
                assert(adapter.initialize({
                    {"marker", marker}
                }));
                auto result = adapter.execute({});
                assert(testview::status(result) && testview::businessCount(result) == 4);
                PortValue value = result.outputs.at("grid");
                {
                    auto view = toPython(value);
                    assert(py::isinstance<py::array>(view));
                    assert(view.cast<py::array>().writeable());
                    // A row slice is a NumPy view of the same buffer and keeps its own reference.
                    row = view[py::int_(0)];
                    assert(py::isinstance<py::array>(row) && py::len(row) == 2);
                }
                result.outputs.clear();
                value = Missing{};
                assert(adapter.release());
            }
            loader.reset();
            module.reset();
            // Only the exported slice keeps the plugin module and its buffer alive now.
            assert(!alive.expired());
#ifdef _WIN32
            assert(GetModuleHandleW(library.c_str()) != nullptr);
#endif
            assert(!std::filesystem::exists(markerFile));
            assert(row.attr("tolist")().cast<py::list>()[0].cast<std::int64_t>() == 17);
            row.attr("__setitem__")(py::int_(1), py::int_(42));
            assert(row.attr("tolist")().cast<py::list>()[1].cast<std::int64_t>() == 42);
        }
        row = py::none();
        assert(alive.expired());
#ifdef _WIN32
        assert(GetModuleHandleW(library.c_str()) == nullptr);
#endif
        std::ifstream released(markerFile);
        std::string   message;
        std::getline(released, message);
        assert(message == "released" && released.peek() == std::char_traits<char>::eof());
        std::cout << "NumPy view outlives plugin tool results and unloads the module after the buffer\n";
        return 0;
    }
    catch (const std::exception &error)
    {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
