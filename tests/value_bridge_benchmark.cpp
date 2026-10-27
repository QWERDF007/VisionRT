#include "python/PythonInterop.h"
#include "ui/DisplayConverter.h"
#include "ui/OutputModel.h"

#include <QCommandLineParser>
#include <QCoreApplication>
#include <pybind11/embed.h>
#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <memory>
#include <optional>
#include <string>
#include <type_traits>
#include <vector>

#ifdef _WIN32
#    include <windows.h>
#    include <psapi.h>
#elif defined(__linux__)
#    include <sys/resource.h>
#endif

using namespace visionrt;
namespace py = pybind11;
using Clock  = std::chrono::steady_clock;

namespace {
void require(bool condition, const char *message)
{
    if (!condition)
        throw std::runtime_error(message);
}

std::optional<std::uint64_t> processPeakBytes()
{
#ifdef _WIN32
    PROCESS_MEMORY_COUNTERS counters{};
    if (GetProcessMemoryInfo(GetCurrentProcess(), &counters, sizeof(counters)))
        return counters.PeakWorkingSetSize;
#elif defined(__linux__)
    rusage usage{};
    if (getrusage(RUSAGE_SELF, &usage) == 0)
        return static_cast<std::uint64_t>(usage.ru_maxrss) * 1024;
#endif
    return {};
}

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

template<class Vector>
Vector nested(std::size_t elements)
{
    using Element = typename Vector::value_type;
    Vector result;
    if constexpr (std::is_arithmetic_v<Element> || std::is_same_v<Element, std::string>)
    {
        result.reserve(elements);
        for (std::size_t index = 0; index < elements; ++index)
        {
            if constexpr (std::is_same_v<Element, std::string>)
                result.push_back("图像中文");
            else if constexpr (std::is_same_v<Element, std::int64_t>)
                result.push_back(9007199254740993LL + static_cast<std::int64_t>(index % 2));
            else
                result.push_back(static_cast<Element>(index % 2));
        }
    }
    else
    {
        constexpr std::size_t branches = 4;
        for (std::size_t index = 0; index < branches; ++index)
            result.push_back(nested<Element>(elements / branches + (index < elements % branches ? 1 : 0)));
    }
    return result;
}

class Benchmark final
{
public:
    Benchmark(const std::filesystem::path &output, bool full)
        : full_(full)
        , identity_(py::eval("lambda value: value"))
    {
        if (!output.parent_path().empty())
            std::filesystem::create_directories(output.parent_path());
        raw_.open(output);
        auto summary = output;
        summary.replace_extension(".summary.csv");
        summary_.open(summary);
        require(raw_.good() && summary_.good(), "Cannot open benchmark CSV files");
        const char *prefix = "mode,type,dtype,rank,scalar_leaves,image_count,width,height,";
        raw_ << prefix
             << "sample,to_python_ms,identity_algorithm_ms,from_python_ms,display_ms,total_ms,"
                "peak_supported,process_peak_bytes\n";
        summary_ << prefix
                 << "samples,to_python_p50_ms,to_python_p95_ms,identity_algorithm_p50_ms,"
                    "identity_algorithm_p95_ms,from_python_p50_ms,from_python_p95_ms,"
                    "display_p50_ms,display_p95_ms,total_p50_ms,total_p95_ms,roundtrips_per_second,"
                    "peak_supported,process_peak_bytes\n";
    }

