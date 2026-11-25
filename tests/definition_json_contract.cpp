#include "core/DefinitionJson.h"
#include <QTemporaryDir>
#include <algorithm>
#include <cassert>
#include <iostream>

using namespace visionrt;

int main()
try
{
    QTemporaryDir directory;
    assert(directory.isValid());
    const auto file = std::filesystem::path(directory.path().toStdWString()) / L"声明.json";
    ToolDefinition generic;
    generic.nameEn = "test.generic_training";
    generic.nameZh = "通用训练参数";
    generic.trainParams = {{0, "图像", {Type::Image}}, {1, "方向", {Type::Direction}},
                           {2, "第一组区域", {Type::Rectangle}}, {3, "第二组区域", {Type::Circle}},
                           {4, "训练集", {Type::TrainingSet}}};
    generic.initParams = {{0, "初始化图像", {Type::Image}}, {1, "初始化方向", {Type::Direction}}};
    generic.initParams.front().defaultValue = DataRef<Image>(Image{cv::Mat(2, 3, CV_16UC1, cv::Scalar(1000))});
    generic.trainParams.front().colorOrder = ColorOrder::RGB;
    writeToolDefinitionFile(file, generic);
    const auto genericRestored = parseToolDefinitionFile(file);
    assert(genericRestored.trainParams.size() == generic.trainParams.size());
    assert(genericRestored.initParams.front().tab == ParameterTab::Initialization);
    assert(std::get<DataRef<Image>>(genericRestored.initParams.front().defaultValue).get().front().at<std::uint16_t>(1, 2) == 1000);
    assert(genericRestored.trainParams.front().tab == ParameterTab::Training);
    assert(genericRestored.trainParams.front().colorOrder == ColorOrder::RGB);
    assert(nextFreePortId(ToolDefinition{}, PortStage::Train) == 0);
    assert(toolTabs().front().id == ParameterTab::BasicInput);
    assert(toolTabs()[1].id == ParameterTab::Initialization
           && toolTabs()[2].id == ParameterTab::Training);
    ToolDefinition authored;
    authored.nameEn = "test.persisted";
    authored.nameZh = "声明读写";
    authored.version = "1.0.0";
    authored.kind = ToolKind::Cpp;
    PortDefinition threshold{1, "阈值", {Type::Int}};
    threshold.defaultValue = std::int64_t{9007199254740993LL};
    threshold.editor = EditorKind::Number;
    threshold.tab = ParameterTab::Detection;
    authored.inputs.push_back(threshold);
    PortDefinition image{2, "输入图像", {Type::Image}};
    image.imageVisible = true;
    authored.inputs.push_back(image);
    PortDefinition output{2, "输出图像", {Type::Image}};
    output.sameGeometryAs = PortRef{PortStage::Inputs, 2};
    output.colorOrder = ColorOrder::BGR;
    output.imageVisible = true;
    authored.outputs.push_back(output);
    PortDefinition modelPath{2, "模型路径", {Type::String}};
    PortDefinition regions{1, "参考边", {Type::Rectangle}};
    regions.minRegions = 2; regions.maxRegions = 2;
    authored.trainParams = {modelPath, PortDefinition{1, "样本", {Type::TrainingSet}}};
    PortDefinition polarity{5, "极性", {Type::String}};
    polarity.defaultValue = std::string("any");
    polarity.options = {PortOption{std::string("any"), "任意"}, PortOption{std::string("dark_to_light"), "暗到亮"}};
    authored.trainParams.push_back(polarity);
    authored.outputs.push_back(PortDefinition{3, "模型路径", {Type::String}});
    authored.training = TrainingSpec{};
    authored.training->datasets.push_back({1, PortRef{PortStage::Inputs, 2}, 0, {regions}});
    TrainingArtifactSpec artifact;
    artifact.id         = "model";
    artifact.sourcePort = 3;
    artifact.format     = "sample.model";
    artifact.version    = 1;
    artifact.target     = {PortStage::Train, 2};
    authored.training->artifacts.push_back(std::move(artifact));
    writeToolDefinitionFile(file, authored);

    auto restored = parseToolDefinitionFile(file);
    const auto count = std::ranges::find(restored.inputs, PortId{1}, &PortDefinition::id);
    assert(count != restored.inputs.end() && std::get<std::int64_t>(count->defaultValue) == 9007199254740993LL);
    assert(count->editor == EditorKind::Number && count->tab == ParameterTab::Detection);
    assert(std::ranges::find(restored.inputs, PortId{2}, &PortDefinition::id)->imageVisible);
    const auto result = std::ranges::find(restored.outputs, PortId{2}, &PortDefinition::id);
    assert(result != restored.outputs.end() && result->sameGeometryAs == output.sameGeometryAs && result->colorOrder == ColorOrder::BGR);
    const PortRef framePort{PortStage::Inputs, 2};
    assert(restored.training->datasets.front().image == framePort);
    assert(restored.trainParams.size() == 3 && restored.training->datasets.front().fields.front().maxRegions == 2);
    assert(restored.trainParams.back().options.back().label == "暗到亮");

    restored.nameZh = "编辑后的声明";
    writeToolDefinitionFile(file, restored);
    const auto edited = parseToolDefinitionFile(file);
    assert(std::get<std::int64_t>(std::ranges::find(edited.inputs, PortId{1}, &PortDefinition::id)->defaultValue) == 9007199254740993LL);
    std::cout << "declaration save/reopen preserves typed metadata and exact integers\n";
    return 0;
}
catch (const std::exception &error)
{
    std::cerr << error.what() << '\n';
    return 1;
}
