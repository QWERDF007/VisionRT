#include "core/ToolRegistry.h"
#include "plugins/PluginLoader.h"
#include <visionrt/sdk/Package.h>

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
    auto tool = registry.create(id, services);
    assert(tool->initialize({}));
    auto result = tool->execute(inputs);
    assert(tool->release());
    return result;
}

void success(const ToolResult &result)
{
    if (!result.status)
        std::cerr << result.status.message << '\n';
    assert(result.status);
}

void failure(const ToolResult &result)
{
    assert(!result.status);
    assert(!result.status.message.empty());
    for (const auto &[id, value] : result.outputs) assert(isMissing(value));
}

template<class T>
NumericBuffer buffer(DType dtype, std::vector<std::int64_t> shape, const std::vector<T> &data)
{
    return NumericBuffer::copy(dtype, std::move(shape), std::as_bytes(std::span(data)));
}

void saveImage(const QString &path, unsigned char value)
{
    std::vector<unsigned char> bytes;
    assert(cv::imencode(".png", cv::Mat(2, 3, CV_8UC1, cv::Scalar(value)), bytes));
    QFile file(path);
    assert(file.open(QIODevice::WriteOnly));
    assert(file.write(reinterpret_cast<const char *>(bytes.data()), bytes.size()) == static_cast<qint64>(bytes.size()));
}

void readImages(ToolRegistry &registry)
{
    QTemporaryDir directory;
    assert(directory.isValid());
    const auto folder = directory.path() + QString::fromUtf8("/中文目录");
    assert(QDir().mkpath(folder));
    saveImage(folder + "/b.png", 22);
    saveImage(folder + "/a.png", 11);
    const auto         root = std::filesystem::u8path(directory.path().toUtf8().toStdString());
    const ToolServices services{PathResolver(root)};
    Values             inputs{
        {"mode", Utf8("directory")},
        {"path",  Utf8("中文目录")}
    };
    auto result = execute(registry, "visionrt.image.read", inputs, services);
    success(result);
    const auto &images = std::get<ImageBatch>(result.outputs.at("images"));
    assert(images.images().size() == 2);
    assert(images.images()[0].readOnlyMat().at<unsigned char>(0, 0) == 11);
    assert(images.images()[1].readOnlyMat().at<unsigned char>(0, 0) == 22);
    const auto paths = std::get<StringArray>(result.outputs.at("paths")).buffers()[0].values();
    assert(paths[0].ends_with("/a.png") && paths[1].ends_with("/b.png"));
    inputs["limit"] = std::int64_t{-1};
    result = execute(registry, "visionrt.image.read", inputs, services);
    failure(result);
    assert(result.status.message.find("limit") != std::string::npos);
    inputs["limit"] = std::int64_t{1};
    result          = execute(registry, "visionrt.image.read", inputs, services);
    success(result);
    assert(std::get<ImageBatch>(result.outputs.at("images")).images().size() == 1);
    saveImage(folder + "/a.png", 99);
    result = execute(registry, "visionrt.image.read", inputs, services);
    success(result);
    assert(std::get<ImageBatch>(result.outputs.at("images")).images()[0].readOnlyMat().at<unsigned char>(0, 0) == 99);
    QFile corrupt(folder + "/c.png");
    assert(corrupt.open(QIODevice::WriteOnly));
    assert(corrupt.write("corrupt") == 7);
    corrupt.close();
    inputs["limit"] = std::int64_t{0};
    result          = execute(registry, "visionrt.image.read", inputs, services);
    failure(result);
    assert(result.status.message.find("c.png") != std::string::npos);
    inputs["mode"] = Utf8("file");
    inputs["path"] = Utf8("中文目录/a.png");
    success(execute(registry, "visionrt.image.read", inputs, services));
    inputs["path"] = Utf8("absent.png");
    failure(execute(registry, "visionrt.image.read", inputs, services));
    assert(QDir().mkpath(directory.path() + "/empty"));
    result = execute(registry, "visionrt.image.read",
                     {
                         {"mode", Utf8("directory")},
                         {"path",     Utf8("empty")}
    },
                     services);
    success(result);
    assert(std::get<ImageBatch>(result.outputs.at("images")).images().empty());
    assert(std::get<StringArray>(result.outputs.at("paths")).buffers()[0].shape()[0] == 0);
    const auto unicodeFolder = directory.path() + "/unicode_sort";
    assert(QDir().mkpath(unicodeFolder));
    const auto supplementaryName = QString::fromUtf8("\xF0\x90\x80\x80.png"); // U+10000
    const auto bmpName           = QString::fromUtf8("\xEE\x80\x80.png");     // U+E000
    saveImage(unicodeFolder + "/" + supplementaryName, 100);
    saveImage(unicodeFolder + "/" + bmpName, 14);
    const Values unicodeInputs{
        {"mode",    Utf8("directory")},
        {"path", Utf8("unicode_sort")}
    };
    result = execute(registry, "visionrt.image.read", unicodeInputs, services);
    success(result);
    const auto &orderedImages = std::get<ImageBatch>(result.outputs.at("images"));
    assert(orderedImages.images().size() == 2);
    assert(orderedImages.images()[0].readOnlyMat().at<unsigned char>(0, 0) == 14);
    assert(orderedImages.images()[1].readOnlyMat().at<unsigned char>(0, 0) == 100);
    const auto orderedPaths = std::get<StringArray>(result.outputs.at("paths")).buffers()[0].values();
    assert(orderedPaths[0].ends_with(bmpName.toUtf8().toStdString()));
    assert(orderedPaths[1].ends_with(supplementaryName.toUtf8().toStdString()));
}

