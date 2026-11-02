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
    // An explicit order only applies to a layout it can describe. A declared BGR output that really
    // carries a grey or four-channel image therefore falls back to per-image inference instead of
    // being rejected before display.
    const auto expectedChannels = [](const QString &order)
    {
        if (order == QStringLiteral("Gray"))
            return 1;
        if (order == QStringLiteral("BGR") || order == QStringLiteral("RGB"))
            return 3;
        if (order == QStringLiteral("BGRA") || order == QStringLiteral("RGBA"))
            return 4;
        return 0;
    };
    if (expectedChannels(requested) == channels)
        return requested;
    if (channels == 1)
        return QStringLiteral("Gray");
    if (channels == 3)
        return QStringLiteral("BGR");
    if (channels == 4)
        return QStringLiteral("BGRA");
    return {};
}

QImage displayImage(const cv::Mat &image, QImage::Format format)
{
    const QImage source(image.data, image.cols, image.rows, static_cast<qsizetype>(image.step[0]), format);
    auto         result = source.copy();
    if (result.isNull())
        throw std::bad_alloc();
    return result;
}

// 255 marks a sample that participates in the colour range and the display; only floating depths can
// produce a non-finite one, so integer planes never allocate a mask.
cv::Mat finiteMask(const cv::Mat &plane)
{
    return cv::abs(plane) < std::numeric_limits<double>::max();
}

double alphaUnitScale(int depth)
{
    switch (depth)
    {
    case CV_8U:
        return 1.0 / 255.0;
    case CV_8S:
        return 1.0 / 127.0;
    case CV_16U:
        return 1.0 / 65535.0;
    case CV_16S:
        return 1.0 / 32767.0;
    case CV_32S:
        return 1.0 / 2147483647.0;
    default:
        // Floating alpha already carries the unit range.
        return 1.0;
    }
}

