#include "python/PythonInterop.h"

#include <pybind11/numpy.h>
#include <pybind11/stl.h>

#include <algorithm>
#include <cstring>
#include <limits>

namespace visionrt {
namespace py = pybind11;
namespace {

py::dtype numpyDtype(DType dtype) {
    switch (dtype) {
    case DType::UInt8: return py::dtype::of<std::uint8_t>();
    case DType::Int32: return py::dtype::of<std::int32_t>();
    case DType::Int64: return py::dtype::of<std::int64_t>();
    case DType::Float32: return py::dtype::of<float>();
    case DType::Float64: return py::dtype::of<double>();
    case DType::Bool: return py::dtype::of<bool>();
    }
    throw ContractError("NumericArray 数据类型不受支持");
}

py::dtype imageDtype(int depth) {
    switch (depth) {
    case CV_8U: return py::dtype::of<std::uint8_t>();
    case CV_8S: return py::dtype::of<std::int8_t>();
    case CV_16U: return py::dtype::of<std::uint16_t>();
    case CV_16S: return py::dtype::of<std::int16_t>();
    case CV_32S: return py::dtype::of<std::int32_t>();
    case CV_32F: return py::dtype::of<float>();
    case CV_64F: return py::dtype::of<double>();
    case CV_16F: return py::dtype("float16");
    }
    throw ContractError("ImageBatch 的 OpenCV 深度不受支持");
}

struct RetainedStorage {
    std::shared_ptr<const void> owner;
    const void* begin;
    std::size_t size;
};

constexpr const char* ownerCapsuleName = "visionrt.readonly";

py::capsule ownerCapsule(std::shared_ptr<const void> owner, const void* begin, std::size_t size) {
    auto retained = std::make_unique<RetainedStorage>(RetainedStorage{std::move(owner), begin, size});
    py::capsule capsule(retained.get(), ownerCapsuleName, [](void* pointer) {
        delete static_cast<RetainedStorage*>(pointer);
    });
    retained.release();
    return capsule;
}

py::array readonlyArray(py::dtype dtype, const std::vector<py::ssize_t>& shape,
                        const std::vector<py::ssize_t>& strides, const void* data,
                        std::shared_ptr<const void> owner) {
    std::size_t bytes = static_cast<std::size_t>(dtype.itemsize());
    for (std::size_t axis = 0; axis < shape.size(); ++axis) {
        if (shape[axis] == 0) { bytes = 0; break; }
        const auto count = static_cast<std::size_t>(shape[axis] - 1);
        const auto stride = static_cast<std::size_t>(strides[axis]);
        if (count && stride > (std::numeric_limits<std::size_t>::max() - bytes) / count)
            throw ContractError("输入视图步幅范围超出地址空间");
        bytes += count * stride;
    }
    // A null pointer would make NumPy allocate even for a zero-element view.
    static const std::byte emptyStorage{};
    const void* pointer = data ? data : &emptyStorage;
    auto capsule = ownerCapsule(std::move(owner), pointer, bytes);
    py::array array(dtype, shape, strides, pointer, capsule);
    array.attr("flags").attr("writeable") = false;
    return array;
}

std::vector<py::ssize_t> arrayShape(std::span<const std::int64_t> dimensions) {
    std::vector<py::ssize_t> shape;
    shape.reserve(dimensions.size());
    for (const auto dimension : dimensions) {
        if (dimension < 0 || static_cast<std::uint64_t>(dimension) >
                static_cast<std::uint64_t>(std::numeric_limits<py::ssize_t>::max()))
            throw ContractError("数组维度超出 NumPy 索引范围");
        shape.push_back(static_cast<py::ssize_t>(dimension));
    }
    return shape;
}

py::array numericView(const NumericBuffer& buffer) {
    auto shape = arrayShape(buffer.shape());
    std::vector<py::ssize_t> strides(shape.size());
    auto stride = static_cast<py::ssize_t>(dtypeSize(buffer.dtype()));
    for (std::size_t i = shape.size(); i-- > 0;) {
        strides[i] = stride;
        if (shape[i] != 0 && stride > std::numeric_limits<py::ssize_t>::max() / shape[i])
            throw ContractError("NumericArray 步幅超出 NumPy 索引范围");
        stride *= shape[i];
    }
    return readonlyArray(numpyDtype(buffer.dtype()), shape, strides, buffer.data(), buffer.owner());
}

py::array stringView(const StringBuffer& buffer) {
    py::list strings;
    for (const auto& string : buffer.values()) strings.append(py::str(string));
    py::object array = py::module_::import("numpy").attr("array")(strings, py::arg("dtype") = "U");
    py::tuple shape(buffer.shape().size());
    for (std::size_t i = 0; i < buffer.shape().size(); ++i) shape[i] = py::int_(buffer.shape()[i]);
    auto result = array.attr("reshape")(shape).cast<py::array>();
    result.attr("flags").attr("writeable") = false;
    return result;
}

py::array requireArray(py::handle value, int rank, bool contiguous) {
    if (!py::isinstance<py::array>(value))
        throw ContractError("需要 NumPy ndarray，不接受隐式转换");
    auto array = py::reinterpret_borrow<py::array>(value);
    if (array.ndim() != rank)
        throw ContractError("NumPy 维数不匹配：期望 " + std::to_string(rank) + "，实际 "
                            + std::to_string(array.ndim()));
    if (contiguous && !(array.flags() & py::array::c_style))
        throw ContractError("数组输出必须为 C 连续布局");
    if (!array.dtype().attr("isnative").cast<bool>())
        throw ContractError("NumPy dtype 必须使用本机字节序");
    return array;
}

std::vector<std::int64_t> checkedShape(const py::array& array) {
    std::vector<std::int64_t> shape;
    shape.reserve(static_cast<std::size_t>(array.ndim()));
    for (py::ssize_t i = 0; i < array.ndim(); ++i) shape.push_back(array.shape(i));
    if (array.itemsize() <= 0 ||
        checkedByteCount(shape, static_cast<std::size_t>(array.itemsize())) >
            static_cast<std::size_t>(std::numeric_limits<py::ssize_t>::max()))
        throw ContractError("NumPy 形状的字节数超出缓冲区范围");
    return shape;
}

void validateStorage(const py::array& array, py::ssize_t low, py::ssize_t high) {
    if (low == high) return;
    py::object base = array;
    const void* origin = nullptr;
    std::size_t bytes = 0;
    std::vector<PyObject*> visited;
    for (;;) {
        if (std::find(visited.begin(), visited.end(), base.ptr()) != visited.end())
            throw ContractError("NumPy 缓冲区所有权链存在循环");
        visited.push_back(base.ptr());
        if (PyCapsule_IsValid(base.ptr(), ownerCapsuleName)) {
            const auto* retained = static_cast<const RetainedStorage*>(
                PyCapsule_GetPointer(base.ptr(), ownerCapsuleName));
            origin = retained->begin;
            bytes = retained->size;
            break;
        }
        if (py::isinstance<py::array>(base)) {
            const auto owner = py::reinterpret_borrow<py::array>(base);
            if (owner.owndata()) {
                origin = owner.data();
                bytes = checkedByteCount(checkedShape(owner), static_cast<std::size_t>(owner.itemsize()));
                break;
            }
        }
        if (py::hasattr(base, "base")) {
            py::object next = base.attr("base");
            if (!next.is_none()) { base = std::move(next); continue; }
        }
        if (PyMemoryView_Check(base.ptr())) {
            py::object next = base.attr("obj");
            if (!next.is_none()) { base = std::move(next); continue; }
        }
        if (PyObject_CheckBuffer(base.ptr())) {
            Py_buffer buffer{};
            if (PyObject_GetBuffer(base.ptr(), &buffer, PyBUF_SIMPLE) != 0)
                throw py::error_already_set();
            origin = buffer.buf;
            bytes = buffer.len < 0 ? 0 : static_cast<std::size_t>(buffer.len);
            PyBuffer_Release(&buffer);
            break;
        }
        throw ContractError("NumPy 输出没有可验证的底层缓冲区所有者");
    }
    const auto address = reinterpret_cast<std::uintptr_t>(array.data());
    const auto start = reinterpret_cast<std::uintptr_t>(origin);
    if (low == std::numeric_limits<py::ssize_t>::min() || high < 0 ||
        address < static_cast<std::uintptr_t>(-low) ||
        static_cast<std::uintptr_t>(high) > std::numeric_limits<std::uintptr_t>::max() - address ||
        bytes > std::numeric_limits<std::uintptr_t>::max() - start ||
        address - static_cast<std::uintptr_t>(-low) < start ||
        address + static_cast<std::uintptr_t>(high) > start + bytes)
        throw ContractError("NumPy 形状或步幅超出底层缓冲区范围");
}

#if defined(VISIONRT_TEST_PROBES)
std::atomic<std::size_t> g_leafImageCopyCount{0};
std::atomic<std::size_t> g_leafNumericCopyCount{0};
#endif

NumericBuffer numericCopy(py::handle value, DType dtype, int rank) {
    auto array = requireArray(value, rank, true);
    if (!array.dtype().equal(numpyDtype(dtype)))
        throw ContractError("NumericArray 数据类型不匹配：实际 " + py::str(array.dtype()).cast<std::string>());
    auto shape = checkedShape(array);
    const auto size = checkedByteCount(shape, dtypeSize(dtype));
    validateStorage(array, 0, static_cast<py::ssize_t>(size));
    auto buffer = NumericBuffer::copy(dtype, std::move(shape),
        {static_cast<const std::byte*>(array.data()), size});
#if defined(VISIONRT_TEST_PROBES)
    ++g_leafNumericCopyCount;
#endif
    return buffer;
}

StringBuffer stringCopy(py::handle value, int rank) {
    auto array = requireArray(value, rank, true);
    if (array.dtype().kind() != 'U')
        throw ContractError("StringArray 需要 Unicode dtype，不接受 object 或 bytes 数组");
    auto shape = checkedShape(array);
    validateStorage(array, 0, static_cast<py::ssize_t>(
        checkedByteCount(shape, static_cast<std::size_t>(array.itemsize()))));
    std::vector<std::string> strings;
    strings.reserve(checkedElementCount(shape));
    for (auto item : array.attr("flat")) {
        if (!PyUnicode_Check(item.ptr()))
            throw ContractError("StringArray 元素必须为 Unicode");
        strings.push_back(py::cast<std::string>(item));
    }
    return StringBuffer(std::move(shape), std::move(strings));
}

py::list requireList(py::handle value, const char* description) {
    if (!py::isinstance<py::list>(value))
        throw ContractError(std::string(description) + " 需要 list");
    return py::reinterpret_borrow<py::list>(value);
}

OwnedImage imageCopy(py::handle value) {
    if (!py::isinstance<py::array>(value))
        throw ContractError("ImageBatch 元素需要 NumPy ndarray");
    auto array = py::reinterpret_borrow<py::array>(value);
    if (array.ndim() != 2 && array.ndim() != 3)
        throw ContractError("ImageBatch 图像形状必须为 HxW 或 HxWxC");
    array = requireArray(value, static_cast<int>(array.ndim()), false);
    auto shape = checkedShape(array);
    const auto channels = array.ndim() == 3 ? shape[2] : 1;
    if (shape[0] <= 0 || shape[1] <= 0 || shape[0] > std::numeric_limits<int>::max() ||
        shape[1] > std::numeric_limits<int>::max() || channels <= 0 || channels > CV_CN_MAX ||
        (array.ndim() == 3 && channels == 1))
        throw ContractError("ImageBatch 的 OpenCV 维度必须为正数，单通道图像必须使用 HxW");
    int depth = -1;
    for (const int candidate : {CV_8U, CV_8S, CV_16U, CV_16S, CV_32S, CV_32F, CV_64F, CV_16F})
        if (array.dtype().equal(imageDtype(candidate))) { depth = candidate; break; }
    if (depth < 0)
        throw ContractError("ImageBatch 数据类型不受 OpenCV 支持：" + py::str(array.dtype()).cast<std::string>());
    // Bound every signed strided offset before pointer arithmetic, including
    // reversed and transposed image outputs.
    py::ssize_t low = 0;
    py::ssize_t high = array.itemsize();
    for (py::ssize_t axis = 0; axis < array.ndim(); ++axis) {
        const auto count = array.shape(axis) - 1;
        const auto stride = array.strides(axis);
        if (stride == std::numeric_limits<py::ssize_t>::min() ||
            (count && (stride < 0 ? -stride : stride) > std::numeric_limits<py::ssize_t>::max() / count))
            throw ContractError("ImageBatch 步幅范围超出地址空间");
        const auto extent = count * stride;
        if (extent >= 0) {
            if (high > std::numeric_limits<py::ssize_t>::max() - extent)
                throw ContractError("ImageBatch 步幅范围超出地址空间");
            high += extent;
        } else {
            if (low < std::numeric_limits<py::ssize_t>::min() - extent)
                throw ContractError("ImageBatch 步幅范围超出地址空间");
            low += extent;
        }
    }
    validateStorage(array, low, high);
    cv::Mat destination(static_cast<int>(shape[0]), static_cast<int>(shape[1]),
                        CV_MAKETYPE(depth, static_cast<int>(channels)));
    const auto* source = static_cast<const std::byte*>(array.data());
    const auto elementSize = static_cast<std::size_t>(array.itemsize());
    const auto pixelSize = elementSize * static_cast<std::size_t>(channels);
    const auto channelStride = array.ndim() == 3 ? array.strides(2) : array.itemsize();
    for (int row = 0; row < destination.rows; ++row) {
        auto* output = destination.ptr(row);
        const auto* input = source + row * array.strides(0);
        if (array.strides(1) == static_cast<py::ssize_t>(pixelSize) && channelStride == array.itemsize()) {
            std::memcpy(output, input, static_cast<std::size_t>(destination.cols) * pixelSize);
        } else {
            for (int column = 0; column < destination.cols; ++column)
                for (std::int64_t channel = 0; channel < channels; ++channel)
                    std::memcpy(output + static_cast<std::size_t>(column) * pixelSize + channel * elementSize,
                                input + column * array.strides(1) + channel * channelStride, elementSize);
        }
    }
#if defined(VISIONRT_TEST_PROBES)
    ++g_leafImageCopyCount;
#endif
    return OwnedImage::take(std::move(destination));
}

Roi roiCopy(py::handle value) {
    auto array = requireArray(value, 1, true);
    if (!array.dtype().equal(py::dtype::of<float>()))
        throw ContractError("ROI 数据类型必须为 float32");
    auto shape = checkedShape(array);
    validateStorage(array, 0, static_cast<py::ssize_t>(checkedByteCount(shape, sizeof(float))));
    return Roi::decode({static_cast<const float*>(array.data()), checkedElementCount(shape)});
}

} // namespace

py::object toPythonReadonly(const PortValue& value) {
    return std::visit([](const auto& item) -> py::object {
        using T = std::decay_t<decltype(item)>;
        if constexpr (std::is_same_v<T, Missing>) return py::none();
        else if constexpr (std::is_same_v<T, std::int64_t>) return py::int_(item);
        else if constexpr (std::is_same_v<T, double>) return py::float_(item);
        else if constexpr (std::is_same_v<T, bool>) return py::bool_(item);
        else if constexpr (std::is_same_v<T, Utf8>) return py::str(item.str());
        else if constexpr (std::is_same_v<T, ImageBatch>) {
            py::list images;
            for (const auto& image : item.images()) {
                const auto elementSize = CV_ELEM_SIZE1(image.type());
                std::vector<py::ssize_t> shape{image.rows(), image.cols()};
                std::vector<py::ssize_t> strides{static_cast<py::ssize_t>(image.rowStride()),
                                               static_cast<py::ssize_t>(CV_ELEM_SIZE(image.type()))};
                if (image.channels() != 1) {
                    shape.push_back(image.channels());
                    strides.push_back(static_cast<py::ssize_t>(elementSize));
                }
                images.append(readonlyArray(imageDtype(CV_MAT_DEPTH(image.type())), shape, strides,
                                            image.data(), image.owner()));
            }
            return images;
        } else if constexpr (std::is_same_v<T, NumericArray> || std::is_same_v<T, StringArray>) {
            const auto view = [](const auto& buffer) -> py::array {
                if constexpr (std::is_same_v<T, NumericArray>) return numericView(buffer);
                else return stringView(buffer);
            };
            if (item.layout() == Layout::Dense) return view(item.buffers()[0]);
            py::list buffers;
            for (const auto& buffer : item.buffers()) buffers.append(view(buffer));
            return buffers;
        } else if constexpr (std::is_same_v<T, Roi>) {
            validateValue({Type::Rect}, item);
            return readonlyArray(py::dtype::of<float>(),
                {static_cast<py::ssize_t>(item.encoded().size())}, {sizeof(float)},
                item.encoded().data(), item.owner());
        } else {
            py::list images;
            for (const auto& image : item.images()) {
                py::list rois;
                for (const auto& roi : image)
                    rois.append(readonlyArray(py::dtype::of<float>(),
                        {static_cast<py::ssize_t>(roi.encoded().size())}, {sizeof(float)},
                        roi.encoded().data(), roi.owner()));
                images.append(std::move(rois));
            }
            return images;
        }
    }, value);
}

PortValue fromPython(const TypeSpec& expected, py::handle value) {
    validateTypeSpec(expected);
    if (value.is_none()) return Missing{};
    switch (expected.type) {
    case Type::Int:
        if (!PyLong_Check(value.ptr()) || PyBool_Check(value.ptr()))
            throw ContractError("Int 需要 Python int，不接受 bool 或隐式转换");
        return py::cast<std::int64_t>(value);
    case Type::Float:
        if (!PyFloat_Check(value.ptr()))
            throw ContractError("Float 需要 Python float，不接受 int");
        return py::cast<double>(value);
    case Type::Bool:
        if (!PyBool_Check(value.ptr()))
            throw ContractError("Bool 需要 Python bool");
        return py::cast<bool>(value);
    case Type::String:
        if (!PyUnicode_Check(value.ptr()))
            throw ContractError("String 需要 Unicode str，不接受 bytes");
        return Utf8(py::cast<std::string>(value));
    case Type::ImageBatch: {
        std::vector<OwnedImage> images;
        for (auto image : requireList(value, "ImageBatch")) images.push_back(imageCopy(image));
        return ImageBatch(std::move(images));
    }
    case Type::NumericArray: {
        std::vector<NumericBuffer> buffers;
        if (expected.layout == Layout::Dense) buffers.push_back(numericCopy(value, *expected.dtype, expected.rank));
        else for (auto buffer : requireList(value, "Batch-ragged NumericArray"))
            buffers.push_back(numericCopy(buffer, *expected.dtype, expected.rank - 1));
        return NumericArray(*expected.dtype, expected.rank, expected.layout, std::move(buffers));
    }
    case Type::StringArray: {
        std::vector<StringBuffer> buffers;
        if (expected.layout == Layout::Dense) buffers.push_back(stringCopy(value, expected.rank));
        else for (auto buffer : requireList(value, "Batch-ragged StringArray"))
            buffers.push_back(stringCopy(buffer, expected.rank - 1));
        return StringArray(expected.rank, expected.layout, std::move(buffers));
    }
    case Type::Rect: {
        PortValue rectangle = roiCopy(value);
        validateValue(expected, rectangle);
        return rectangle;
    }
    case Type::RoiBatch: {
        std::vector<std::vector<Roi>> images;
        for (auto image : requireList(value, "RoiBatch")) {
            std::vector<Roi> rois;
            for (auto roiItem : requireList(image, "RoiBatch image")) {
                rois.push_back(roiCopy(roiItem));
            }
            images.push_back(std::move(rois));
        }
        return RoiBatch(std::move(images));
    }
    }
    throw ContractError("TypeSpec 不受支持");
}

#if defined(VISIONRT_TEST_PROBES)
std::size_t pythonLeafImageCopyCount() noexcept {
    return g_leafImageCopyCount.load();
}

void resetPythonLeafImageCopyCount() noexcept {
    g_leafImageCopyCount.store(0);
}

std::size_t pythonLeafNumericCopyCount() noexcept {
    return g_leafNumericCopyCount.load();
}

void resetPythonLeafNumericCopyCount() noexcept {
    g_leafNumericCopyCount.store(0);
}
#endif

} // namespace visionrt
