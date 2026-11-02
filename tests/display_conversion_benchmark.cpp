// End-to-end cost of the display conversion: normalisation, colour assembly and the QImage copy for
// every depth the viewport accepts. Same build, inputs and warm-up for before/after comparisons.
#include "ui/DisplayConverter.h"

#include <QCommandLineParser>
#include <QCoreApplication>

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <fstream>
#include <iostream>
#include <limits>
#include <stdexcept>
#include <string>
#include <vector>

using namespace visionrt;
using Clock = std::chrono::steady_clock;

namespace {

struct Scenario
{
    const char *name;
    cv::Mat     image;
    const char *order;
};

double milliseconds(Clock::time_point begin, Clock::time_point end)
{
    return std::chrono::duration<double, std::milli>(end - begin).count();
}

double percentile(std::vector<double> samples, double fraction)
{
    std::sort(samples.begin(), samples.end());
    const auto position = fraction * static_cast<double>(samples.size() - 1);
    const auto lower    = static_cast<std::size_t>(std::floor(position));
    const auto upper    = static_cast<std::size_t>(std::ceil(position));
    return samples[lower] + (samples[upper] - samples[lower]) * (position - lower);
}

// Deterministic content with real dynamic range, a NaN and an Inf so the non-finite path is included.
cv::Mat filled(int rows, int cols, int type, double base, double step)
{
    cv::Mat image(rows, cols, type);
    for (int y = 0; y < rows; ++y)
    {
        auto *row = reinterpret_cast<unsigned char *>(image.ptr(y));
        for (int x = 0; x < cols; ++x)
        {
            const double value = base + step * static_cast<double>((x * 7 + y * 13) % 97);
            const double value2 = base + step * static_cast<double>((x * 11 + y * 3) % 89);
            const double value3 = base + step * static_cast<double>((x * 5 + y * 17) % 83);
            auto        *pixel  = row + static_cast<std::size_t>(x) * image.elemSize();
            switch (CV_MAT_DEPTH(type))
            {
            case CV_8U:
                for (int c = 0; c < image.channels(); ++c)
                    pixel[c] = static_cast<unsigned char>(std::clamp(value + c * 5, 0.0, 255.0));
                break;
            case CV_16U:
                for (int c = 0; c < image.channels(); ++c)
                    reinterpret_cast<unsigned short *>(pixel)[c]
                        = static_cast<unsigned short>(std::clamp(value * 60 + c * 900, 0.0, 65535.0));
                break;
            case CV_32F:
                reinterpret_cast<float *>(pixel)[0] = static_cast<float>(value);
                if (image.channels() > 1)
                    reinterpret_cast<float *>(pixel)[1] = static_cast<float>(value2);
                if (image.channels() > 2)
                    reinterpret_cast<float *>(pixel)[2] = static_cast<float>(value3);
                if (image.channels() > 3)
                    reinterpret_cast<float *>(pixel)[3] = 0.5F;
                break;
            default:
                break;
            }
        }
    }
    if (CV_MAT_DEPTH(type) == CV_32F)
    {
        image.at<float>(0, 0) = std::numeric_limits<float>::quiet_NaN();
        image.at<float>(0, 1) = std::numeric_limits<float>::infinity();
    }
    return image;
}

} // namespace

int main(int argc, char **argv)
{
    QCoreApplication    application(argc, argv);
    QCommandLineParser  parser;
    parser.addHelpOption();
    const QCommandLineOption output("output", "CSV output path", "path");
    const QCommandLineOption iterations("iterations", "Measured iterations per scenario", "count", "15");
    parser.addOption(output);
    parser.addOption(iterations);
    parser.process(application);
    const int count = std::max(3, parser.value(iterations).toInt());

    const std::vector<Scenario> scenarios{
        {"1080p 16UC1", filled(1080, 1920, CV_16UC1, 0.0, 30.0), "Gray"},
        {"1080p 32FC1", filled(1080, 1920, CV_32FC1, -5.0, 0.25), "Gray"},
        {"1080p 8UC3", filled(1080, 1920, CV_8UC3, 0.0, 2.0), "BGR"},
        {"1080p 32FC4", filled(1080, 1920, CV_32FC4, 0.0, 0.01), "BGRA"},
        {"4K 16UC3", filled(2160, 3840, CV_16UC3, 0.0, 30.0), "BGR"},
        {"4K 32FC1", filled(2160, 3840, CV_32FC1, -5.0, 0.25), "Gray"},
    };

    std::ofstream csv;
    if (parser.isSet(output))
    {
        csv.open(parser.value(output).toStdString());
        csv << "scenario,median_ms,p95_ms,megapixels_per_second\n";
    }
    std::cout << "scenario,median_ms,p95_ms,megapixels_per_second\n";
    for (const auto &scenario : scenarios)
    {
        for (int warmup = 0; warmup < 3; ++warmup)
            DisplayConverter::convert(scenario.image, scenario.order);
        std::vector<double> samples;
        samples.reserve(static_cast<std::size_t>(count));
        for (int iteration = 0; iteration < count; ++iteration)
        {
            const auto begin = Clock::now();
            const auto frame = DisplayConverter::convert(scenario.image, scenario.order);
            const auto end   = Clock::now();
            if (frame.image.isNull() || !frame.error.isEmpty())
                throw std::runtime_error(std::string("conversion failed: ") + scenario.name);
            samples.push_back(milliseconds(begin, end));
        }
        const double median = percentile(samples, 0.5);
        const double p95    = percentile(samples, 0.95);
        const double pixels = static_cast<double>(scenario.image.total()) / 1.0e6;
        const double rate   = median > 0 ? pixels / (median / 1000.0) : 0.0;
        const auto   line   = QStringLiteral("%1,%2,%3,%4")
                                .arg(QString::fromLatin1(scenario.name))
                                .arg(median, 0, 'f', 3)
                                .arg(p95, 0, 'f', 3)
                                .arg(rate, 0, 'f', 1);
        std::cout << line.toStdString() << '\n';
        if (csv.is_open())
            csv << line.toStdString() << '\n';
    }
    return 0;
}
