#include "ui/DisplayConverter.h"

#include <QMetaObject>
#include <QThread>
#include <algorithm>
#include <cmath>
#include <limits>
#include <utility>

namespace visionrt {
namespace {

QString resolvedOrder(int channels, const QString &requested)
{
    if (!requested.isEmpty())
        return requested;
    if (channels == 1)
        return QStringLiteral("Gray");
    if (channels == 3)
        return QStringLiteral("BGR");
    if (channels == 4)
        return QStringLiteral("BGRA");
    return {};
}

QImage retainImage(const OwnedImage &image, QImage::Format format)
{
    auto   owner = std::make_unique<OwnedImage>(image);
    QImage result(
        image.data(), image.cols(), image.rows(), static_cast<qsizetype>(image.rowStride()), format,
        [](void *value) { delete static_cast<OwnedImage *>(value); }, owner.get());
    owner.release(); // QImage's shared data owns the immutable original until conversion/upload ends.
    return result;
}

template<class T>
DisplayFrame normalize(const OwnedImage &source, const QString &order)
{
    DisplayFrame result;
    const int    channels  = source.channels();
    double       minimum   = std::numeric_limits<double>::infinity();
    double       maximum   = -minimum;
    bool         nonfinite = false;
    const auto  &matrix    = source.readOnlyMat();
    for (int y = 0; y < source.rows(); ++y)
    {
        const T          *row   = matrix.ptr<T>(y);
        const std::size_t count = static_cast<std::size_t>(source.cols()) * channels;
        for (std::size_t i = 0; i < count; ++i)
        {
            const double value = static_cast<double>(row[i]);
            if (!std::isfinite(value))
            {
                nonfinite = true;
                continue;
            }
            minimum = std::min(minimum, value);
            maximum = std::max(maximum, value);
        }
    }
    result.image = QImage(source.cols(), source.rows(), QImage::Format_RGBA8888);
    if (result.image.isNull())
        throw std::bad_alloc();
    const bool   reverse = order == "BGR" || order == "BGRA";
    const double range   = maximum - minimum;
    const auto   channel = [&](double value) -> unsigned char
    {
        if (maximum == minimum)
            return 128;
        const double fraction = std::isfinite(range) ? (value - minimum) / range
                                                     : (value / 2 - minimum / 2) / (maximum / 2 - minimum / 2);
        return static_cast<unsigned char>(std::clamp(std::lround(fraction * 255), 0L, 255L));
    };
    for (int y = 0; y < source.rows(); ++y)
    {
        const T       *row         = matrix.ptr<T>(y);
        unsigned char *destination = result.image.scanLine(y);
        for (int x = 0; x < source.cols(); ++x)
        {
            const T *pixel  = row + static_cast<std::size_t>(x) * channels;
            bool     finite = true;
            for (int c = 0; c < channels; ++c) finite = finite && std::isfinite(static_cast<double>(pixel[c]));
            if (!finite)
            {
                destination[0] = destination[1] = destination[2] = 0;
                destination[3]                                   = 255;
            }
            else
            {
                destination[0] = channel(static_cast<double>(pixel[channels == 1 ? 0 : (reverse ? 2 : 0)]));
                destination[1] = channel(static_cast<double>(pixel[channels == 1 ? 0 : 1]));
                destination[2] = channel(static_cast<double>(pixel[channels == 1 ? 0 : (reverse ? 0 : 2)]));
                destination[3] = channels == 4 && maximum != minimum ? channel(static_cast<double>(pixel[3])) : 255;
            }
            destination += 4;
        }
    }
    if (nonfinite)
        result.warning = QStringLiteral("图像包含 NaN/Inf；非有限像素显示为黑色。");
    return result;
}

} // namespace

DisplayConverter::DisplayConverter(QObject *parent)
    : QObject(parent)
    , worker_([this](std::stop_token stop) { run(stop); })
{
}

DisplayConverter::~DisplayConverter()
{
    worker_.request_stop();
    changed_.notify_all();
    worker_.join();
}

DisplayFrame DisplayConverter::convert(const OwnedImage &image, const QString &colorOrder)
{
    DisplayFrame result;
    if (image.empty())
        return result;
    const auto order = resolvedOrder(image.channels(), colorOrder);
    if (!((image.channels() == 1 && order == "Gray") || (image.channels() == 3 && (order == "BGR" || order == "RGB"))
          || (image.channels() == 4 && (order == "BGRA" || order == "RGBA"))))
    {
        result.error
            = QStringLiteral("不支持显示的通道布局或颜色顺序：%1 通道，%2").arg(image.channels()).arg(colorOrder);
        return result;
    }
    switch (CV_MAT_DEPTH(image.type()))
    {
    case CV_8U:
        if (image.channels() == 1)
            result.image = retainImage(image, QImage::Format_Grayscale8);
        else if (order == "BGR")
            result.image = retainImage(image, QImage::Format_BGR888);
        else if (order == "RGB")
            result.image = retainImage(image, QImage::Format_RGB888);
        else if (order == "RGBA")
            result.image = retainImage(image, QImage::Format_RGBA8888);
        else
        {
#if Q_BYTE_ORDER == Q_LITTLE_ENDIAN
            result.image = retainImage(image, QImage::Format_ARGB32);
#else
            result.image = retainImage(image, QImage::Format_RGBA8888).rgbSwapped();
#endif
        }
        break;
    case CV_8S:
        return normalize<signed char>(image, order);
    case CV_16U:
        return normalize<unsigned short>(image, order);
    case CV_16S:
        return normalize<short>(image, order);
    case CV_32S:
        return normalize<int>(image, order);
    case CV_32F:
        return normalize<float>(image, order);
    case CV_64F:
        return normalize<double>(image, order);
    case CV_16F:
        return normalize<cv::float16_t>(image, order);
    default:
        result.error = QStringLiteral("不支持显示的图像像素类型。");
        break;
    }
    return result;
}

void DisplayConverter::invalidate(quint64 generation)
{
    Q_ASSERT(QThread::currentThread() == thread());
    generation_ = generation;
    hasRequest_ = false;
    ++serial_;
    pending_.reset();
}

void DisplayConverter::request(OwnedImage image, QString colorOrder, quint64 requestId, quint64 generation)
{
    std::vector<OwnedImage> images;
    images.push_back(std::move(image));
    requestBatch(std::move(images), std::move(colorOrder), requestId, generation);
}

void DisplayConverter::requestBatch(std::vector<OwnedImage> images, QString colorOrder, quint64 requestId,
                                    quint64 generation)
{
    Q_ASSERT(QThread::currentThread() == thread());
    if (generation < generation_)
        return;
    if (generation != generation_)
        invalidate(generation);
    if (hasRequest_ && requestId <= latestRequestId_)
        return;
    hasRequest_      = true;
    latestRequestId_ = requestId;
    Request request{std::move(images), std::move(colorOrder), requestId, generation, ++serial_};
    if (busy_)
        pending_ = std::move(request);
    else
        dispatch(std::move(request));
}

void DisplayConverter::dispatch(Request request)
{
    busy_ = true;
    {
        std::lock_guard lock(mutex_);
        work_ = std::move(request);
    }
    changed_.notify_one();
}

void DisplayConverter::run(std::stop_token stop)
{
    while (!stop.stop_requested())
    {
        std::optional<Request> request;
        {
            std::unique_lock lock(mutex_);
            if (!changed_.wait(lock, stop, [this] { return work_.has_value(); }))
                return;
            request = std::move(work_);
            work_.reset();
        }
        std::vector<DisplayFrame> frames;
        frames.reserve(request->images.size());
        for (std::size_t i = 0; i < request->images.size(); ++i)
        {
            if (stop.stop_requested())
                return;
            DisplayFrame frame;
            const auto  &image    = request->images[i];
            std::size_t  previous = 0;
            for (; previous < i; ++previous)
            {
                const auto &candidate = request->images[previous];
                if (image.data() == candidate.data() && image.rows() == candidate.rows()
                    && image.cols() == candidate.cols() && image.type() == candidate.type()
                    && image.rowStride() == candidate.rowStride())
                    break;
            }
            if (previous < i)
                frame = frames[previous];
            else
            {
                try
                {
                    frame = convert(image, request->colorOrder);
                }
                catch (const std::exception &error)
                {
                    frame.error = QString::fromUtf8(error.what());
                }
            }
            frame.generation = request->generation;
            frame.requestId  = request->requestId;
            frame.batchIndex = static_cast<int>(i);
            frames.push_back(std::move(frame));
        }
        if (stop.stop_requested())
            return;
        // Do not begin another conversion until the GUI consumes this one event.
        QMetaObject::invokeMethod(
            this, [this, frames = std::move(frames), serial = request->serial]() mutable
            { complete(std::move(frames), serial); }, Qt::QueuedConnection);
    }
}

void DisplayConverter::complete(std::vector<DisplayFrame> frames, quint64 serial)
{
    busy_ = false;
    if (serial == serial_ && hasRequest_)
    {
        emit batchConverted(frames);
        if (serial == serial_ && !frames.empty())
            emit converted(frames.front());
    }
    // A direct signal receiver may have submitted the next request already.
    if (!busy_ && pending_)
    {
        auto next = std::move(*pending_);
        pending_.reset();
        dispatch(std::move(next));
    }
}

} // namespace visionrt