    void run(const PortValue &value, std::size_t leaves)
    {
        const auto  type       = *typeOf(value);
        const auto *images     = std::get_if<DataRef<Image>>(&value);
        const auto  imageCount = images ? images->get().size() : 0;
        const auto  width      = images ? images->get().front().cols : 0;
        const auto  height     = images ? images->get().front().rows : 0;
        const auto  prefix     = std::string(full_ ? "full," : "smoke,") + typeName(type.type) + ","
                          + (type.dtype ? dtypeName(*type.dtype) : "") + "," + std::to_string(type.rank) + ","
                          + std::to_string(leaves) + "," + std::to_string(imageCount) + "," + std::to_string(width)
                          + "," + std::to_string(height) + ",";
        std::array<std::vector<double>, 5> phases;
        const int                          samples = full_ ? 21 : 3;
        ToolDefinition                     definition;
        definition.outputs.push_back({"value", "Value", type});
        OutputModel model;
        const auto  started = Clock::now();
        for (int sample = 0; sample < samples; ++sample)
        {
            const auto                begin         = Clock::now();
            const auto                object        = toPython(value);
            const auto                inputReady    = Clock::now();
            const auto                returned      = identity_(object);
            const auto                algorithmDone = Clock::now();
            const auto                decoded       = fromPython(type, returned, storage_);
            const auto                outputReady   = Clock::now();
            std::vector<DisplayFrame> frames;
            if (images)
            {
                const auto &roundtrip = std::get<DataRef<Image>>(decoded).get();
                require(roundtrip.size() == imageCount, "Image batch changed during bridge benchmark");
                for (std::size_t batch = 0; batch < imageCount; ++batch)
                {
                    require(roundtrip[batch].data == images->get()[batch].data
                                && roundtrip[batch].step[0] == images->get()[batch].step[0],
                            "Image bridge copied storage or changed stride");
                    frames.push_back(DisplayConverter::convert(roundtrip[batch], "BGR"));
                    require(frames.back().error.isEmpty() && !frames.back().image.isNull()
                                && frames.back().image.constBits() != roundtrip[batch].data,
                            "Display did not create independent Qt storage");
                }
            }
            else
            {
                auto snapshot = std::make_shared<NodeSnapshot>();
                snapshot->outputs.emplace("value", decoded);
                model.setResult(&definition, std::move(snapshot), {});
                const auto displayed = model.get(2);
                require(!displayed["preview"].toString().isEmpty() && displayed["preview"].toString().size() <= 256
                            && displayed["tooltip"].toString().size() <= 4096,
                        "Display preview missing or unbounded");
            }
            const auto                  displayReady = Clock::now();
            const std::array<double, 5> elapsed{
                milliseconds(begin, inputReady), milliseconds(inputReady, algorithmDone),
                milliseconds(algorithmDone, outputReady), milliseconds(outputReady, displayReady),
                milliseconds(begin, displayReady)};
            require(PyList_CheckExact(object.ptr()) && PyList_CheckExact(returned.ptr()),
                    "Bridge substituted a non-list collection");
            if (sample == 0 && !images)
                require(equalValues(value, decoded), "Array/ROI benchmark roundtrip changed native values");
            raw_ << prefix << sample;
            for (std::size_t phase = 0; phase < phases.size(); ++phase)
            {
                phases[phase].push_back(elapsed[phase]);
                raw_ << ',' << elapsed[phase];
            }
            const auto peak = processPeakBytes();
            raw_ << ',' << peak.has_value() << ',' << peak.value_or(0) << '\n';
        }
        const auto seconds = std::chrono::duration<double>(Clock::now() - started).count();
        const auto peak    = processPeakBytes();
        summary_ << prefix << samples;
        for (const auto &phase : phases) summary_ << ',' << percentile(phase, 0.50) << ',' << percentile(phase, 0.95);
        summary_ << ',' << samples / seconds << ',' << peak.has_value() << ',' << peak.value_or(0) << '\n';
        raw_.flush();
        summary_.flush();
        require(raw_.good() && summary_.good(), "Benchmark CSV write failed");
        storage_.waitForRelease();
        std::cout << prefix << "total_p95_ms=" << percentile(phases.back(), 0.95) << std::endl;
    }

private:
    bool          full_;
    py::object    identity_;
    PythonStorage storage_;
    std::ofstream raw_, summary_;
};

template<class Element, int Rank = 1>
void arrays(Benchmark &benchmark, const std::vector<std::size_t> &sizes)
{
    for (const auto size : sizes) benchmark.run(Array(nested<NestedVector<Element, Rank>>(size)), size);
    if constexpr (Rank < kMaxArrayRank)
        arrays<Element, Rank + 1>(benchmark, sizes);
}
}

int main(int argc, char **argv)
try
{
    QCoreApplication   application(argc, argv);
    QCommandLineParser parser;
    parser.addHelpOption();
    parser.addOption({"full", "Measure 1k/100k/1M leaves and 1/8/32 4K images"});
    parser.addOption({"output", "Raw CSV output path", "path"});
    parser.process(application);
    require(parser.isSet("output"), "--output is required");
    const bool                     full = parser.isSet("full");
    const std::vector<std::size_t> sizes
        = full ? std::vector<std::size_t>{1000, 100000, 1000000} : std::vector<std::size_t>{1000};
    py::scoped_interpreter interpreter;
    Benchmark              benchmark(std::filesystem::path(parser.value("output").toStdU16String()), full);
    arrays<std::uint8_t>(benchmark, sizes);
    arrays<std::int32_t>(benchmark, sizes);
    arrays<std::int64_t>(benchmark, sizes);
    arrays<float>(benchmark, sizes);
    arrays<double>(benchmark, sizes);
    arrays<bool>(benchmark, sizes);
    arrays<std::string>(benchmark, sizes);
    for (const auto size : sizes)
    {
        ROI regions{std::vector<std::vector<double>>(size / 6, {2., 1.25, 2.5, 3.75, 4.5, 17.5})};
        regions.front().push_back({3., 1.25, 2.5, 3.75});
        benchmark.run(DataRef<ROI>(std::move(regions)), size);
    }
    for (const auto count : full ? std::vector<int>{1, 8, 32} : std::vector<int>{1})
    {
        Image images;
        for (int batch = 0; batch < count; ++batch)
            images.emplace_back(full ? 2160 : 480, full ? 3840 : 640, CV_8UC3, cv::Scalar(17, 29, 43));
        benchmark.run(DataRef<Image>(std::move(images)), 0);
    }
    return 0;
}
catch (const std::exception &error)
{
    std::cerr << error.what() << '\n';
    return 1;
}
