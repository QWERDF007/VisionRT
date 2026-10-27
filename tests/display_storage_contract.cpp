#include "ui/DisplayConverter.h"

#include <QCoreApplication>
#include <QElapsedTimer>
#include <QThread>
#include <cmath>
#include <future>
#include <iostream>
#include <limits>

using namespace visionrt;

namespace {
void require(bool condition, const char *message)
{
    if (!condition)
        throw std::runtime_error(message);
}

void ownedDisplayPixels()
{
    for (const auto &[channels, order] : std::vector<std::pair<int, QString>>{
             {1, "Gray"},
             {3,  "RGB"},
             {3,  "BGR"},
             {4, "RGBA"},
             {4, "BGRA"}
    })
    {
        cv::Mat    pixels(5, 8, CV_MAKETYPE(CV_8U, channels), cv::Scalar(11, 23, 37, 255));
        const auto view  = pixels(cv::Rect(1, 1, 3, 2));
        const auto frame = DisplayConverter::convert(view, order);
        require(frame.error.isEmpty() && !frame.image.isNull(), "display conversion failed");
        require(frame.image.constBits() != view.data, "display frame borrows mutable business pixels");
        const auto before = frame.image.pixelColor(0, 0);
        require(before.red() == (order.startsWith('B') ? 37 : 11), "display color order changed");
        if (channels > 1)
            require(before.green() == 23 && before.blue() == (order.startsWith('B') ? 11 : 37),
                    "display channels changed");
        pixels.setTo(cv::Scalar(99, 88, 77, 255));
        require(frame.image.pixelColor(0, 0) == before, "later business write changed displayed frame");
        const auto next = DisplayConverter::convert(view, order);
        require(next.image.pixelColor(0, 0) != before, "next read phase did not generate a fresh display frame");
    }
    cv::Mat     floating(1, 5, CV_32FC1);
    const float values[]{-10.f, 0.f, 10.f, std::numeric_limits<float>::quiet_NaN(),
                         std::numeric_limits<float>::infinity()};
    std::copy(std::begin(values), std::end(values), floating.ptr<float>());
    const auto normalized = DisplayConverter::convert(floating);
    require(normalized.image.pixelColor(0, 0).red() == 0 && normalized.image.pixelColor(1, 0).red() == 128
                && normalized.image.pixelColor(2, 0).red() == 255,
            "normalization changed");
    require(!normalized.warning.isEmpty() && normalized.image.pixelColor(3, 0).red() == 0, "nonfinite display changed");
    require(floating.at<float>(0, 0) == -10.f && std::isnan(floating.at<float>(0, 3)),
            "display conversion wrote business pixels");
}

void asynchronousSourcesRetainOwners()
{
    DisplayConverter converter;
    int              released = 0;
    auto             pixels   = std::shared_ptr<std::uint8_t[]>(new std::uint8_t[16]{},
                                                                [&](auto *memory)
                                                                {
                                                      delete[] memory;
                                                      ++released;
                                                  });
    pixels[5]                 = 47;
    DataRef<Image>            source(Image{cv::Mat(4, 4, CV_8UC1, pixels.get())}, {pixels});
    std::vector<DisplayFrame> displayed;
    QObject::connect(&converter, &DisplayConverter::batchConverted, &converter,
                     [&](const auto &frames) { displayed = frames; });
    converter.requestBatch(source, "Gray", 1, 1);
    source = DataRef<Image>(Image{});
    pixels.reset();
    QElapsedTimer timer;
    timer.start();
    while ((displayed.empty() || released == 0) && timer.elapsed() < 5000)
    {
        QCoreApplication::processEvents();
        QThread::msleep(1);
    }
    require(displayed.size() == 1 && displayed[0].image.pixelColor(1, 1).red() == 47,
            "async conversion lost source ownership");
    require(released == 1, "display frame retains business storage after conversion");
}

void drainReleasesSourcesBeforeNextExecution()
{
    DisplayConverter   converter;
    std::promise<void> releasing;
    std::promise<void> permitRelease;
    auto               permission = permitRelease.get_future();
    auto               owner      = std::shared_ptr<int>(new int(1),
                                                         [&](int *value)
                                                         {
                                          releasing.set_value();
                                          permission.wait();
                                          delete value;
                                      });
    DataRef<Image>     source(Image{cv::Mat(2, 3, CV_8UC1, cv::Scalar(19))}, {owner});
    converter.requestBatch(source, "Gray", 1, 1);
    source = DataRef<Image>(Image{});
    owner.reset();
    auto entered = releasing.get_future();
    require(entered.wait_for(std::chrono::seconds(5)) == std::future_status::ready,
            "display worker did not release input");
    auto release = std::async(std::launch::async,
                              [&]
                              {
                                  std::this_thread::sleep_for(std::chrono::milliseconds(30));
                                  permitRelease.set_value();
                              });
    converter.waitForReads();
    release.get();
    require(!converter.isBusy() && !converter.hasPending(), "drain left an active source request");
    std::vector<DisplayFrame> displayed;
    QObject::connect(&converter, &DisplayConverter::batchConverted, &converter,
                     [&](const auto &frames) { displayed = frames; });
    converter.requestBatch(DataRef<Image>(Image{cv::Mat(2, 3, CV_8UC1, cv::Scalar(79))}), "Gray", 2, 1);
    QElapsedTimer timer;
    timer.start();
    while ((displayed.empty() || converter.isBusy()) && timer.elapsed() < 5000)
    {
        QCoreApplication::processEvents();
        QThread::msleep(1);
    }
    require(displayed.size() == 1 && displayed[0].requestId == 2 && displayed[0].image.pixelColor(0, 0).red() == 79,
            "stale pre-drain completion replaced the next display request");
}
}

int main(int argc, char **argv)
try
{
    QCoreApplication application(argc, argv);
    ownedDisplayPixels();
    asynchronousSourcesRetainOwners();
    drainReleasesSourcesBeforeNextExecution();
    std::cout << "Owned display frames preserve color and normalization without retaining mutable business pixels\n";
}
catch (const std::exception &error)
{
    std::cerr << error.what() << '\n';
    return 1;
}
