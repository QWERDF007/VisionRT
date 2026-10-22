#include "core/ToolRegistry.h"
#include "plugins/PluginLoader.h"
#include <core/ToolPackage.h>
#include "ImageFixtures.h"

#include <opencv2/imgcodecs.hpp>

#include <QDir>
#include <QFile>
#include <QTemporaryDir>
#include <algorithm>
#include <array>
#include <cassert>
#include <iostream>
#include <limits>

using namespace visionrt;

namespace {
ToolResult execute(ToolRegistry &registry, const char *id, Values inputs, const ToolServices &services = {})
{
    const auto *definition = registry.find(id);
    assert((definition));
    for (const auto &port : definition->inputs)
        inputs.try_emplace(port.id, port.defaultValue);
    Values parameters;
    for (const auto &port : definition->initParams)
        parameters.emplace(port.id, port.defaultValue);
    auto tool = registry.create(id, services);
    assert((tool->initialize(parameters)));
    auto result = tool->execute(inputs);
    assert((tool->release()));
    return result;
}

void success(const ToolResult &result)
{
    if (!result.status)
        std::cerr << result.status.message << '\n';
    assert((result.status));
}

void failure(const ToolResult &result)
{
    assert((!result.status));
    assert((!result.status.message.empty()));
    for (const auto &[id, value] : result.outputs) assert(isMissing(value));
}

void saveImage(const QString &path, unsigned char value)
{
    std::vector<unsigned char> bytes;
    assert((cv::imencode(".png", cv::Mat(2, 3, CV_8UC1, cv::Scalar(value)), bytes)));
    QFile file(path);
    assert((file.open(QIODevice::WriteOnly)));
    assert(
        (file.write(reinterpret_cast<const char *>(bytes.data()), bytes.size()) == static_cast<qint64>(bytes.size())));
}

void readImages(ToolRegistry &registry)
{
    QTemporaryDir directory;
    assert((directory.isValid()));
    const auto folder = directory.path() + QString::fromUtf8("/中文目录");
    assert((QDir().mkpath(folder)));
    saveImage(folder + "/b.png", 22);
    saveImage(folder + "/a.png", 11);
    const auto         root = std::filesystem::u8path(directory.path().toUtf8().toStdString());
    const ToolServices services{PathResolver(root)};
    Values             inputs{
                    {"mode",    std::string("directory")},
                    {"path", std::string("中文目录")}
    };
    auto result = execute(registry, "visionrt.image.read", inputs, services);
    success(result);
    const auto &images = std::get<DataRef<Image>>(result.outputs.at("images"));
    assert((images.get().size() == 2));
    assert((images.get()[0].at<unsigned char>(0, 0) == 11));
    assert((images.get()[1].at<unsigned char>(0, 0) == 22));
    const auto paths = std::get<Array>(result.outputs.at("paths")).get<NestedVector<std::string, 1>>();
    assert((paths[0].ends_with("/a.png") && paths[1].ends_with("/b.png")));
    inputs["limit"] = std::int64_t{-1};
    result = execute(registry, "visionrt.image.read", inputs, services);
    failure(result);
    assert((result.status.message.find("limit") != std::string::npos));
    inputs["limit"] = std::int64_t{1};
    result          = execute(registry, "visionrt.image.read", inputs, services);
    success(result);
    assert((std::get<DataRef<Image>>(result.outputs.at("images")).get().size() == 1));
    saveImage(folder + "/a.png", 99);
    result = execute(registry, "visionrt.image.read", inputs, services);
    success(result);
    assert((std::get<DataRef<Image>>(result.outputs.at("images")).get()[0].at<unsigned char>(0, 0) == 99));
    QFile corrupt(folder + "/c.png");
    assert((corrupt.open(QIODevice::WriteOnly)));
    assert((corrupt.write("corrupt") == 7));
    corrupt.close();
    inputs["limit"] = std::int64_t{0};
    result          = execute(registry, "visionrt.image.read", inputs, services);
    failure(result);
    assert((result.status.message.find("c.png") != std::string::npos));
    const auto  oversized = testing::oversizedBmp();
    std::string decoderDetail;
    try
    {
        cv::imdecode(oversized, cv::IMREAD_COLOR);
    }
    catch (const cv::Exception &error)
    {
        decoderDetail = error.what();
    }
    assert((decoderDetail.find("validateInputImageSize") != std::string::npos));
    QFile oversizedFile(folder + "/oversized.bmp");
    assert((oversizedFile.open(QIODevice::WriteOnly)));
    assert(oversizedFile.write(reinterpret_cast<const char *>(oversized.data()), oversized.size())
           == static_cast<qint64>(oversized.size()));
    oversizedFile.close();
    result = execute(registry, "visionrt.image.read",
                     {
                         {"mode",                       std::string("file")},
                         {"path", std::string("中文目录/oversized.bmp")}
    },
                     services);
    failure(result);
    assert((result.status.code == 1));
    assert((result.status.message.find("图像解码失败") != std::string::npos));
    assert((result.status.message.find("oversized.bmp") != std::string::npos));
    assert((result.status.message.find("批次索引 0") != std::string::npos));
    assert((result.status.message.find("OpenCV") == std::string::npos));
    assert((result.status.detail == decoderDetail));
    inputs["mode"] = std::string("file");
    inputs["path"] = std::string("中文目录/a.png");
    success(execute(registry, "visionrt.image.read", inputs, services));
    inputs["path"] = std::string("absent.png");
    failure(execute(registry, "visionrt.image.read", inputs, services));
    assert((QDir().mkpath(directory.path() + "/empty")));
    result = execute(registry, "visionrt.image.read",
                     {
                         {"mode", std::string("directory")},
                         {"path",     std::string("empty")}
    },
                     services);
    success(result);
    assert((std::get<DataRef<Image>>(result.outputs.at("images")).get().empty()));
    assert((std::get<Array>(result.outputs.at("paths")).get<NestedVector<std::string, 1>>().size() == 0));
    const auto unicodeFolder = directory.path() + "/unicode_sort";
    assert((QDir().mkpath(unicodeFolder)));
    const auto supplementaryName = QString::fromUtf8("\xF0\x90\x80\x80.png"); // U+10000
    const auto bmpName           = QString::fromUtf8("\xEE\x80\x80.png");     // U+E000
    saveImage(unicodeFolder + "/" + supplementaryName, 100);
    saveImage(unicodeFolder + "/" + bmpName, 14);
    const Values unicodeInputs{
        {"mode",    std::string("directory")},
        {"path", std::string("unicode_sort")}
    };
    result = execute(registry, "visionrt.image.read", unicodeInputs, services);
    success(result);
    const auto &orderedImages = std::get<DataRef<Image>>(result.outputs.at("images"));
    assert((orderedImages.get().size() == 2));
    assert((orderedImages.get()[0].at<unsigned char>(0, 0) == 14));
    assert((orderedImages.get()[1].at<unsigned char>(0, 0) == 100));
    const auto orderedPaths = std::get<Array>(result.outputs.at("paths")).get<NestedVector<std::string, 1>>();
    assert((orderedPaths[0].ends_with(bmpName.toUtf8().toStdString())));
    assert((orderedPaths[1].ends_with(supplementaryName.toUtf8().toStdString())));
}

DataRef<Image> fixtureImages()
{
    cv::Mat bars(64, 96, CV_8UC1, cv::Scalar(0));
    bars.colRange(10, 30).setTo(255);
    bars.colRange(55, 75).setTo(255);
    return DataRef<Image>(Image{(cv::Mat(64, 96, CV_8UC1, cv::Scalar(0))), (std::move(bars))});
}

void projection(ToolRegistry &registry)
{
    const auto images = fixtureImages();
    auto       result = execute(registry, "visionrt.roi.projection",
                                {
                                    {"images", images}
    });
    success(result);
    const auto &rois = std::get<DataRef<ROI>>(result.outputs.at("rois"));
    assert((rois.get().size() == 2 && rois.get()[0].empty()));
    assert((rois.get()[1].size() == 2));
    const std::array<float, 6> first{2, 10, 0, 20, 64, 0};
    const std::array<float, 6> second{2, 55, 0, 20, 64, 0};
    assert((std::equal(first.begin(), first.end(), rois.get()[1][0].begin())));
    assert((std::equal(second.begin(), second.end(), rois.get()[1][1].begin())));
    assert((&std::get<DataRef<Image>>(result.outputs.at("images")).get() == &images.get()));
    result = execute(registry, "visionrt.roi.projection",
                     {
                         {    "images",           images},
                         { "merge_gap", std::int64_t{25}},
                         {"min_extent", std::int64_t{60}},
                         {   "padding", std::int64_t{12}}
    });
    success(result);
    const auto merged = std::get<DataRef<ROI>>(result.outputs.at("rois")).get()[1][0];
    assert((merged[1] == 0 && merged[3] == 87));
    result = execute(registry, "visionrt.roi.projection",
                     {
                         {   "images",              images},
                         {     "axis",    std::string("y")},
                         { "polarity", std::string("dark")},
                         {"threshold",     std::int64_t{0}}
    });
    success(result);
    assert((std::get<DataRef<ROI>>(result.outputs.at("rois")).get()[1].empty()));
    result = execute(registry, "visionrt.roi.projection",
                     {
                         {    "images",            images},
                         {      "axis",  std::string("y")},
                         { "threshold", std::int64_t{255}},
                         {"min_pixels",  std::int64_t{40}}
    });
    success(result);
    const auto vertical = std::get<DataRef<ROI>>(result.outputs.at("rois")).get()[1][0];
    assert((vertical[1] == 0 && vertical[2] == 0 && vertical[3] == 96 && vertical[4] == 64));
    result = execute(registry, "visionrt.roi.projection",
                     {
                         {    "images",           images},
                         {"min_pixels", std::int64_t{65}}
    });
    success(result);
    assert((std::get<DataRef<ROI>>(result.outputs.at("rois")).get()[1].empty()));
    failure(execute(registry, "visionrt.roi.projection",
                    {
                        {"images", Missing{}}
    }));
    failure(execute(registry, "visionrt.roi.projection",
                    {
                        {   "images",            images},
                        {"threshold", std::int64_t{256}}
    }));
}

void crop(ToolRegistry &registry)
{
    const auto images = fixtureImages();
    auto       result = execute(registry, "visionrt.image.crop",
                                {
                              {"images",                                     images},
                              {  "rect", DataRef<ROI>(ROI{{{2, 10, 0, 20, 64, 0}}})}
    });
    success(result);
    const auto &crops = std::get<DataRef<Image>>(result.outputs.at("images"));
    assert((crops.get().size() == images.get().size()));
    for (std::size_t index = 0; index < crops.get().size(); ++index)
    {
        const auto &image = crops.get()[index];
        assert((image.rows == 64 && image.cols == 20));
        assert((cv::countNonZero(image != (index == 0 ? 0 : 255)) == 0));
        assert((image.data == images.get()[index].data + 10));
        assert((image.u == images.get()[index].u));
        assert((image.step == images.get()[index].step));
    }
    assert((!result.outputs.contains("source_indices")));
    const auto origins = std::get<Array>(result.outputs.at("origins")).get<NestedVector<double, 2>>();
    assert(((origins
             == NestedVector<double, 2>{
                 {10, 0},
                 {10, 0}
    })));
    const DataRef<Image> mixed(
        Image{(cv::Mat(8, 10, CV_8UC1, cv::Scalar(11))), (cv::Mat(5, 6, CV_8UC1, cv::Scalar(29)))});
    result = execute(registry, "visionrt.image.crop",
                     {
                         {"images",                                   mixed},
                         {  "rect", DataRef<ROI>(ROI{{{2, 4, 3, 4, 4, 0}}})}
    });
    success(result);
    const auto &mixedCrops = std::get<DataRef<Image>>(result.outputs.at("images")).get();
    assert((mixedCrops.size() == 2 && mixedCrops[0].rows == 4 && mixedCrops[0].cols == 4));
    assert((mixedCrops[1].rows == 2 && mixedCrops[1].cols == 2));
    for (std::size_t index = 0; index < mixedCrops.size(); ++index)
    {
        assert((cv::countNonZero(mixedCrops[index] != (index == 0 ? 11 : 29)) == 0));
        assert((mixedCrops[index].u == mixed.get()[index].u));
        assert((mixedCrops[index].step == mixed.get()[index].step));
    }
    const auto mixedOrigins = std::get<Array>(result.outputs.at("origins")).get<NestedVector<double, 2>>();
    assert(((mixedOrigins
             == NestedVector<double, 2>{
                 {4, 3},
                 {4, 3}
    })));
    failure(execute(registry, "visionrt.image.crop",
                    {
                        {"images",                                   mixed},
                        {  "rect", DataRef<ROI>(ROI{{{2, 7, 6, 2, 2, 0}}})}
    }));
    const DataRef<Image> solid(Image{(cv::Mat(5, 5, CV_8UC1, cv::Scalar(17)))});
    const auto run = [&](PortValue rectangle)
    {
        return execute(registry, "visionrt.image.crop", {{"images", solid}, {"rect", std::move(rectangle)}});
    };
    result = run(DataRef<ROI>(ROI{{{2, 1, 1, 2, 2, 45}}}));
    success(result);
    const auto &rotated = std::get<DataRef<Image>>(result.outputs.at("images")).get()[0];
    assert((rotated.rows == 4 && rotated.cols == 4));
    assert((cv::countNonZero(rotated) == 4));
    assert((rotated.at<unsigned char>(1, 1) == 17 && rotated.at<unsigned char>(0, 0) == 0));
    result = run(DataRef<ROI>(ROI{{{2, -1, -1, 3, 3, 0}}}));
    success(result);
    const auto &clipped = std::get<DataRef<Image>>(result.outputs.at("images")).get()[0];
    assert((clipped.rows == 2 && clipped.cols == 2));
    assert((cv::countNonZero(solid.get()[0] != 17) == 0));
    failure(run(DataRef<ROI>(ROI{{{2, 6, 0, 2, 2, 0}}})));
    failure(run(DataRef<ROI>(ROI{{{3, 2.5F, 2.5F, 1}}})));
    failure(run(DataRef<ROI>(ROI{{{1, 0, 0, 3, 3}}})));
    failure(run(Missing{}));
    failure(run(DataRef<ROI>(ROI{
        {{2, 0, 0, 2, 2, 0}, {2, 2, 2, 2, 2, 0}}
    })));
    result = execute(registry, "visionrt.image.crop",
                     {
                         {"images",                 DataRef<Image>(Image{})},
                         {  "rect", DataRef<ROI>(ROI{{{2, 0, 0, 2, 2, 0}}})}
    });
    success(result);
    assert((std::get<DataRef<Image>>(result.outputs.at("images")).get().empty()));
    assert(((std::get<Array>(result.outputs.at("origins")).get<NestedVector<double, 2>>().empty())));
}

void preprocess(ToolRegistry &registry)
{
    cv::Mat source(1, 3, CV_8UC3);
    source.at<cv::Vec3b>(0, 0) = {0, 0, 255};
    source.at<cv::Vec3b>(0, 1) = {128, 128, 128};
    source.at<cv::Vec3b>(0, 2) = {255, 255, 255};
    const DataRef<Image> images(Image{(std::move(source))});
    auto             result = execute(registry, "visionrt.image.preprocess",
                                      {
                                          {"images", images}
    });
    success(result);
    assert((&std::get<DataRef<Image>>(result.outputs.at("images")).get() == &images.get()));
    result = execute(registry, "visionrt.image.preprocess",
                     {
                         {   "images",              images},
                         {"operation", std::string("gray")}
    });
    success(result);
    const auto &grayscale = std::get<DataRef<Image>>(result.outputs.at("images")).get()[0];
    assert((grayscale.rows == 1 && grayscale.cols == 3 && grayscale.channels() == 1));
    assert((grayscale.at<unsigned char>(0, 0) == 76));
    result = execute(registry, "visionrt.image.preprocess",
                     {
                         {   "images",                   images},
                         {"operation", std::string("threshold")},
                         {"threshold",        std::int64_t{128}}
    });
    success(result);
    const auto &binary = std::get<DataRef<Image>>(result.outputs.at("images")).get()[0];
    assert((binary.at<unsigned char>(0, 0) == 0));
    assert((binary.at<unsigned char>(0, 1) == 255 && binary.at<unsigned char>(0, 2) == 255));
    cv::Mat impulse(3, 3, CV_8UC1, cv::Scalar(0));
    impulse.at<unsigned char>(1, 1) = 160;
    const DataRef<Image> impulseBatch(Image{(std::move(impulse))});
    result = execute(registry, "visionrt.image.preprocess",
                     {
                         {   "images",            impulseBatch},
                         {"operation", std::string("gaussian")}
    });
    success(result);
    const auto &blurred = std::get<DataRef<Image>>(result.outputs.at("images")).get()[0];
    assert((cv::countNonZero(blurred != 40) == 0));
    assert((impulseBatch.get()[0].at<unsigned char>(1, 1) == 160));
    failure(execute(registry, "visionrt.image.preprocess",
                    {
                        {   "images",                  images},
                        {"operation", std::string("gaussian")},
                        {   "kernel",         std::int64_t{2}}
    }));
    const DataRef<Image> invalid(Image{(cv::Mat(2, 2, CV_16UC1, cv::Scalar(1)))});
    failure(execute(registry, "visionrt.image.preprocess",
                    {
                        {"images", invalid}
    }));
}

void filter(ToolRegistry &registry)
{
    const auto         images = fixtureImages();
    const DataRef<ROI> rois(ROI{
        {},
        {std::vector<double>{2, 0, 0, 10, 10, 0}, std::vector<double>{2, 15, 0, 4, 4, 0},
         std::vector<double>{2, 25, 0, 8, 8, 15}}
    });
    const Array        scores(NestedVector<float, 2>{
        std::vector<float>{},
        std::vector<float>{0.8F, 0.95F, 0.7F}
    });
    const Array        classes(NestedVector<std::int64_t, 2>{
        std::vector<std::int64_t>{},
        std::vector<std::int64_t>{1, 2, 1}
    });
    const Array        names(NestedVector<std::string, 2>{
        {},
        {"first", "second", "third"}
    });
    const Array        allowed(std::vector<std::int64_t>{1});
    Values             inputs{
        {         "images",  images},
        {           "rois",    rois},
        {         "scores",  scores},
        {        "classes", classes},
        {    "class_names",   names},
        {      "min_score",     0.6},
        {       "min_area",    64.0},
        {"allowed_classes", allowed}
    };
    auto result = execute(registry, "visionrt.detection.filter", inputs);
    success(result);
    const auto counts = std::get<Array>(result.outputs.at("counts")).get<NestedVector<std::int64_t, 1>>();
    assert((counts.size() == 2 && counts[0] == 0 && counts[1] == 2));
    const auto &keptRois = std::get<DataRef<ROI>>(result.outputs.at("rois"));
    assert((keptRois.get()[0].empty()));
    assert((keptRois.get()[1][0][1] == 0 && keptRois.get()[1][1][1] == 25));
    const auto keptScores = std::get<Array>(result.outputs.at("scores")).get<NestedVector<float, 2>>()[1];
    assert((keptScores[0] == 0.8F && keptScores[1] == 0.7F));
    const auto keptClasses = std::get<Array>(result.outputs.at("classes")).get<NestedVector<std::int64_t, 2>>()[1];
    assert((keptClasses[0] == 1 && keptClasses[1] == 1));
    const auto keptNames = std::get<Array>(result.outputs.at("class_names")).get<NestedVector<std::string, 2>>()[1];
    assert((keptNames[0] == "first" && keptNames[1] == "third"));
    assert((&std::get<DataRef<Image>>(result.outputs.at("images")).get() == &images.get()));
    inputs["min_area"] = 0.0;
    result             = execute(registry, "visionrt.detection.filter", inputs);
    success(result);
    assert((std::get<Array>(result.outputs.at("counts")).get<NestedVector<std::int64_t, 1>>()[1] == 2));
    inputs["allowed_classes"] = Array(std::vector<std::int64_t>{});
    inputs["min_area"] = 64.0;
    result             = execute(registry, "visionrt.detection.filter", inputs);
    success(result);
    assert((std::get<Array>(result.outputs.at("class_names")).get<NestedVector<std::string, 2>>()[1][1] == "third"));
    assert((std::get<Array>(result.outputs.at("counts")).get<NestedVector<std::int64_t, 1>>()[1] == 2));
    inputs["min_area"]  = 0.0;
    inputs["min_score"] = 0.9;
    result              = execute(registry, "visionrt.detection.filter", inputs);
    success(result);
    assert((std::get<Array>(result.outputs.at("counts")).get<NestedVector<std::int64_t, 1>>()[1] == 1));
    assert((std::get<Array>(result.outputs.at("class_names")).get<NestedVector<std::string, 2>>()[1][0] == "second"));
    inputs["allowed_classes"] = allowed;
    inputs["min_area"]        = 64.0;
    inputs["min_score"]       = 0.9;
    result                    = execute(registry, "visionrt.detection.filter", inputs);
    success(result);
    assert((std::get<DataRef<ROI>>(result.outputs.at("rois")).get()[1].empty()));
    assert((std::get<Array>(result.outputs.at("class_names")).get<NestedVector<std::string, 2>>()[1].size() == 0));
    inputs["scores"] = Array(NestedVector<float, 2>{
        std::vector<float>{},
        std::vector<float>{0.8F, 0.95F}
    });
    failure(execute(registry, "visionrt.detection.filter", inputs));
    inputs["scores"] = scores;
    inputs["scores"] = Array(NestedVector<float, 2>{
        std::vector<float>{},
        std::vector<float>{0.8F, std::numeric_limits<float>::quiet_NaN(), 0.7F}
    });
    failure(execute(registry, "visionrt.detection.filter", inputs));
    inputs["scores"]  = scores;
    inputs["classes"] = Array(NestedVector<std::int64_t, 2>{
        std::vector<std::int64_t>{},
        std::vector<std::int64_t>{1, -1, 1}
    });
    failure(execute(registry, "visionrt.detection.filter", inputs));
    inputs["classes"] = classes;
    inputs["rois"]    = DataRef<ROI>(ROI{
           {},
           {std::vector<double>{3, 2, 2, 1}, rois.get()[1][1], rois.get()[1][2]}
    });
    failure(execute(registry, "visionrt.detection.filter", inputs));
}

void incompleteInstallation(ToolRegistry &completeRegistry)
{
    ToolRegistry registry;
    registry.addDefinition(*completeRegistry.find("visionrt.image.preprocess"));
    registry.setPluginLoader(completeRegistry.pluginLoader());
    assert((registry.availability("visionrt.image.read").state == ToolAvailability::DefinitionMissing));
    cv::Mat pixels(1, 2, CV_8UC1);
    pixels.at<unsigned char>(0, 0) = 127;
    pixels.at<unsigned char>(0, 1) = 128;
    const auto result              = execute(registry, "visionrt.image.preprocess",
                                             {
                                    {   "images", DataRef<Image>(Image{(std::move(pixels))})},
                                    {"operation",                   std::string("threshold")}
    });
    success(result);
    const auto &output = std::get<DataRef<Image>>(result.outputs.at("images")).get()[0];
    assert((output.at<unsigned char>(0, 0) == 0 && output.at<unsigned char>(0, 1) == 255));
}
} // namespace

int main(int argc, char **argv)
{
    assert((argc == 2));
    ToolRegistry registry;
    registry.setPluginLoader(std::make_shared<PluginLoader>());
    registry.registerPackage(readToolsPackage(std::filesystem::u8path(argv[1])));
    registry.loadNativeImplementations();
    readImages(registry);
    projection(registry);
    crop(registry);
    preprocess(registry);
    filter(registry);
    incompleteInstallation(registry);
}