// Non-8-bit display: one pass of OpenCV primitives over each plane instead of a per-pixel loop.
// Colour extremes ignore non-finite components while a pixel with any non-finite channel (alpha
// included) still renders as opaque black.
DisplayFrame normalize(const cv::Mat &source, const QString &order)
{
    const int  channels  = source.channels();
    const bool hasAlpha  = channels == 4;
    const bool reverse   = order == "BGR" || order == "BGRA";
    const int  depth     = CV_MAT_DEPTH(source.type());
    const bool floats    = depth == CV_32F || depth == CV_64F || depth == CV_16F;

    // OpenCV has no float16 range or comparison primitives; widening once keeps every later plane a
    // supported depth without changing any value.
    cv::Mat        widened;
    if (depth == CV_16F)
        source.convertTo(widened, CV_MAKETYPE(CV_32F, channels));
    const cv::Mat &matrix = widened.empty() ? source : widened;

    std::vector<cv::Mat> colour;
    cv::Mat              alpha;
    if (channels == 1)
    {
        colour.assign(3, matrix);
    }
    else
    {
        colour.resize(3);
        for (int index = 0; index < 3; ++index)
            cv::extractChannel(matrix, colour[reverse ? 2 - index : index], index);
        if (hasAlpha)
            cv::extractChannel(matrix, alpha, 3);
    }

    std::vector<cv::Mat> planes = channels == 1 ? std::vector<cv::Mat>{matrix} : colour;
    double               minimum = std::numeric_limits<double>::infinity();
    double               maximum = -minimum;
    cv::Mat              pixelFinite;
    for (const auto &plane : planes)
    {
        if (!floats)
        {
            double low  = 0;
            double high = 0;
            cv::minMaxIdx(plane, &low, &high);
            minimum = std::min(minimum, low);
            maximum = std::max(maximum, high);
            continue;
        }
        const cv::Mat finite = finiteMask(plane);
        if (pixelFinite.empty())
            pixelFinite = finite;
        else
            cv::bitwise_and(pixelFinite, finite, pixelFinite);
        if (cv::countNonZero(finite) == 0)
            continue;
        double low  = 0;
        double high = 0;
        cv::minMaxIdx(plane, &low, &high, nullptr, nullptr, finite);
        minimum = std::min(minimum, low);
        maximum = std::max(maximum, high);
    }
    if (hasAlpha && floats)
    {
        const cv::Mat finite = finiteMask(alpha);
        if (pixelFinite.empty())
            pixelFinite = finite;
        else
            cv::bitwise_and(pixelFinite, finite, pixelFinite);
    }

    // A constant (or entirely non-finite) colour range has no fraction to spread; 128 is the
    // established midpoint and every non-finite pixel is overwritten with black below.
    const bool   constant = !(maximum > minimum);
    const double scale    = constant ? 0.0 : 255.0 / (maximum - minimum);
    const double offset   = constant ? 0.0 : -minimum * scale;
    const auto   quantize = [&](const cv::Mat &plane)
    {
        if (constant)
            return cv::Mat(plane.size(), CV_8U, cv::Scalar(128));
        cv::Mat result;
        plane.convertTo(result, CV_8U, scale, offset);
        return result;
    };
    std::vector<cv::Mat> display;
    display.reserve(4);
    if (channels == 1)
    {
        const cv::Mat grey = quantize(matrix);
        display.assign(3, grey);
    }
    else
    {
        for (const auto &plane : colour)
            display.push_back(quantize(plane));
    }
    if (hasAlpha)
    {
        cv::Mat unit;
        alpha.convertTo(unit, CV_32F, alphaUnitScale(CV_MAT_DEPTH(matrix.type())));
        cv::max(unit, 0.0, unit);
        if (floats)
            cv::min(unit, 1.0, unit);
        cv::Mat opaque;
        unit.convertTo(opaque, CV_8U, 255.0);
        display.push_back(opaque);
    }
    else
    {
        display.push_back(cv::Mat(matrix.size(), CV_8U, cv::Scalar(255)));
    }

    DisplayFrame result;
    result.image = QImage(source.cols, source.rows, QImage::Format_RGBA8888);
    if (result.image.isNull())
        throw std::bad_alloc();
    // Merge straight into the QImage storage: RGBA8888 is interleaved R,G,B,A with four bytes per
    // pixel, exactly the memory layout cv::merge produces.
    cv::Mat target(result.image.height(), result.image.width(), CV_8UC4, result.image.bits(),
                   static_cast<std::size_t>(result.image.bytesPerLine()));
    cv::merge(display, target);
    if (!pixelFinite.empty())
    {
        cv::Mat nonFinite;
        cv::bitwise_not(pixelFinite, nonFinite);
        if (cv::countNonZero(nonFinite) > 0)
        {
            target.setTo(cv::Scalar(0, 0, 0, 255), nonFinite);
            result.warning = QStringLiteral("非有限像素显示为黑色。");
        }
    }
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

DisplayFrame DisplayConverter::convert(const cv::Mat &image, const QString &colorOrder)
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
            result.image = displayImage(image, QImage::Format_Grayscale8);
        else if (order == "BGR")
            result.image = displayImage(image, QImage::Format_BGR888);
        else if (order == "RGB")
            result.image = displayImage(image, QImage::Format_RGB888);
        else if (order == "RGBA")
            result.image = displayImage(image, QImage::Format_RGBA8888);
        else
        {
#if Q_BYTE_ORDER == Q_LITTLE_ENDIAN
            result.image = displayImage(image, QImage::Format_ARGB32);
#else
            result.image = displayImage(image, QImage::Format_RGBA8888).rgbSwapped();
#endif
        }
        break;
    case CV_8S:
    case CV_16U:
    case CV_16S:
    case CV_32S:
    case CV_32F:
    case CV_64F:
    case CV_16F:
        return normalize(image, order);
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

void DisplayConverter::requestBatch(DataRef<Image> images, QString colorOrder, quint64 requestId, quint64 generation)
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
    activeSerial_ = request.serial;
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
            reading_ = true;
        }
        const auto                serial = request->serial;
        std::vector<DisplayFrame> frames;
        frames.reserve(request->images.get().size());
        for (std::size_t i = 0; i < request->images.get().size(); ++i)
        {
            if (stop.stop_requested())
                break;
            DisplayFrame frame;
            const auto  &image    = request->images.get()[i];
            std::size_t  previous = 0;
            for (; previous < i; ++previous)
            {
                const auto &candidate = request->images.get()[previous];
                if (image.data == candidate.data && image.rows == candidate.rows && image.cols == candidate.cols
                    && image.type() == candidate.type() && image.step[0] == candidate.step[0])
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
        request.reset();
        {
            std::lock_guard lock(mutex_);
            reading_ = false;
        }
        changed_.notify_all();
        if (stop.stop_requested())
            return;
        // Do not begin another conversion until the GUI consumes this one event.
        QMetaObject::invokeMethod(
            this, [this, frames = std::move(frames), serial]() mutable { complete(std::move(frames), serial); },
            Qt::QueuedConnection);
    }
}

void DisplayConverter::complete(std::vector<DisplayFrame> frames, quint64 serial)
{
    if (serial != activeSerial_)
        return;
    busy_ = false;
    if (serial == serial_ && hasRequest_)
    {
        emit batchConverted(frames);
    }
    // A direct signal receiver may have submitted the next request already.
    if (!busy_ && pending_)
    {
        auto next = std::move(*pending_);
        pending_.reset();
        dispatch(std::move(next));
        return;
    }
    if (!busy_)
        emit readersIdle();
}

void DisplayConverter::waitForReads()
{
    Q_ASSERT(QThread::currentThread() == thread());
    invalidate(generation_);
    std::unique_lock lock(mutex_);
    work_.reset();
    changed_.wait(lock, [this] { return !reading_; });
    busy_         = false;
    activeSerial_ = 0;
}

} // namespace visionrt
