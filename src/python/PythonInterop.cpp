#include "python/PythonInterop.h"

#include "python/PythonImages.h"

#include <cmath>
#include <limits>

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
                return payload.visit([](const auto &data) { return encodeList(data.get()); });
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
    {
        auto result = Array::empty(*expected.dtype, expected.rank);
        result.visit(
            [&](const auto &data)
            {
                using Vector = std::remove_reference_t<decltype(data.get())>;
                data.get()   = decodeList<Vector>(value, path);
            });
        return result;
    }
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
