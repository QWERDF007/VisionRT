#include "core/ToolRegistry.h"
#include "core/StatusCodes.h"
#include "ToolResultAssertions.h"
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
    // 声明的每个端口都有编号：缺省值按编号补齐，名字从不参与查找。
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
    if (!testview::status(result))
        std::cerr << testview::status(result).message << '\n';
    assert((testview::status(result)));
}

void failure(const ToolResult &result)
{
    assert((!testview::status(result)));
    assert((!testview::status(result).message.empty()));
    // The two status keys are the tool's own answer and are never Missing.
    for (const auto &[id, value] : result.outputs)
        if (id != kStatusCodePort && id != kStatusMessagePort)
            assert(isMissing(value));
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
                    {1, std::string("中文目录")},
                    {2, false},
                    {3, std::string("循环")},
                    {4, std::string("顺序（下一张）")},
                    {5, std::int64_t(2)},
                    {6, false}
    };
    auto result = execute(registry, "visionrt.image.read", inputs, services);
    success(result);
    const auto &images = std::get<DataRef<Image>>(result.outputs.at(2));
    assert((images.get().size() == 2));
    assert((images.get()[0].at<unsigned char>(0, 0) == 11));
    assert((images.get()[1].at<unsigned char>(0, 0) == 22));
    const auto paths = nestedOf<NestedVector<std::string, 1>>(std::get<Array>(result.outputs.at(3)));
    assert((paths[0].ends_with("/a.png") && paths[1].ends_with("/b.png")));
    inputs[5] = std::int64_t{-1};
    result = execute(registry, "visionrt.image.read", inputs, services);
    failure(result);
    assert((testview::status(result).message.find(portLabel(5)) != std::string::npos));
    inputs[5] = std::int64_t{1};
    result          = execute(registry, "visionrt.image.read", inputs, services);
    success(result);
    assert((std::get<DataRef<Image>>(result.outputs.at(2)).get().size() == 1));
    saveImage(folder + "/a.png", 99);
    inputs[6] = true;
    result = execute(registry, "visionrt.image.read", inputs, services);
    success(result);
    assert((std::get<DataRef<Image>>(result.outputs.at(2)).get()[0].at<unsigned char>(0, 0) == 99));
    inputs[6] = false;
    QFile corrupt(folder + "/c.png");
    assert((corrupt.open(QIODevice::WriteOnly)));
    assert((corrupt.write("corrupt") == 7));
    corrupt.close();
    inputs[1] = std::string("中文目录/c.png");
    result          = execute(registry, "visionrt.image.read", inputs, services);
    failure(result);
    assert((testview::status(result).message.find("c.png") != std::string::npos));
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
                         {1, std::string("中文目录/oversized.bmp")},
                         {2, false},
                         {3, std::string("循环")},
                         {4, std::string("顺序（下一张）")},
                         {5, std::int64_t(1)},
                         {6, false}
                     },
                     services);
    failure(result);
    // The readable reason is the message; the decoder text is a detail the diagnostic keeps apart.
    const auto decodeStatus = testview::status(result);
    assert((decodeStatus.code == 101004));
    assert((decodeStatus.message.find("图像解码失败") != std::string::npos));
    assert((decodeStatus.message.find("oversized.bmp") != std::string::npos));
    assert((decodeStatus.message.find("批次索引 0") != std::string::npos));
    assert((decodeStatus.message.find("OpenCV") == std::string::npos));
    assert((decodeStatus.detail == decoderDetail));
    inputs[1] = std::string("中文目录/a.png");
    success(execute(registry, "visionrt.image.read", inputs, services));
    inputs[1] = std::string("absent.png");
    failure(execute(registry, "visionrt.image.read", inputs, services));
    assert((QDir().mkpath(directory.path() + "/empty")));
    result = execute(registry, "visionrt.image.read",
                     {
                         {1,     std::string("empty")},
                         {2, false},
                         {3, std::string("循环")},
                         {4, std::string("顺序（下一张）")},
                         {5, std::int64_t(1)},
                         {6, false}
                     },
                     services);
    // 空目录没有任何可用文件，按规范明确失败而不是发布空批次。
    failure(result);
    assert((testview::status(result).message.find("没有可用图像") != std::string::npos));
    const auto unicodeFolder = directory.path() + "/unicode_sort";
    assert((QDir().mkpath(unicodeFolder)));
    const auto supplementaryName = QString::fromUtf8("\xF0\x90\x80\x80.png"); // U+10000
    const auto bmpName           = QString::fromUtf8("\xEE\x80\x80.png");     // U+E000
    saveImage(unicodeFolder + "/" + supplementaryName, 100);
    saveImage(unicodeFolder + "/" + bmpName, 14);
    const Values unicodeInputs{
        {1, std::string("unicode_sort")},
        {2, false},
        {3, std::string("循环")},
        {4, std::string("顺序（下一张）")},
        {5, std::int64_t(2)},
        {6, false}
    };
    result = execute(registry, "visionrt.image.read", unicodeInputs, services);
    success(result);
    const auto &orderedImages = std::get<DataRef<Image>>(result.outputs.at(2));
    assert((orderedImages.get().size() == 2));
    assert((orderedImages.get()[0].at<unsigned char>(0, 0) == 14));
    assert((orderedImages.get()[1].at<unsigned char>(0, 0) == 100));
    const auto orderedPaths = nestedOf<NestedVector<std::string, 1>>(std::get<Array>(result.outputs.at(3)));
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
                                    {1, images}
    });
    success(result);
    const auto &rois = std::get<DataRef<ROI>>(result.outputs.at(3));
    assert((rois.get().size() == 2 && rois.get()[0].empty()));
    assert((rois.get()[1].size() == 2));
    const std::array<float, 6> first{2, 10, 0, 20, 64, 0};
    const std::array<float, 6> second{2, 55, 0, 20, 64, 0};
    assert((std::equal(first.begin(), first.end(), rois.get()[1][0].begin())));
    assert((std::equal(second.begin(), second.end(), rois.get()[1][1].begin())));
    assert((&std::get<DataRef<Image>>(result.outputs.at(2)).get() == &images.get()));
    result = execute(registry, "visionrt.roi.projection",
                     {
                         {    1,           images},
                         { 7, std::int64_t{25}},
                         {6, std::int64_t{60}},
                         {   8, std::int64_t{12}}
    });
    success(result);
    const auto merged = std::get<DataRef<ROI>>(result.outputs.at(3)).get()[1][0];
    assert((merged[1] == 0 && merged[3] == 87));
    result = execute(registry, "visionrt.roi.projection",
                     {
                         {   1,              images},
                         {     2,    std::string("y")},
                         { 3, std::string("dark")},
                         {4,     std::int64_t{0}}
    });
    success(result);
    assert((std::get<DataRef<ROI>>(result.outputs.at(3)).get()[1].empty()));
    result = execute(registry, "visionrt.roi.projection",
                     {
                         {    1,            images},
                         {      2,  std::string("y")},
                         { 4, std::int64_t{255}},
                         {5,  std::int64_t{40}}
    });
    success(result);
    const auto vertical = std::get<DataRef<ROI>>(result.outputs.at(3)).get()[1][0];
    assert((vertical[1] == 0 && vertical[2] == 0 && vertical[3] == 96 && vertical[4] == 64));
    result = execute(registry, "visionrt.roi.projection",
                     {
                         {    1,           images},
                         {5, std::int64_t{65}}
    });
    success(result);
    assert((std::get<DataRef<ROI>>(result.outputs.at(3)).get()[1].empty()));
    failure(execute(registry, "visionrt.roi.projection",
                    {
                        {1, Missing{}}
    }));
    failure(execute(registry, "visionrt.roi.projection",
                    {
                        {   1,            images},
                        {4, std::int64_t{256}}
    }));
}

