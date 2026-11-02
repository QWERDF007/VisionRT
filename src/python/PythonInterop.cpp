#include "python/PythonInterop.h"

#include "python/PythonImages.h"

#include <pybind11/numpy.h>

#include <cmath>
#include <cstring>
#include <limits>
#include <memory>

namespace visionrt {
namespace py = pybind11;
namespace {

template<class Scalar>
Scalar scalar(py::handle value, const std::string &path)
{
    if constexpr (std::is_same_v<Scalar, bool>)
    {
        if (!PyBool_Check(value.ptr()))
            throw ContractError(path + " 需要 Python bool");
        return value.ptr() == Py_True;
    }
    else if constexpr (std::is_integral_v<Scalar>)
    {
        if (!PyLong_CheckExact(value.ptr()))
            throw ContractError(path + " 需要 Python int，不接受 bool 或隐式转换");
        const auto result = PyLong_AsLongLong(value.ptr());
        if (PyErr_Occurred())
        {
            PyErr_Clear();
            throw ContractError(path + " 超出 int64 范围");
        }
        if (result < std::numeric_limits<Scalar>::min() || result > std::numeric_limits<Scalar>::max())
            throw ContractError(path + " 超出声明的整数范围");
        return static_cast<Scalar>(result);
    }
    else if constexpr (std::is_floating_point_v<Scalar>)
    {
        if (!PyFloat_CheckExact(value.ptr()))
            throw ContractError(path + " 需要 Python float，不接受 int 或 NumPy 标量");
        const auto result    = PyFloat_AS_DOUBLE(value.ptr());
        const auto converted = static_cast<Scalar>(result);
        if (std::isfinite(result) && !std::isfinite(converted))
            throw ContractError(path + " 超出声明的浮点范围");
        return converted;
    }
    else
    {
        if (!PyUnicode_CheckExact(value.ptr()))
            throw ContractError(path + " 需要 Unicode str，不接受 bytes");
        Py_ssize_t  length = 0;
        const auto *text   = PyUnicode_AsUTF8AndSize(value.ptr(), &length);
        if (!text)
        {
            py::error_already_set error;
            throw ContractError(path + " 无法表示为 UTF-8：" + error.what());
        }
        return std::string(text, static_cast<std::size_t>(length));
    }
}

template<class Vector>
Vector decodeList(py::handle value, const std::string &path)
{
    if (!PyList_CheckExact(value.ptr()))
        throw ContractError(path + " 需要原生 Python list");
    const auto list = py::reinterpret_borrow<py::list>(value);
    using Element   = typename Vector::value_type;
    Vector result;
    result.reserve(list.size());
    for (std::size_t index = 0; index < list.size(); ++index)
    {
        const auto location = path + "[" + std::to_string(index) + "]";
        if constexpr (std::is_arithmetic_v<Element> || std::is_same_v<Element, std::string>)
            result.push_back(scalar<Element>(list[index], location));
        else
            result.push_back(decodeList<Element>(list[index], location));
    }
    return result;
}

template<class Vector>
py::list encodeList(const Vector &values)
{
    using Element = typename Vector::value_type;
    py::list result;
    for (const auto &value : values)
    {
        if constexpr (std::is_same_v<Element, bool>)
            result.append(py::bool_(static_cast<bool>(value)));
        else if constexpr (std::is_arithmetic_v<Element> || std::is_same_v<Element, std::string>)
            result.append(py::cast(value));
        else
            result.append(encodeList(value));
    }
    return result;
}

py::dtype numpyDtype(DType dtype)
{
    switch (dtype)
    {
    case DType::UInt8:
        return py::dtype::of<std::uint8_t>();
    case DType::Int32:
        return py::dtype::of<std::int32_t>();
    case DType::Int64:
        return py::dtype::of<std::int64_t>();
    case DType::Float32:
        return py::dtype::of<float>();
    case DType::Float64:
        return py::dtype::of<double>();
    case DType::Bool:
        return py::dtype("bool");
    case DType::String:
        break;
    }
    throw ContractError("字符串数组没有 NumPy 表示");
}

std::size_t numpyElementSize(DType dtype)
{
    switch (dtype)
    {
    case DType::UInt8:
    case DType::Bool:
        return 1;
    case DType::Int32:
    case DType::Float32:
        return 4;
    case DType::Int64:
    case DType::Float64:
        return 8;
    case DType::String:
        break;
    }
    throw ContractError("字符串数组没有 NumPy 表示");
}

py::array numpyView(const Array &array)
{
    const auto dtype = numpyDtype(array.dtype());
    std::vector<py::ssize_t> shape;
    shape.reserve(array.shape().size());
    for (const auto dimension : array.shape()) shape.push_back(static_cast<py::ssize_t>(dimension));
    std::vector<py::ssize_t> strides(shape.size());
    py::ssize_t              stride = static_cast<py::ssize_t>(numpyElementSize(array.dtype()));
    for (std::size_t index = shape.size(); index-- > 0;)
    {
        strides[index] = stride;
        stride *= shape[index];
    }
    // The capsule holds a shallow Array copy, not just its buffer owner: the shared Storage owns the
    // buffer and the dependency that keeps a plugin module loaded. Holding only the buffer would let
    // the module unload while NumPy still reads the view.
    auto        keeper = std::make_unique<Array>(array);
    py::capsule base(keeper.get(), [](void *pointer) { delete static_cast<Array *>(pointer); });
    keeper.release();
    return py::array(dtype, shape, strides, array.data(), base);
}

template<class T>
py::object arrayLeaf(const Array &array, std::size_t index)
{
    if constexpr (std::is_same_v<T, bool>)
        return py::bool_(array.boolBytes()[index] != 0);
    else if constexpr (std::is_same_v<T, std::string>)
        return py::str(array.elements<std::string>()[index]);
    else
        return py::cast(array.elements<T>()[index]);
}

template<class T>
py::list denseList(const Array &array, int dimension, std::size_t &index)
{
    py::list  result;
    const auto &shape = array.shape();
    for (std::int64_t i = 0; i < shape[static_cast<std::size_t>(dimension)]; ++i)
    {
        if (dimension + 1 == static_cast<int>(shape.size()))
            result.append(arrayLeaf<T>(array, index++));
        else
            result.append(denseList<T>(array, dimension + 1, index));
    }
    return result;
}

template<class T>
py::list raggedList(const Array &array, int level, std::size_t group, std::size_t &index)
{
    py::list    result;
    const auto &bounds = array.offsets()[static_cast<std::size_t>(level)];
    if (level + 1 == static_cast<int>(array.offsets().size()))
    {
        for (auto leaf = bounds[group]; leaf < bounds[group + 1]; ++leaf)
            result.append(arrayLeaf<T>(array, index++));
        return result;
    }
    for (auto child = bounds[group]; child < bounds[group + 1]; ++child)
        result.append(raggedList<T>(array, level + 1, static_cast<std::size_t>(child), index));
    return result;
}

template<class T>
py::object arrayList(const Array &array)
{
    if (array.layout() == ArrayLayout::Dense)
    {
        std::size_t index = 0;
        return denseList<T>(array, 0, index);
    }
    std::size_t index = 0;
    py::list    result;
    const auto  groups = array.offsets().empty() ? 0 : array.offsets()[0].size() - 1;
    for (std::size_t group = 0; group < groups; ++group)
        result.append(raggedList<T>(array, 0, group, index));
    return result;
}

py::object arrayToPython(const Array &array)
{
    if (array.layout() == ArrayLayout::Dense && array.dtype() != DType::String)
        return numpyView(array);
    switch (array.dtype())
    {
    case DType::UInt8:
        return arrayList<std::uint8_t>(array);
    case DType::Int32:
        return arrayList<std::int32_t>(array);
    case DType::Int64:
        return arrayList<std::int64_t>(array);
    case DType::Float32:
        return arrayList<float>(array);
    case DType::Float64:
        return arrayList<double>(array);
    case DType::Bool:
        return arrayList<bool>(array);
    case DType::String:
        return arrayList<std::string>(array);
    }
    throw ContractError("未知数组 dtype");
}

template<class T>
Array listArray(const py::handle &value, int rank, const std::string &path)
{
    switch (rank)
    {
    case 1:
        return Array::fromNested(decodeList<NestedVector<T, 1>>(value, path));
    case 2:
        return Array::fromNested(decodeList<NestedVector<T, 2>>(value, path));
    case 3:
        return Array::fromNested(decodeList<NestedVector<T, 3>>(value, path));
    case 4:
        return Array::fromNested(decodeList<NestedVector<T, 4>>(value, path));
    }
    throw ContractError(path + " 的数组 rank 必须在支持范围内");
}

Array arrayFromList(const TypeSpec &expected, const py::handle &value, const std::string &path)
{
    Array result;
    switch (*expected.dtype)
    {
    case DType::UInt8:
        result = listArray<std::uint8_t>(value, expected.rank, path);
        break;
    case DType::Int32:
        result = listArray<std::int32_t>(value, expected.rank, path);
        break;
    case DType::Int64:
        result = listArray<std::int64_t>(value, expected.rank, path);
        break;
    case DType::Float32:
        result = listArray<float>(value, expected.rank, path);
        break;
    case DType::Float64:
        result = listArray<double>(value, expected.rank, path);
        break;
    case DType::Bool:
        result = listArray<bool>(value, expected.rank, path);
        break;
    case DType::String:
        result = listArray<std::string>(value, expected.rank, path);
        break;
    }
    if (result.rank() != expected.rank || result.dtype() != *expected.dtype)
        throw ContractError(path + " 的数组结构与声明不一致");
    return result;
}

Array arrayFromNumpy(const TypeSpec &expected, const py::handle &value, const PythonStorage &storage,
                     const std::string &path)
{
    auto array = py::reinterpret_borrow<py::array>(value);
    if (!array.dtype().is(numpyDtype(*expected.dtype)))
        throw ContractError(path + " 的 NumPy dtype 必须精确匹配，不接受隐式转换");
    if (array.ndim() != expected.rank)
        throw ContractError(path + " 的 NumPy 维度与声明 rank 不一致");
    std::vector<std::int64_t> shape(static_cast<std::size_t>(array.ndim()));
    std::size_t               count = 1;
    for (py::ssize_t dimension = 0; dimension < array.ndim(); ++dimension)
    {
        shape[static_cast<std::size_t>(dimension)] = array.shape(dimension);
        if (array.shape(dimension) < 0)
            throw ContractError(path + " 的 NumPy shape 不能为负");
        count *= static_cast<std::size_t>(array.shape(dimension));
    }
    if (count != static_cast<std::size_t>(array.size()))
        throw ContractError(path + " 的 NumPy 元素数量与 shape 不一致");
    if ((array.flags() & py::array::c_style) == 0 || !array.writeable())
    {
        // Read-only or strided input costs one explicit copy; it is never a zero-copy claim.
        py::object numpy = py::module_::import("numpy");
        py::array  contiguous = numpy.attr("ascontiguousarray")(array, numpyDtype(*expected.dtype));
        if (!contiguous.writeable())
            contiguous = contiguous.attr("copy")();
        return Array::borrowedDense(*expected.dtype, std::move(shape), contiguous.mutable_data(), count,
                                    storage.retain(contiguous));
    }
    return Array::borrowedDense(*expected.dtype, std::move(shape), array.mutable_data(), count,
                                storage.retain(value));
}
}

py::object toPython(const PortValue &value)
{
    if (const auto type = typeOf(value))
        validateValue(*type, value);
    return std::visit(
        [](const auto &payload) -> py::object
        {
            using Payload = std::decay_t<decltype(payload)>;
            if constexpr (std::is_same_v<Payload, Missing>)
                return py::none();
            else if constexpr (std::is_same_v<Payload, DataRef<Image>>)
                return python::imageViews(payload);
            else if constexpr (std::is_same_v<Payload, DataRef<ROI>>)
                return encodeList(payload.get());
            else if constexpr (std::is_same_v<Payload, Array>)
                return arrayToPython(payload);
            else
                return py::cast(payload);
        },
        value);
}

PortValue fromPython(const TypeSpec &expected, py::handle value, const PythonStorage &storage, std::string path)
{
    validateTypeSpec(expected);
    if (value.is_none())
        return Missing{};
    switch (expected.type)
    {
    case Type::Int:
        return scalar<std::int64_t>(value, path);
    case Type::Float:
        return scalar<double>(value, path);
    case Type::Bool:
        return scalar<bool>(value, path);
    case Type::String:
        return scalar<std::string>(value, path);
    case Type::Image:
        return python::imageHeaders(value, storage, path);
    case Type::Array:
        if (py::isinstance<py::array>(value))
            return arrayFromNumpy(expected, value, storage, path);
        return arrayFromList(expected, value, path);
    case Type::ROI:
    {
        auto result = decodeList<ROI>(value, path);
        for (std::size_t group = 0; group < result.size(); ++group)
            for (std::size_t record = 0; record < result[group].size(); ++record) try
                {
                    validateRoi(result[group][record]);
                }
                catch (const ContractError &error)
                {
                    throw ContractError(path + "[" + std::to_string(group) + "][" + std::to_string(record) + "]："
                                        + error.what());
                }
        return DataRef<ROI>(std::move(result));
    }
    }
    throw ContractError(path + " 的类型不受支持");
}
}