ImageBatch fixtureImages()
{
    cv::Mat bars(64, 96, CV_8UC1, cv::Scalar(0));
    bars.colRange(10, 30).setTo(255);
    bars.colRange(55, 75).setTo(255);
    return ImageBatch({OwnedImage::take(cv::Mat(64, 96, CV_8UC1, cv::Scalar(0))), OwnedImage::take(std::move(bars))});
}

void projection(ToolRegistry &registry)
{
    const auto images = fixtureImages();
    auto       result = execute(registry, "visionrt.roi.projection",
                                {
                                    {"images", images}
    });
    success(result);
    const auto &rois = std::get<RoiBatch>(result.outputs.at("rois"));
    assert(rois.images().size() == 2 && rois.images()[0].empty());
    assert(rois.images()[1].size() == 2);
    const std::array<float, 6> first{2, 10, 0, 20, 64, 0};
    const std::array<float, 6> second{2, 55, 0, 20, 64, 0};
    assert(std::equal(first.begin(), first.end(), rois.images()[1][0].encoded().begin()));
    assert(std::equal(second.begin(), second.end(), rois.images()[1][1].encoded().begin()));
    assert(std::get<ImageBatch>(result.outputs.at("images")).owner() == images.owner());
    result = execute(registry, "visionrt.roi.projection",
                     {
                         {    "images",           images},
                         { "merge_gap", std::int64_t{25}},
                         {"min_extent", std::int64_t{60}},
                         {   "padding", std::int64_t{12}}
    });
    success(result);
    const auto merged = std::get<RoiBatch>(result.outputs.at("rois")).images()[1][0].encoded();
    assert(merged[1] == 0 && merged[3] == 87);
    result = execute(registry, "visionrt.roi.projection",
                     {
                         {   "images",          images},
                         {     "axis",       Utf8("y")},
                         { "polarity",    Utf8("dark")},
                         {"threshold", std::int64_t{0}}
    });
    success(result);
    assert(std::get<RoiBatch>(result.outputs.at("rois")).images()[1].empty());
    result = execute(registry, "visionrt.roi.projection",
                     {
                         {    "images",            images},
                         {      "axis",         Utf8("y")},
                         { "threshold", std::int64_t{255}},
                         {"min_pixels",  std::int64_t{40}}
    });
    success(result);
    const auto vertical = std::get<RoiBatch>(result.outputs.at("rois")).images()[1][0].encoded();
    assert(vertical[1] == 0 && vertical[2] == 0 && vertical[3] == 96 && vertical[4] == 64);
    result = execute(registry, "visionrt.roi.projection",
                     {
                         {    "images",           images},
                         {"min_pixels", std::int64_t{65}}
    });
    success(result);
    assert(std::get<RoiBatch>(result.outputs.at("rois")).images()[1].empty());
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
    auto result = execute(registry, "visionrt.image.crop",
                          {{"images", images}, {"rect", Roi::rectangle(10, 0, 20, 64, 0)}});
    success(result);
    const auto &crops = std::get<ImageBatch>(result.outputs.at("images"));
    assert(crops.images().size() == images.images().size());
    for (std::size_t index = 0; index < crops.images().size(); ++index)
    {
        const auto &image = crops.images()[index];
        assert(image.rows() == 64 && image.cols() == 20);
        assert(cv::countNonZero(image.readOnlyMat() != (index == 0 ? 0 : 255)) == 0);
        assert(image.data() == images.images()[index].data() + 10);
        assert(image.readOnlyMat().u == images.images()[index].readOnlyMat().u);
        assert(image.rowStride() == images.images()[index].rowStride());
    }
    assert(!result.outputs.contains("source_indices"));
    const auto origins = std::get<NumericArray>(result.outputs.at("origins")).buffers()[0].values<double>();
    assert((std::vector<double>(origins.begin(), origins.end()) == std::vector<double>{10, 0, 10, 0}));
    const ImageBatch mixed({OwnedImage::take(cv::Mat(8, 10, CV_8UC1, cv::Scalar(11))),
                            OwnedImage::take(cv::Mat(5, 6, CV_8UC1, cv::Scalar(29)))});
    result = execute(registry, "visionrt.image.crop",
                     {{"images", mixed}, {"rect", Roi::rectangle(4, 3, 4, 4, 0)}});
    success(result);
    const auto &mixedCrops = std::get<ImageBatch>(result.outputs.at("images")).images();
    assert(mixedCrops.size() == 2 && mixedCrops[0].rows() == 4 && mixedCrops[0].cols() == 4);
    assert(mixedCrops[1].rows() == 2 && mixedCrops[1].cols() == 2);
    for (std::size_t index = 0; index < mixedCrops.size(); ++index)
    {
        assert(cv::countNonZero(mixedCrops[index].readOnlyMat() != (index == 0 ? 11 : 29)) == 0);
        assert(mixedCrops[index].readOnlyMat().u == mixed.images()[index].readOnlyMat().u);
        assert(mixedCrops[index].rowStride() == mixed.images()[index].rowStride());
    }
    const auto mixedOrigins = std::get<NumericArray>(result.outputs.at("origins")).buffers()[0].values<double>();
    assert((std::vector<double>(mixedOrigins.begin(), mixedOrigins.end()) == std::vector<double>{4, 3, 4, 3}));
    failure(execute(registry, "visionrt.image.crop",
                    {{"images", mixed}, {"rect", Roi::rectangle(7, 6, 2, 2, 0)}}));
    const ImageBatch solid({OwnedImage::take(cv::Mat(5, 5, CV_8UC1, cv::Scalar(17)))});
    const auto run = [&](PortValue rectangle)
    {
        return execute(registry, "visionrt.image.crop", {{"images", solid}, {"rect", std::move(rectangle)}});
    };
    result = run(Roi::rectangle(1, 1, 2, 2, 45));
    success(result);
    const auto &rotated = std::get<ImageBatch>(result.outputs.at("images")).images()[0].readOnlyMat();
    assert(rotated.rows == 4 && rotated.cols == 4);
    assert(cv::countNonZero(rotated) == 4);
    assert(rotated.at<unsigned char>(1, 1) == 17 && rotated.at<unsigned char>(0, 0) == 0);
    result = run(Roi::rectangle(-1, -1, 3, 3, 0));
    success(result);
    const auto &clipped = std::get<ImageBatch>(result.outputs.at("images")).images()[0];
    assert(clipped.rows() == 2 && clipped.cols() == 2);
    assert(cv::countNonZero(solid.images()[0].readOnlyMat() != 17) == 0);
    failure(run(Roi::rectangle(6, 0, 2, 2, 0)));
    failure(run(Roi::circle(2.5F, 2.5F, 1)));
    failure(run(Roi::segment({0, 0}, {3, 3})));
    failure(run(Missing{}));
    failure(run(RoiBatch({{Roi::rectangle(0, 0, 2, 2, 0)}})));
    result = execute(registry, "visionrt.image.crop",
                     {{"images", ImageBatch{}}, {"rect", Roi::rectangle(0, 0, 2, 2, 0)}});
    success(result);
    assert(std::get<ImageBatch>(result.outputs.at("images")).images().empty());
    const auto shape = std::get<NumericArray>(result.outputs.at("origins")).buffers()[0].shape();
    assert(shape.size() == 2 && shape[0] == 0 && shape[1] == 2);
}

