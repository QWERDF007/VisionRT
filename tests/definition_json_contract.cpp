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
    PortDefinition regions{1, "参考边", {Type::ROI}};
    regions.scope = ParameterScope::Record;
    regions.roi = true; regions.minRegions = 2; regions.maxRegions = 2;
    authored.trainParams = {modelPath, regions};
    authored.outputs.push_back(PortDefinition{3, "模型路径", {Type::String}});
    authored.training = TrainingSpec{};
    authored.training->image = {PortStage::Inputs, 2};
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
    assert(restored.training->image == framePort);
    assert(restored.trainParams.size() == 2 && restored.trainParams.back().roi
           && restored.trainParams.back().scope == ParameterScope::Record && restored.trainParams.back().maxRegions == 2);

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
