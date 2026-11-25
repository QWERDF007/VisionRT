#include "core/DefinitionJson.h"
#include "core/ValueJson.h"
#include "ui/AppController.h"
#include "ui/ImageViewport.h"
#include <QDir>
#include <QFile>
#include <QGuiApplication>
#include <QImage>
#include <QQuickWindow>
#include <QTemporaryDir>
#include <QTest>
#include <iostream>

using namespace visionrt;
void require(bool value, const char *message) { if (!value) throw std::runtime_error(message); }

int main(int argc, char **argv)
try
{
    QGuiApplication application(argc, argv);
    QTemporaryDir installation;
    const auto root = installation.path();
    QDir().mkpath(root + "/tools-package/tools/custom/stages");
    QDir().mkpath(root + "/tools-package/python/custom/stages");
    QFile index(root + "/tools-package/module_index.json");
    require(index.open(QIODevice::WriteOnly), "index write");
    index.write(R"({"schema_version":1,"variant":"test","tools":[{"tool_id":"stages","definition":"tools/custom/stages/tool.json"}],"modules":[],"runtime_files":[]})");
    index.close();
    QFile script(root + "/tools-package/python/custom/stages/tool.py");
    require(script.open(QIODevice::WriteOnly), "script write");
    script.write("def run(values):\n    return {'outputs': {0: 0, 1: 'ok'}, 'detail': ''}\n");
    script.close();
    ToolDefinition tool;
    tool.nameEn = "stages"; tool.nameZh = "参数阶段";
    tool.kind = ToolKind::PythonFunction;
    tool.entry = {"stages", "tool.py", "run"};
    for (auto stage : {PortStage::Init, PortStage::Train, PortStage::Inputs})
    {
        auto &ports = stage == PortStage::Init ? tool.initParams : stage == PortStage::Train ? tool.trainParams : tool.inputs;
        PortDefinition image; image.id = stage == PortStage::Inputs ? 1 : 0;
        image.nameZh = "图像"; image.type = {Type::Image}; image.imageVisible = true;
        PortDefinition roi; roi.id = image.id + 1; roi.nameZh = "区域"; roi.type = {Type::Circle}; roi.image = {stage, image.id};
        ports = {image, roi};
    }
    tool.training.emplace();
    for (PortId id : {2u, 3u})
    {
        PortDefinition dataset; dataset.id = id; dataset.nameZh = "样本"; dataset.type = {Type::TrainingSet};
        tool.trainParams.push_back(dataset);
        tool.training->datasets.push_back({id, PortRef{PortStage::Inputs, 1}, 0, {}});
    }
    tool.training->datasets.back().fields = {{0, "计数", {Type::Int}, std::int64_t{5}}};
    writeToolDefinitionFile(std::filesystem::path((root + "/tools-package/tools/custom/stages/tool.json").toStdWString()), tool);
    QImage pixels(100, 80, QImage::Format_RGB888); pixels.fill(QColor(20, 80, 180));
    const auto file = root + "/参数图像.png";
    require(pixels.save(file), "image fixture");
    AppController app(1, root);
    const auto settle = [&] { require(QTest::qWaitFor([&] { return app.workspaceEnabled()
        && app.pythonEnvironmentPhase() != PythonEnvironmentPhase::Probing; }, 10000), "controller did not settle"); };
    settle(); app.addNode("stages", 0, 0); settle();
    require(!app.selectedNode().isEmpty(), "generic tool discovery");
    for (const auto type : {Type::Direction, Type::CoordinateFrame2D, Type::TrainingSet})
    {
        bool offered = false;
        for (const auto &entry : app.parameterTypes()) offered |= entry.toMap().value("name").toInt() == static_cast<int>(type);
        require(offered, "type selector offers all ordinary parameter types");
    }
    QQuickWindow window;
    ImageViewport viewport(window.contentItem()); viewport.setWidth(500); viewport.setHeight(400);
    app.attachViewport(&viewport);
    for (auto stage : {PortStage::Init, PortStage::Train, PortStage::Inputs})
    {
        const int image = stage == PortStage::Inputs ? 1 : 0;
        require(app.loadParameterImage(image, static_cast<int>(stage), QUrl::fromLocalFile(file), {}), "image editor supports each parameter stage");
        settle();
        for (const auto &entry : app.parameters())
        {
            const auto row = entry.toMap();
            if (row.value("type").toInt() == static_cast<int>(Type::Image))
                require(row.value("literal").toString().size() < 512, "parameter metadata summarizes pixels instead of expanding their storage");
        }
        require(app.startParameterRoi(image + 1, static_cast<int>(stage), ImageViewport::DrawCircle, false), "ROI editor supports each parameter stage");
        const auto kind = stage == PortStage::Init ? RoiTargetKind::Initialization : stage == PortStage::Train ? RoiTargetKind::TrainingParameter : RoiTargetKind::Input;
        require(QTest::qWaitFor([&] { return app.workspaceEnabled() && app.shapeToolsAvailable() && viewport.editTarget() && viewport.hasImage()
            && viewport.editTarget()->kind == kind && viewport.editTarget()->inputPortId == static_cast<PortId>(image + 1); }, 5000), "stage has its own current canvas target");
        auto target = *viewport.editTarget();
        viewport.roiCommitted(target, makeCircle({30, 40}, 12));
        settle();
        bool found = false;
        for (const auto &entry : app.parameters())
        {
            const auto row = entry.toMap();
            if (row.value("stage").toInt() != static_cast<int>(stage) || row.value("id").toInt() != image + 1 || row.value("recordScoped").toBool()) continue;
            const auto value = ValueJson::parse(row.value("literal").toString(), {Type::Circle});
            const auto &groups = std::get<Circle>(value).get();
            found = groups.size() == 1 && groups.front().size() == 1;
        }
        require(found, "canvas writes the selected stage without changing the same numbered peer");
        app.endRoiPreview();
    }
    require(app.selectTrainingDataset(3), "second dataset is selectable");
    require(app.addTrainingRecord(3), "an imageless dataset record is editable");
    require(app.stepIntegerParameter(0, false, 1, "5", static_cast<int>(PortStage::Train), app.selectedTrainingDraftRecord()), "record integer stepping uses its own field declaration");
    bool stepped = false;
    for (const auto &entry : app.parameters())
    {
        const auto row = entry.toMap();
        if (row.value("recordScoped").toBool() && row.value("id").toInt() == 0) stepped = row.value("valueText").toString() == "6";
    }
    require(stepped, "record integer does not use the same numbered top-level image parameter");
    const auto draftOnly = app.workspaceEnabled();
    settle();
    require(draftOnly, "stepping a training field does not reinitialize the node");
    require(app.selectTrainingDataset(2), "first dataset is selectable");
    QStringList tabs;
    for (const auto &entry : app.parameterTabs()) tabs.push_back(entry.toMap().value("name").toString());
    require(tabs == QStringList{"基本输入", "初始化", "训练", "预处理", "检测", "结果处理"}, "peer parameter tab order");
}
catch (const std::exception &error) { std::cerr << error.what() << '\n'; return 1; }