void preprocess(ToolRegistry &registry)
{
    cv::Mat source(1, 3, CV_8UC3);
    source.at<cv::Vec3b>(0, 0) = {0, 0, 255};
    source.at<cv::Vec3b>(0, 1) = {128, 128, 128};
    source.at<cv::Vec3b>(0, 2) = {255, 255, 255};
    const ImageBatch images({OwnedImage::take(std::move(source))});
    auto             result = execute(registry, "visionrt.image.preprocess",
                                      {
                                          {"images", images}
    });
    success(result);
    assert(std::get<ImageBatch>(result.outputs.at("images")).owner() == images.owner());
    result = execute(registry, "visionrt.image.preprocess",
                     {
                         {   "images",       images},
                         {"operation", Utf8("gray")}
    });
    success(result);
    const auto &grayscale = std::get<ImageBatch>(result.outputs.at("images")).images()[0];
    assert(grayscale.rows() == 1 && grayscale.cols() == 3 && grayscale.channels() == 1);
    assert(grayscale.readOnlyMat().at<unsigned char>(0, 0) == 76);
    result = execute(registry, "visionrt.image.preprocess",
                     {
                         {   "images",            images},
                         {"operation", Utf8("threshold")},
                         {"threshold", std::int64_t{128}}
    });
    success(result);
    const auto &binary = std::get<ImageBatch>(result.outputs.at("images")).images()[0].readOnlyMat();
    assert(binary.at<unsigned char>(0, 0) == 0);
    assert(binary.at<unsigned char>(0, 1) == 255 && binary.at<unsigned char>(0, 2) == 255);
    cv::Mat impulse(3, 3, CV_8UC1, cv::Scalar(0));
    impulse.at<unsigned char>(1, 1) = 160;
    const ImageBatch impulseBatch({OwnedImage::take(std::move(impulse))});
    result = execute(registry, "visionrt.image.preprocess",
                     {
                         {   "images",     impulseBatch},
                         {"operation", Utf8("gaussian")}
    });
    success(result);
    const auto &blurred = std::get<ImageBatch>(result.outputs.at("images")).images()[0].readOnlyMat();
    assert(cv::countNonZero(blurred != 40) == 0);
    assert(impulseBatch.images()[0].readOnlyMat().at<unsigned char>(1, 1) == 160);
    failure(execute(registry, "visionrt.image.preprocess",
                    {
                        {   "images",           images},
                        {"operation", Utf8("gaussian")},
                        {   "kernel",  std::int64_t{2}}
    }));
    const ImageBatch invalid({OwnedImage::take(cv::Mat(2, 2, CV_16UC1, cv::Scalar(1)))});
    failure(execute(registry, "visionrt.image.preprocess",
                    {
                        {"images", invalid}
    }));
}

