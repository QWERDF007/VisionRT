#include "python/PythonImages.h"

#include <pybind11/numpy.h>

#include <limits>
#include <unordered_set>

namespace visionrt::python {
namespace py = pybind11;

namespace {

constexpr const char *capsuleName = "visionrt.image.storage";

struct BufferLease
{
    Py_buffer view{};
    bool      active = false;

    BufferLease() = default;
    explicit BufferLease(Py_buffer b) : view(b), active(true) {}
    ~BufferLease()
    {
        if (active)
        {
            py::gil_scoped_acquire gil;
            PyBuffer_Release(&view);
            active = false;
        }
    }
    BufferLease(const BufferLease &) = delete;
    BufferLease &operator=(const BufferLease &) = delete;
    BufferLease(BufferLease &&other) noexcept : view(other.view), active(other.active)
    {
        other.active = false;
    }
    BufferLease &operator=(BufferLease &&other) noexcept
    {
        if (this != &other)
        {
            if (active)
            {
                py::gil_scoped_acquire gil;
                PyBuffer_Release(&view);
            }
            view = other.view;
            active = other.active;
            other.active = false;
        }
        return *this;
    }
};

struct ImageStorage
{
    Owner                       owner;
    cv::Mat                     matrix;
    const void                 *begin = nullptr;
    std::size_t                 bytes = 0;
    std::shared_ptr<BufferLease> lease;
};

using Storage = std::shared_ptr<ImageStorage>;

class StorageAllocator final : public cv::MatAllocator
{
public:
    cv::UMatData *allocate(int dimensions, const int *sizes, int type, void *data, std::size_t *steps,
                           cv::AccessFlag access, cv::UMatUsageFlags usage) const override
    {
        return cv::Mat::getStdAllocator()->allocate(dimensions, sizes, type, data, steps, access, usage);
    }

    bool allocate(cv::UMatData *data, cv::AccessFlag access, cv::UMatUsageFlags usage) const override
    {
        return cv::Mat::getStdAllocator()->allocate(data, access, usage);
    }

