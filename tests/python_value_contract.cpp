#include "python/PythonInterop.h"

#include <pybind11/embed.h>
#include <pybind11/numpy.h>

#include <array>
#include <cassert>
#include <cstdint>
#include <future>
#include <iostream>
#include <memory>

namespace py = pybind11;
using namespace visionrt;

namespace {

void rejected(const TypeSpec &type, py::handle value, const PythonStorage &storage, std::string_view location)
{
    bool failed = false;
    try
    {
        fromPython(type, value, storage, "port");
    }
    catch (const ContractError &error)
    {
        failed = std::string_view(error.what()).find(location) != std::string_view::npos;
    }
    assert(failed);
}

void nativeLists(const PythonStorage &storage)
{
    for (const auto type : Array::supportedTypes())
    {
        std::string expression;
        switch (*type.dtype)
        {
        case DType::UInt8:
            expression = "[0, 255]";
            break;
        case DType::Int32:
            expression = "[-2147483648, 2147483647]";
            break;
        case DType::Int64:
            expression = "[-9223372036854775808, 9223372036854775807]";
            break;
        case DType::Float32:
            expression = "[-0.5, 1.25]";
            break;
        case DType::Float64:
            expression = "[-0.5, 1.25]";
            break;
        case DType::Bool:
            expression = "[False, True]";
            break;
        case DType::String:
            expression = "['图像😀', '']";
            break;
        }
        for (int rank = 1; rank < type.rank; ++rank) expression = "[[], " + expression + "]";
        const auto source = py::eval(expression);
        const auto value  = fromPython(type, source, storage);
        assert(std::get<Array>(value).type() == type);
        auto converted = toPython(value);
        assert(PyList_CheckExact(converted.ptr()));
        assert(converted.equal(source));
        const auto empty = fromPython(type, py::list(), storage);
        assert(std::get<Array>(empty).type() == type && toPython(empty).cast<py::list>().empty());
    }
    auto source                      = py::eval("[[1.0, 2.0], [], [3.0]]");
    auto value                       = fromPython({Type::Array, DType::Float64, 2}, source, storage);
    auto converted                   = toPython(value).cast<py::list>();
    converted[0].cast<py::list>()[0] = 99.0;
    assert((std::get<Array>(value).get<NestedVector<double, 2>>()[0][0] == 1.0));
    assert(source.cast<py::list>()[0].cast<py::list>()[0].cast<double>() == 1.0);
    const auto returned = fromPython({Type::Array, DType::Float64, 2}, converted, storage);
    assert((std::get<Array>(returned).get<NestedVector<double, 2>>()[0][0] == 99.0));

    const auto roi      = py::eval(R"([[
        [0.0, 1.25, 2.5], [1.0, 0.0, 0.0, 1.0, 2.0], [2.0, 1.0, 2.0, 3.0, 4.0, 0.0],
        [3.0, 3.0, 4.0, 2.0], [4.0, 0.0, 0.0, 1.0, 0.0, 1.0, 1.0], [5.0, 7.0, 8.0]], []])");
    const auto geometry = fromPython({Type::ROI}, roi, storage);
    assert(toPython(geometry).equal(roi));
    assert(std::get<DataRef<ROI>>(geometry).get()[0][0][1] == 1.25);
    const TypeSpec bytes{Type::Array, DType::UInt8, 1};
    rejected(bytes, py::eval("[True]"), storage, "port[0]");
    rejected(bytes, py::eval("[-1]"), storage, "port[0]");
    rejected(bytes, py::eval("[256]"), storage, "port[0]");
    rejected({Type::Array, DType::Int32, 1}, py::eval("[2147483648]"), storage, "port[0]");
    rejected({Type::Array, DType::Int64, 1}, py::eval("[9223372036854775808]"), storage, "port[0]");
    rejected({Type::Array, DType::Float32, 1}, py::eval("[1e40]"), storage, "port[0]");
    rejected({Type::Array, DType::Float64, 2}, py::eval("[[0.0, 1]]"), storage, "port[0][1]");
    rejected({Type::Array, DType::String, 1}, py::eval("[b'bytes']"), storage, "port[0]");
    rejected({Type::Array, DType::Bool, 1}, py::eval("[0]"), storage, "port[0]");
    rejected({Type::Array, DType::Float64, 2}, py::eval("[[None]]"), storage, "port[0][0]");
    rejected(bytes, py::eval("(1, 2)"), storage, "port");
    rejected(bytes, py::eval("__import__('numpy').array([1], dtype='uint8')"), storage, "port");
    rejected({Type::Array, DType::Float64, 1}, py::eval("[__import__('numpy').float64(0.5)]"), storage, "port[0]");
    rejected({Type::ROI}, py::eval("[[[0.0, 1.0]]]"), storage, "port[0][0]");
    rejected({Type::Array, DType::String, 1}, py::eval("['\\ud800']"), storage, "port[0]");
    assert(isMissing(fromPython({Type::Image}, py::none(), storage)));
    assert(std::get<DataRef<Image>>(fromPython({Type::Image}, py::list(), storage)).get().empty());
}