void filter(ToolRegistry &registry)
{
    const auto         images = fixtureImages();
    const RoiBatch     rois({
        {},
        {Roi::rectangle(0, 0, 10, 10, 0), Roi::rectangle(15, 0, 4, 4, 0), Roi::rectangle(25, 0, 8, 8, 15)}
    });
    const NumericArray scores(DType::Float32, 2, Layout::BatchRagged,
                              {buffer(DType::Float32, {0}, std::vector<float>{}),
                               buffer(DType::Float32, {3}, std::vector<float>{0.8F, 0.95F, 0.7F})});
    const NumericArray classes(DType::Int64, 2, Layout::BatchRagged,
                               {buffer(DType::Int64, {0}, std::vector<std::int64_t>{}),
                                buffer(DType::Int64, {3}, std::vector<std::int64_t>{1, 2, 1})});
    const StringArray  names(2, Layout::BatchRagged,
                             {StringBuffer({0}, {}), StringBuffer({3}, {"first", "second", "third"})});
    const NumericArray allowed(DType::Int64, 1, Layout::Dense,
                               {buffer(DType::Int64, {1}, std::vector<std::int64_t>{1})});
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
    const auto counts = std::get<NumericArray>(result.outputs.at("counts")).buffers()[0].values<std::int64_t>();
    assert(counts.size() == 2 && counts[0] == 0 && counts[1] == 2);
    const auto &keptRois = std::get<RoiBatch>(result.outputs.at("rois"));
    assert(keptRois.images()[0].empty());
    assert(keptRois.images()[1][0].encoded()[1] == 0 && keptRois.images()[1][1].encoded()[1] == 25);
    const auto keptScores = std::get<NumericArray>(result.outputs.at("scores")).buffers()[1].values<float>();
    assert(keptScores[0] == 0.8F && keptScores[1] == 0.7F);
    const auto keptClasses = std::get<NumericArray>(result.outputs.at("classes")).buffers()[1].values<std::int64_t>();
    assert(keptClasses[0] == 1 && keptClasses[1] == 1);
    const auto keptNames = std::get<StringArray>(result.outputs.at("class_names")).buffers()[1].values();
    assert(keptNames[0] == "first" && keptNames[1] == "third");
    assert(std::get<ImageBatch>(result.outputs.at("images")).owner() == images.owner());
    inputs["min_area"] = 0.0;
    result             = execute(registry, "visionrt.detection.filter", inputs);
    success(result);
    assert(std::get<NumericArray>(result.outputs.at("counts")).buffers()[0].values<std::int64_t>()[1] == 2);
    inputs["allowed_classes"] = NumericArray(DType::Int64, 1, Layout::Dense,
                                             {buffer(DType::Int64, {0}, std::vector<std::int64_t>{})});
    inputs["min_area"] = 64.0;
    result             = execute(registry, "visionrt.detection.filter", inputs);
    success(result);
    assert(std::get<StringArray>(result.outputs.at("class_names")).buffers()[1].values()[1] == "third");
    assert(std::get<NumericArray>(result.outputs.at("counts")).buffers()[0].values<std::int64_t>()[1] == 2);
    inputs["min_area"]  = 0.0;
    inputs["min_score"] = 0.9;
    result              = execute(registry, "visionrt.detection.filter", inputs);
    success(result);
    assert(std::get<NumericArray>(result.outputs.at("counts")).buffers()[0].values<std::int64_t>()[1] == 1);
    assert(std::get<StringArray>(result.outputs.at("class_names")).buffers()[1].values()[0] == "second");
    inputs["allowed_classes"] = allowed;
    inputs["min_area"]        = 64.0;
    inputs["min_score"]       = 0.9;
    result                    = execute(registry, "visionrt.detection.filter", inputs);
    success(result);
    assert(std::get<RoiBatch>(result.outputs.at("rois")).images()[1].empty());
    assert(std::get<StringArray>(result.outputs.at("class_names")).buffers()[1].shape()[0] == 0);
    inputs["scores"] = NumericArray(DType::Float32, 2, Layout::BatchRagged,
                                    {buffer(DType::Float32, {0}, std::vector<float>{}),
                                     buffer(DType::Float32, {2}, std::vector<float>{0.8F, 0.95F})});
    failure(execute(registry, "visionrt.detection.filter", inputs));
    inputs["scores"] = scores;
    inputs["scores"] = NumericArray(
        DType::Float32, 2, Layout::BatchRagged,
        {buffer(DType::Float32, {0}, std::vector<float>{}),
         buffer(DType::Float32, {3}, std::vector<float>{0.8F, std::numeric_limits<float>::quiet_NaN(), 0.7F})});
    failure(execute(registry, "visionrt.detection.filter", inputs));
    inputs["scores"]  = scores;
    inputs["classes"] = NumericArray(DType::Int64, 2, Layout::BatchRagged,
                                     {buffer(DType::Int64, {0}, std::vector<std::int64_t>{}),
                                      buffer(DType::Int64, {3}, std::vector<std::int64_t>{1, -1, 1})});
    failure(execute(registry, "visionrt.detection.filter", inputs));
    inputs["classes"] = classes;
    inputs["rois"]    = RoiBatch({
        {},
        {Roi::circle(2, 2, 1), rois.images()[1][1], rois.images()[1][2]}
    });
    failure(execute(registry, "visionrt.detection.filter", inputs));
}

