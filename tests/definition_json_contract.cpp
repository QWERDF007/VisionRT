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
    authored.initParams.push_back(PortDefinition{0, "训练数据", {Type::String}});
    authored.training = TrainingSpec{TrainingKind::StraightEdges, 0};
    writeToolDefinitionFile(file, authored);

    auto restored = parseToolDefinitionFile(file);
    const auto count = std::ranges::find(restored.inputs, PortId{1}, &PortDefinition::id);
    assert(count != restored.inputs.end() && std::get<std::int64_t>(count->defaultValue) == 9007199254740993LL);
    assert(count->editor == EditorKind::Number && count->tab == ParameterTab::Detection);
    assert(std::ranges::find(restored.inputs, PortId{2}, &PortDefinition::id)->imageVisible);
    const auto result = std::ranges::find(restored.outputs, PortId{2}, &PortDefinition::id);
    assert(result != restored.outputs.end() && result->sameGeometryAs == output.sameGeometryAs && result->colorOrder == ColorOrder::BGR);
    assert(restored.training && restored.training->kind == TrainingKind::StraightEdges);

    restored.nameZh = "编辑后的声明";
    restored.training->kind = TrainingKind::ColorSamples;
    writeToolDefinitionFile(file, restored);
    const auto edited = parseToolDefinitionFile(file);
    assert(edited.nameZh == "编辑后的声明" && edited.training->kind == TrainingKind::ColorSamples);
    assert(std::get<std::int64_t>(std::ranges::find(edited.inputs, PortId{1}, &PortDefinition::id)->defaultValue) == 9007199254740993LL);
    std::cout << "declaration save/reopen preserves typed metadata and exact integers\n";
    return 0;
}
catch (const std::exception &error)
{
    std::cerr << error.what() << '\n';
    return 1;
}
