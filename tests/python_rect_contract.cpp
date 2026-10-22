#ifdef NDEBUG
#undef NDEBUG
#endif

#include "python/PythonInterop.h"

#include <pybind11/embed.h>
#include <pybind11/numpy.h>
#include <cassert>
#include <iostream>

namespace py = pybind11;
using namespace visionrt;

int main() {
    py::scoped_interpreter interpreter;
    try {
        const auto numpy = py::module_::import("numpy");
        py::object view;
        std::weak_ptr<const std::vector<float>> lifetime;
        {
            const auto rectangle = Roi::rectangle(1.25F, -2.5F, 3.75F, 4.5F, 17.5F);
            lifetime = rectangle.owner();
            view = toPythonReadonly(rectangle);
            const auto array = py::cast<py::array>(view);
            assert(array.ndim() == 1 && array.shape(0) == 6);
            assert(array.dtype().equal(py::dtype::of<float>()));
            assert(!array.writeable());
            assert(array.data() == rectangle.encoded().data());
        }
        assert(!lifetime.expired());
        auto copied = fromPython({Type::Rect}, view);
        assert(equalValues(copied, Roi::rectangle(1.25F, -2.5F, 3.75F, 4.5F, 17.5F)));
        assert(std::get<Roi>(copied).encoded().data() != py::cast<py::array>(view).data());
        view = py::none();
        assert(lifetime.expired());
        assert(std::get<Roi>(copied).encoded()[1] == 1.25F);
        for (auto expression : {"[3, 1, 2, 3]", "[2, 0, 0, 0, 5, 0]", "[[2, 0, 0, 4, 5, 0]]"}) {
            const auto malformed = numpy.attr("array")(py::eval(expression), py::arg("dtype") = "float32");
            bool rejected = false;
            try { (void)fromPython({Type::Rect}, malformed); }
            catch (const std::exception&) { rejected = true; }
            assert(rejected);
        }
        bool rejected = false;
        try { (void)fromPython({Type::Rect}, numpy.attr("array")(py::eval("[2, 0, 0, 4, 5, 0]"))); }
        catch (const std::exception&) { rejected = true; }
        assert(rejected);
        assert(isMissing(fromPython({Type::Rect}, py::none())));
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