void incompleteInstallation(ToolRegistry &completeRegistry)
{
    ToolRegistry registry;
    registry.addDefinition(*completeRegistry.find("visionrt.image.preprocess"));
    registry.setPluginLoader(completeRegistry.pluginLoader());
    assert(registry.availability("visionrt.image.read").state == ToolAvailability::DefinitionMissing);
    cv::Mat pixels(1, 2, CV_8UC1);
    pixels.at<unsigned char>(0, 0) = 127;
    pixels.at<unsigned char>(0, 1) = 128;
    const auto result              = execute(registry, "visionrt.image.preprocess",
                                             {
                                                 {   "images", ImageBatch({OwnedImage::take(std::move(pixels))})},
                                                 {"operation",                                 Utf8("threshold")}
    });
    success(result);
    const auto &output = std::get<ImageBatch>(result.outputs.at("images")).images()[0].readOnlyMat();
    assert(output.at<unsigned char>(0, 0) == 0 && output.at<unsigned char>(0, 1) == 255);
}
} // namespace

int main(int argc, char **argv)
{
    assert(argc == 2);
    ToolRegistry registry;
    registry.setPluginLoader(std::make_shared<PluginLoader>());
    registry.registerPackage(readToolsPackage(std::filesystem::u8path(argv[1])));
    readImages(registry);
    projection(registry);
    crop(registry);
    preprocess(registry);
    filter(registry);
    incompleteInstallation(registry);
}