void crop(ToolRegistry &registry)
{
    const auto images = fixtureImages();
    auto       result = execute(registry, "visionrt.image.crop",
                                {
                              {1,                                     images},
                              {  2, DataRef<ROI>(ROI{{{2, 10, 0, 20, 64, 0}}})}
    });
    success(result);
    const auto &crops = std::get<DataRef<Image>>(result.outputs.at(2));
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
    assert((!result.outputs.contains(99)));   // 未声明的编号不会出现在结果里
    const auto origins = nestedOf<NestedVector<double, 2>>(std::get<Array>(result.outputs.at(3)));
    assert(((origins
             == NestedVector<double, 2>{
                 {10, 0},
                 {10, 0}
    })));
    const DataRef<Image> mixed(
        Image{(cv::Mat(8, 10, CV_8UC1, cv::Scalar(11))), (cv::Mat(5, 6, CV_8UC1, cv::Scalar(29)))});
    result = execute(registry, "visionrt.image.crop",
                     {
                         {1,                                   mixed},
                         {  2, DataRef<ROI>(ROI{{{2, 4, 3, 4, 4, 0}}})}
    });
    success(result);
    const auto &mixedCrops = std::get<DataRef<Image>>(result.outputs.at(2)).get();
    assert((mixedCrops.size() == 2 && mixedCrops[0].rows == 4 && mixedCrops[0].cols == 4));
    assert((mixedCrops[1].rows == 2 && mixedCrops[1].cols == 2));
    for (std::size_t index = 0; index < mixedCrops.size(); ++index)
    {
        assert((cv::countNonZero(mixedCrops[index] != (index == 0 ? 11 : 29)) == 0));
        assert((mixedCrops[index].u == mixed.get()[index].u));
        assert((mixedCrops[index].step == mixed.get()[index].step));
    }
    const auto mixedOrigins = nestedOf<NestedVector<double, 2>>(std::get<Array>(result.outputs.at(3)));
    assert(((mixedOrigins
             == NestedVector<double, 2>{
                 {4, 3},
                 {4, 3}
    })));
    failure(execute(registry, "visionrt.image.crop",
                    {
                        {1,                                   mixed},
                        {  2, DataRef<ROI>(ROI{{{2, 7, 6, 2, 2, 0}}})}
    }));
    const DataRef<Image> solid(Image{(cv::Mat(5, 5, CV_8UC1, cv::Scalar(17)))});
    const auto run = [&](PortValue rectangle)
    {
        return execute(registry, "visionrt.image.crop", {{1, solid}, {2, std::move(rectangle)}});
    };
    result = run(DataRef<ROI>(ROI{{{2, 1, 1, 2, 2, 45}}}));
    success(result);
    const auto &rotated = std::get<DataRef<Image>>(result.outputs.at(2)).get()[0];
    assert((rotated.rows == 4 && rotated.cols == 4));
    assert((cv::countNonZero(rotated) == 4));
    assert((rotated.at<unsigned char>(1, 1) == 17 && rotated.at<unsigned char>(0, 0) == 0));
    result = run(DataRef<ROI>(ROI{{{2, -1, -1, 3, 3, 0}}}));
    success(result);
    const auto &clipped = std::get<DataRef<Image>>(result.outputs.at(2)).get()[0];
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
                         {1,                 DataRef<Image>(Image{})},
                         {  2, DataRef<ROI>(ROI{{{2, 0, 0, 2, 2, 0}}})}
    });
    success(result);
    assert((std::get<DataRef<Image>>(result.outputs.at(2)).get().empty()));
    assert(((nestedOf<NestedVector<double, 2>>(std::get<Array>(result.outputs.at(3))).empty())));
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
                                          {1, images}
    });
    success(result);
    assert((&std::get<DataRef<Image>>(result.outputs.at(2)).get() == &images.get()));
    result = execute(registry, "visionrt.image.preprocess",
                     {
                         {   1,              images},
                         {2, std::string("gray")}
    });
    success(result);
    const auto &grayscale = std::get<DataRef<Image>>(result.outputs.at(2)).get()[0];
    assert((grayscale.rows == 1 && grayscale.cols == 3 && grayscale.channels() == 1));
    assert((grayscale.at<unsigned char>(0, 0) == 76));
    result = execute(registry, "visionrt.image.preprocess",
                     {
                         {   1,                   images},
                         {2, std::string("threshold")},
                         {4,        std::int64_t{128}}
    });
    success(result);
    const auto &binary = std::get<DataRef<Image>>(result.outputs.at(2)).get()[0];
    assert((binary.at<unsigned char>(0, 0) == 0));
    assert((binary.at<unsigned char>(0, 1) == 255 && binary.at<unsigned char>(0, 2) == 255));
    cv::Mat impulse(3, 3, CV_8UC1, cv::Scalar(0));
    impulse.at<unsigned char>(1, 1) = 160;
    const DataRef<Image> impulseBatch(Image{(std::move(impulse))});
    result = execute(registry, "visionrt.image.preprocess",
                     {
                         {   1,            impulseBatch},
                         {2, std::string("gaussian")}
    });
    success(result);
    const auto &blurred = std::get<DataRef<Image>>(result.outputs.at(2)).get()[0];
    assert((cv::countNonZero(blurred != 40) == 0));
    assert((impulseBatch.get()[0].at<unsigned char>(1, 1) == 160));
    failure(execute(registry, "visionrt.image.preprocess",
                    {
                        {   1,                  images},
                        {2, std::string("gaussian")},
                        {   3,         std::int64_t{2}}
    }));
    const DataRef<Image> invalid(Image{(cv::Mat(2, 2, CV_16UC1, cv::Scalar(1)))});
    failure(execute(registry, "visionrt.image.preprocess",
                    {
                        {1, invalid}
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
    const Array        scores = Array::fromNested(NestedVector<float, 2>{
        std::vector<float>{},
        std::vector<float>{0.8F, 0.95F, 0.7F}
    });
    const Array        classes = Array::fromNested(NestedVector<std::int64_t, 2>{
        std::vector<std::int64_t>{},
        std::vector<std::int64_t>{1, 2, 1}
    });
    const Array        names = Array::fromNested(NestedVector<std::string, 2>{
        {},
        {"first", "second", "third"}
    });
    const Array        allowed = Array::fromNested(std::vector<std::int64_t>{1});
    Values             inputs{
        {         1,  images},
        {           2,    rois},
        {         3,  scores},
        {        4, classes},
        {    5,   names},
        {      6,     0.6},
        {       7,    64.0},
        {8, allowed}
    };
    auto result = execute(registry, "visionrt.detection.filter", inputs);
    success(result);
    const auto counts = nestedOf<NestedVector<std::int64_t, 1>>(std::get<Array>(result.outputs.at(7)));
    assert((counts.size() == 2 && counts[0] == 0 && counts[1] == 2));
    const auto &keptRois = std::get<DataRef<ROI>>(result.outputs.at(3));
    assert((keptRois.get()[0].empty()));
    assert((keptRois.get()[1][0][1] == 0 && keptRois.get()[1][1][1] == 25));
    const auto keptScores = nestedOf<NestedVector<float, 2>>(std::get<Array>(result.outputs.at(4)))[1];
    assert((keptScores[0] == 0.8F && keptScores[1] == 0.7F));
    const auto keptClasses = nestedOf<NestedVector<std::int64_t, 2>>(std::get<Array>(result.outputs.at(5)))[1];
    assert((keptClasses[0] == 1 && keptClasses[1] == 1));
    const auto keptNames = nestedOf<NestedVector<std::string, 2>>(std::get<Array>(result.outputs.at(6)))[1];
    assert((keptNames[0] == "first" && keptNames[1] == "third"));
    assert((&std::get<DataRef<Image>>(result.outputs.at(2)).get() == &images.get()));
    inputs[7] = 0.0;
    result             = execute(registry, "visionrt.detection.filter", inputs);
    success(result);
    assert((nestedOf<NestedVector<std::int64_t, 1>>(std::get<Array>(result.outputs.at(7)))[1] == 2));
    inputs[8] = Array::fromNested(std::vector<std::int64_t>{});
    inputs[7] = 64.0;
    result             = execute(registry, "visionrt.detection.filter", inputs);
    success(result);
    assert((nestedOf<NestedVector<std::string, 2>>(std::get<Array>(result.outputs.at(6)))[1][1] == "third"));
    assert((nestedOf<NestedVector<std::int64_t, 1>>(std::get<Array>(result.outputs.at(7)))[1] == 2));
    inputs[7]  = 0.0;
    inputs[6] = 0.9;
    result              = execute(registry, "visionrt.detection.filter", inputs);
    success(result);
    assert((nestedOf<NestedVector<std::int64_t, 1>>(std::get<Array>(result.outputs.at(7)))[1] == 1));
    assert((nestedOf<NestedVector<std::string, 2>>(std::get<Array>(result.outputs.at(6)))[1][0] == "second"));
    inputs[8] = allowed;
    inputs[7]        = 64.0;
    inputs[6]       = 0.9;
    result                    = execute(registry, "visionrt.detection.filter", inputs);
    success(result);
    assert((std::get<DataRef<ROI>>(result.outputs.at(3)).get()[1].empty()));
    assert((nestedOf<NestedVector<std::string, 2>>(std::get<Array>(result.outputs.at(6)))[1].size() == 0));
    inputs[3] = Array::fromNested(NestedVector<float, 2>{
        std::vector<float>{},
        std::vector<float>{0.8F, 0.95F}
    });
    failure(execute(registry, "visionrt.detection.filter", inputs));
    inputs[3] = scores;
    inputs[3] = Array::fromNested(NestedVector<float, 2>{
        std::vector<float>{},
        std::vector<float>{0.8F, std::numeric_limits<float>::quiet_NaN(), 0.7F}
    });
    failure(execute(registry, "visionrt.detection.filter", inputs));
    inputs[3]  = scores;
    inputs[4] = Array::fromNested(NestedVector<std::int64_t, 2>{
        std::vector<std::int64_t>{},
        std::vector<std::int64_t>{1, -1, 1}
    });
    failure(execute(registry, "visionrt.detection.filter", inputs));
    inputs[4] = classes;
    inputs[2]    = DataRef<ROI>(ROI{
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
                                    {1, DataRef<Image>(Image{(std::move(pixels))})},
                                    {2, std::string("threshold")}
    });
    success(result);
    const auto &output = std::get<DataRef<Image>>(result.outputs.at(2)).get()[0];
    assert((output.at<unsigned char>(0, 0) == 0 && output.at<unsigned char>(0, 1) == 255));
}
} // namespace

int main(int argc, char **argv)
{
    // A contract failure is a message, not a silent abort: a test that dies without one hides
    // which declaration or value broke.
    try
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
        return 0;
    }
    catch (const std::exception &error)
    {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