void imageLayouts(const PythonStorage &storage)
{
    py::dict scope;
    py::exec("import numpy as np\nsource = np.arange(60, dtype=np.uint16).reshape(4, 5, 3)", scope);
    const std::vector<std::string> invalid{
        "source.transpose(1, 0, 2)",
        "source[::-1]",
        "source[:, ::2]",
        "source[:, :, ::2]",
        "source.astype(np.int64)",
        "source.astype(np.complex64)",
        "source.astype(object)",
        "source.astype('>u2')",
        "source[:0]",
        "None",
        "np.lib.stride_tricks.as_strided(source, shape=(4,5,3), strides=(10000,6,2))"};
    for (const auto &expression : invalid)
        rejected({Type::Image}, py::list(py::make_tuple(py::eval(expression, scope))), storage, "port[0]");
    py::exec("readonly = np.arange(6, dtype=np.uint8).reshape(2, 3)\nreadonly.flags.writeable = False", scope);
    rejected({Type::Image}, py::list(py::make_tuple(scope["readonly"])), storage, "不可写");
    assert(!scope["readonly"].cast<py::array>().writeable());
    for (const auto *dtype : {"uint8", "int8", "uint16", "int16", "int32", "float32", "float64", "float16"})
    {
        scope["dtype"]     = dtype;
        auto        source = py::eval("np.ones((3,4,1), dtype=dtype)", scope).cast<py::array>();
        auto        value  = fromPython({Type::Image}, py::list(py::make_tuple(source)), storage);
        const auto &image  = std::get<DataRef<Image>>(value).get()[0];
        assert(image.data == source.data() && image.channels() == 1);
        auto output = toPython(value).cast<py::list>()[0].cast<py::array>();
        assert(output.ndim() == 2 && output.data() == source.data());
        assert(output.dtype().equal(source.dtype()) && output.writeable());
    }
}

void workerRelease(const PythonStorage &storage)
{
    py::weakref retained;
    PortValue   output;
    {
        py::array_t<std::uint16_t> array({3, 4});
        *array.mutable_data(1, 2) = 71;
        retained                  = py::weakref(array);
        output                    = fromPython({Type::Image}, py::list(py::make_tuple(array)), storage);
    }
    assert(!retained().is_none());
    for (int index = 0; index < 500; ++index)
    {
        std::weak_ptr<void> previous = std::get<DataRef<Image>>(output).owner();
        auto                view     = toPython(output).cast<py::list>();
        py::weakref         temporary(view[0]);
        output = fromPython({Type::Image}, view, storage);
        view   = py::list();
        assert(previous.expired() && temporary().is_none());
    }
    std::promise<void> dispose;
    auto               permission = dispose.get_future();
    auto               worker     = std::async(std::launch::async,
                                               [value = std::move(output), permission = std::move(permission)]() mutable
                                               {
                                 cv::Mat image = std::get<DataRef<Image>>(value).get()[0](cv::Rect(1, 1, 2, 2));
                                 value = Missing{};
                                 assert(image.at<std::uint16_t>(0, 1) == 71);
                                 permission.wait();
                                 image.release();
                             });
    {
        auto drained = std::async(std::launch::async, [&] { storage.waitForRelease(); });
        assert(drained.wait_for(std::chrono::milliseconds(30)) == std::future_status::timeout);
        dispose.set_value();
        storage.waitForRelease();
        worker.get();
        drained.get();
    }
    assert(retained().is_none());
}

}

int main()
{
    py::scoped_interpreter interpreter;
    try
    {
        PythonStorage       storage;
        int                 released = 0;
        std::weak_ptr<void> retained;
        {
            auto pixels = std::shared_ptr<std::array<std::uint8_t, 12>>(
                new std::array<std::uint8_t, 12>{1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12},
                [&](auto *pointer)
                {
                    delete pointer;
                    ++released;
                });
            DataRef<Image> source(Image{cv::Mat(3, 4, CV_8UC1, pixels->data())}, {pixels});
            retained    = source.owner();
            auto images = toPython(source).cast<py::list>();
            auto array  = images[0].cast<py::array>();
            assert(array.data() == pixels->data());
            assert(array.writeable());
            py::dict scope;
            scope["images"] = images;
            py::exec(R"(
images[0][1, 2] = 91
cropped = [images[0][1:, 1:3]]
)",
                     scope);
            assert(pixels->at(6) == 91);
            auto  output = fromPython({Type::Image}, scope["cropped"], storage);
            auto &crop   = std::get<DataRef<Image>>(output).get()[0];
            assert(crop.data == pixels->data() + 5 && crop.step[0] == 4);
            assert(crop.rows == 2 && crop.cols == 2);
            source.get().clear();
            scope.clear();
            images = py::list();
            array  = py::array();
            for (int index = 0; index < 500; ++index) output = fromPython({Type::Image}, toPython(output), storage);
            auto &last                  = std::get<DataRef<Image>>(output).get()[0];
            last.at<std::uint8_t>(0, 1) = 73;
            assert(pixels->at(6) == 73);
            assert(released == 0);
        }
        storage.waitForRelease();
        assert(released == 1 && retained.expired());
        nativeLists(storage);
        imageLayouts(storage);
        workerRelease(storage);
        std::cout << "Writable NumPy views retain storage and preserve padded crops across 500 round trips\n";
        return 0;
    }
    catch (const std::exception &error)
    {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
