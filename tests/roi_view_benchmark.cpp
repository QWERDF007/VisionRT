// Cost boundary of the unchanged nested ROI carrier: every record is still its own
// vector<double>, so construction pays one allocation per region. This benchmark measures that
// baseline and the read path - a validated dispatch versus a trusted one - so the numbers say what
// the semantic views do and do not remove.
#include "visionrt/contracts/ROI.h"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <fstream>
#include <iostream>
#include <string>
#include <vector>

using namespace visionrt;
using Clock = std::chrono::steady_clock;

namespace {
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

volatile double sink = 0.0;

void report(std::ostream &out, std::ofstream &csv, const char *name, std::size_t records,
            const std::vector<double> &samples)
{
    const double median = percentile(samples, 0.5);
    const double p95    = percentile(samples, 0.95);
    const double ns     = records > 0 ? median * 1.0e6 / static_cast<double>(records) : 0.0;
    out << name << ',' << records << ',' << median << ',' << p95 << ',' << ns << '\n';
    if (csv.is_open())
        csv << name << ',' << records << ',' << median << ',' << p95 << ',' << ns << '\n';
}
} // namespace

int main(int argc, char **argv)
{
    std::string   output;
    int           iterations = 15;
    std::size_t   records    = 100000;
    for (int index = 1; index < argc; ++index)
    {
        const std::string argument(argv[index]);
        if (argument == "--output" && index + 1 < argc)
            output = argv[++index];
        else if (argument == "--iterations" && index + 1 < argc)
            iterations = std::max(3, std::atoi(argv[++index]));
        else if (argument == "--records" && index + 1 < argc)
            records = static_cast<std::size_t>(std::max(1, std::atoi(argv[++index])));
    }

    std::ofstream csv;
    if (!output.empty())
    {
        csv.open(output);
        csv << "scenario,records,median_ms,p95_ms,ns_per_record\n";
    }
    std::cout << "scenario,records,median_ms,p95_ms,ns_per_record\n";

    // Construction: one encoded vector per rectangle inside one group.
    std::vector<double> construction;
    for (int iteration = 0; iteration < iterations; ++iteration)
    {
        const auto begin = Clock::now();
        RegionGroup group;
        group.reserve(records);
        for (std::size_t index = 0; index < records; ++index)
            group.push_back(makeRectangle(static_cast<double>(index % 4096), static_cast<double>(index % 2160), 12.0,
                                          8.0, static_cast<double>(index % 90)));
        const auto end = Clock::now();
        sink += static_cast<double>(group.size());
        construction.push_back(milliseconds(begin, end));
    }
    report(std::cout, csv, "construct_rectangle", records, construction);

    RegionGroup rectangles;
    rectangles.reserve(records);
    for (std::size_t index = 0; index < records; ++index)
        rectangles.push_back(makeRectangle(static_cast<double>(index % 4096), static_cast<double>(index % 2160), 12.0, 8.0,
                                           static_cast<double>(index % 90)));

    // Validated dispatch: every read checks the encoding once, the way an external entry does.
    std::vector<double> validated;
    for (int iteration = 0; iteration < iterations; ++iteration)
    {
        const auto begin = Clock::now();
        double     total = 0;
        for (const auto &record : rectangles)
        {
            const auto rectangle = RegionView(record).rectangle();
            total += rectangle.center().x + rectangle.width();
        }
        const auto end = Clock::now();
        sink += total;
        validated.push_back(milliseconds(begin, end));
    }
    report(std::cout, csv, "read_rectangle_validated", records, validated);

    // Trusted dispatch: the same named read without re-validating data this module produced.
    std::vector<double> trusted;
    for (int iteration = 0; iteration < iterations; ++iteration)
    {
        const auto begin = Clock::now();
        double     total = 0;
        for (const auto &record : rectangles)
        {
            const auto rectangle = RegionView::trusted(record, RoiKind::Rectangle).rectangle();
            total += rectangle.center().x + rectangle.width();
        }
        const auto end = Clock::now();
        sink += total;
        trusted.push_back(milliseconds(begin, end));
    }
    report(std::cout, csv, "read_rectangle_trusted", records, trusted);

    // Point sets keep a single encoded vector, so the per-point read is a pair of loads.
    constexpr std::size_t pointsPerSet = 64;
    std::vector<cv::Point2d> points;
    points.reserve(pointsPerSet);
    for (std::size_t index = 0; index < pointsPerSet; ++index)
        points.push_back({static_cast<double>(index), static_cast<double>(index) * 0.5});
    std::vector<double> pointSetReads;
    const std::size_t   sets = std::max<std::size_t>(1, records / pointsPerSet);
    RegionGroup         pointSets;
    pointSets.reserve(sets);
    for (std::size_t index = 0; index < sets; ++index) pointSets.push_back(makePointSet(points));
    for (int iteration = 0; iteration < iterations; ++iteration)
    {
        const auto begin = Clock::now();
        double     total = 0;
        for (const auto &record : pointSets)
        {
            const auto view = RegionView::trusted(record, RoiKind::PointSet).pointSet();
            for (std::size_t index = 0; index < view.size(); ++index)
            {
                const auto point = view.point(index);
                total += point.x + point.y;
            }
        }
        const auto end = Clock::now();
        sink += total;
        pointSetReads.push_back(milliseconds(begin, end));
    }
    report(std::cout, csv, "read_pointset_trusted", sets * pointsPerSet, pointSetReads);

    if (!(sink > 0))
        return 1;
    return 0;
}