    void deallocate(cv::UMatData *data) const override
    {
        delete static_cast<Storage *>(data->userdata);
        delete data;
    }
};

const StorageAllocator allocator;

py::dtype imageDtype(int depth)
{
    switch (depth)
    {
    case CV_8U:
        return py::dtype::of<std::uint8_t>();
    case CV_8S:
        return py::dtype::of<std::int8_t>();
    case CV_16U:
        return py::dtype::of<std::uint16_t>();
    case CV_16S:
        return py::dtype::of<std::int16_t>();
    case CV_32S:
        return py::dtype::of<std::int32_t>();
    case CV_32F:
        return py::dtype::of<float>();
    case CV_64F:
        return py::dtype::of<double>();
    case CV_16F:
        return py::dtype("float16");
    }
    throw ContractError("Image 的 OpenCV 像素深度不受支持");
}

std::size_t extent(std::size_t rows, std::size_t rowStep, std::size_t rowBytes)
{
    if (rows == 0 || rowBytes == 0 || rowStep < rowBytes
        || rows - 1 > (static_cast<std::size_t>(std::numeric_limits<py::ssize_t>::max()) - rowBytes) / rowStep)
        throw ContractError("图像行步幅超出地址范围");
    return (rows - 1) * rowStep + rowBytes;
}

Storage backingStorage(const py::array &array, std::size_t bytes, const PythonStorage &objects)
{
    py::object                     base = array;
    Storage                        storage;
    std::unordered_set<PyObject *> visited;
    for (;;)
    {
        if (!visited.insert(base.ptr()).second)
            throw ContractError("NumPy 底层存储所有权链存在循环");
        if (PyCapsule_IsValid(base.ptr(), capsuleName))
        {
            storage = *static_cast<Storage *>(PyCapsule_GetPointer(base.ptr(), capsuleName));
            break;
        }
        if (py::isinstance<py::array>(base))
        {
            const auto owner = py::reinterpret_borrow<py::array>(base);
            if (owner.owndata())
            {
                if (owner.nbytes() <= 0)
                    throw ContractError("NumPy 底层存储必须非空");
                storage        = std::make_shared<ImageStorage>();
                storage->begin = owner.data();
                storage->bytes = static_cast<std::size_t>(owner.nbytes());
                break;
            }
        }
        if (py::hasattr(base, "base"))
        {
            auto next = base.attr("base");
            if (!next.is_none())
            {
                base = std::move(next);
                continue;
            }
        }
        if (PyMemoryView_Check(base.ptr()))
        {
            auto next = base.attr("obj");
            if (!next.is_none())
            {
                base = std::move(next);
                continue;
            }
        }
        if (PyObject_CheckBuffer(base.ptr()))
        {
            Py_buffer rawBuffer{};
            if (PyObject_GetBuffer(base.ptr(), &rawBuffer, PyBUF_SIMPLE) != 0)
                throw py::error_already_set();
            BufferLease guard(rawBuffer);
            const auto begin  = guard.view.buf;
            const auto length = guard.view.len;
            if (length <= 0)
                throw ContractError("NumPy 底层存储必须非空");
            auto lease     = std::make_shared<BufferLease>(std::move(guard));
            storage        = std::make_shared<ImageStorage>();
            storage->begin = begin;
            storage->bytes = static_cast<std::size_t>(length);
            storage->lease = std::move(lease);
            break;
        }
        throw ContractError("NumPy 输出没有可验证的底层存储所有者");
    }
    const auto begin   = reinterpret_cast<std::uintptr_t>(storage->begin);
    const auto address = reinterpret_cast<std::uintptr_t>(array.data());
    if (storage->bytes > std::numeric_limits<std::uintptr_t>::max() - begin || address < begin
        || address > begin + storage->bytes || bytes > begin + storage->bytes - address)
        throw ContractError("NumPy 形状或步幅超出底层存储范围");
    if (!storage->owner)
        storage->owner = objects.retain(array);
    return storage;
}

cv::Mat imageHeader(py::handle value, const PythonStorage &objects)
{
    if (!py::isinstance<py::array>(value))
        throw ContractError("Image 元素需要 NumPy ndarray，不接受隐式转换");
    const auto array = py::reinterpret_borrow<py::array>(value);
    if (array.ndim() != 2 && array.ndim() != 3)
        throw ContractError("Image 形状必须为 HxW 或 HxWxC");
    if (!array.dtype().attr("isnative").cast<bool>())
        throw ContractError("Image dtype 必须使用本机字节序");
    int depth = -1;
    for (const int candidate : {CV_8U, CV_8S, CV_16U, CV_16S, CV_32S, CV_32F, CV_64F, CV_16F})
        if (array.dtype().equal(imageDtype(candidate)))
            depth = candidate;
    if (depth < 0)
        throw ContractError("Image 像素 dtype 不受支持，不执行隐式 astype");
    const auto S = static_cast<std::size_t>(array.itemsize());
    const auto addr = reinterpret_cast<std::uintptr_t>(array.data());
    if (addr % S != 0)
        throw ContractError("Image 像素地址未按标量大小对齐");

    const auto rows     = array.shape(0);
    const auto columns  = array.shape(1);
    const auto channels = array.ndim() == 3 ? array.shape(2) : 1;
    if (rows <= 0 || columns <= 0 || rows > std::numeric_limits<int>::max() || columns > std::numeric_limits<int>::max()
        || channels <= 0 || channels > CV_CN_MAX)
        throw ContractError("Image 尺寸必须在 OpenCV 有效范围内");
    if (!array.writeable())
        throw ContractError("Image 的实际 NumPy 存储不可写；请由工具显式构造可写数组");

    const auto C = static_cast<std::size_t>(channels);
    const auto P = C * S;
    const auto W = static_cast<std::size_t>(columns);
    const auto R = W * P;
    const auto H = static_cast<std::size_t>(rows);

    if (array.ndim() == 3 && C > 1)
    {
        if (array.strides(2) != static_cast<py::ssize_t>(S))
            throw ContractError("Image 通道必须连续");
    }

    if (W > 1)
    {
        if (array.strides(1) != static_cast<py::ssize_t>(P))
            throw ContractError("Image 列必须按像素大小连续");
    }

    std::size_t effectiveRowStep = R;
    if (H > 1)
    {
        const auto s0 = array.strides(0);
        if (s0 <= 0 || static_cast<std::size_t>(s0) < R || (static_cast<std::size_t>(s0) % S != 0))
            throw ContractError("Image 布局无法表示为 cv::Mat；请由工具显式使用 np.ascontiguousarray/np.array(copy=True)");
        effectiveRowStep = static_cast<std::size_t>(s0);
    }
    else
    {
        if (array.strides(0) > 0 && static_cast<std::size_t>(array.strides(0)) >= R
            && (static_cast<std::size_t>(array.strides(0)) % S == 0))
        {
            effectiveRowStep = static_cast<std::size_t>(array.strides(0));
        }
        else
        {
            effectiveRowStep = R;
        }
    }

    const auto bytes   = extent(H, effectiveRowStep, R);
    auto       storage = backingStorage(array, bytes, objects);
    cv::Mat    result(static_cast<int>(rows), static_cast<int>(columns), CV_MAKETYPE(depth, static_cast<int>(channels)),
                      const_cast<void *>(array.data()), effectiveRowStep);
    auto       retained = std::make_unique<Storage>(std::move(storage));
    auto       data     = std::make_unique<cv::UMatData>(&allocator);
    data->data = data->origdata = result.data;
    data->size                  = bytes;
    data->flags |= cv::UMatData::USER_ALLOCATED;
    data->userdata = retained.get();
    result.u       = data.get();
    result.addref();
    retained.release();
    data.release();
    return result;
}

}

py::list imageViews(const DataRef<Image> &images)
{
    py::list result;
    for (const auto &image : images.get())
    {
        Storage storage;
        if (image.u && image.u->currAllocator == &allocator)
            storage = *static_cast<const Storage *>(image.u->userdata);
        else
        {
            storage         = std::make_shared<ImageStorage>();
            storage->owner  = images.owner();
            storage->matrix = image;
            storage->begin  = image.data;
            storage->bytes = extent(image.rows, image.step[0], static_cast<std::size_t>(image.cols) * image.elemSize());
        }
        auto        retained = std::make_unique<Storage>(std::move(storage));
        py::capsule capsule(retained.get(), capsuleName, [](void *pointer) { delete static_cast<Storage *>(pointer); });
        retained.release();
        std::vector<py::ssize_t> shape{image.rows, image.cols};
        std::vector<py::ssize_t> strides{static_cast<py::ssize_t>(image.step[0]),
                                         static_cast<py::ssize_t>(image.elemSize())};
        if (image.channels() != 1)
        {
            shape.push_back(image.channels());
            strides.push_back(static_cast<py::ssize_t>(image.elemSize1()));
        }
        result.append(py::array(imageDtype(image.depth()), shape, strides, image.data, capsule));
    }
    return result;
}

DataRef<Image> imageHeaders(py::handle value, const PythonStorage &storage, const std::string &path)
{
    if (!PyList_CheckExact(value.ptr()))
        throw ContractError(path + " 需要 Image list[np.ndarray]");
    const auto list = py::reinterpret_borrow<py::list>(value);
    Image      result;
    result.reserve(list.size());
    for (std::size_t index = 0; index < list.size(); ++index)
    {
        try
        {
            result.push_back(imageHeader(list[index], storage));
        }
        catch (const ContractError &error)
        {
            throw ContractError(path + "[" + std::to_string(index) + "]：" + error.what());
        }
    }
    return DataRef<Image>(std::move(result));
}

}
