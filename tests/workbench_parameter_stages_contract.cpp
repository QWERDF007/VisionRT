#include "WorkbenchFixtures.h"
#include "core/DefinitionJson.h"
#include "core/ValueJson.h"
#include "ui/AppController.h"
#include "ui/ImageViewport.h"
#include <QDir>
#include <QFile>
#include <QGuiApplication>
#include <QImage>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QQuickWindow>
#include <QTest>
#include <iostream>

using namespace visionrt;
void require(bool value, const char *message) { if (!value) throw std::runtime_error(message); }

int main(int argc, char **argv)
try
{
    QGuiApplication application(argc, argv);
    testing::WorkbenchInstallation installation;
    const auto root = installation.directory();
    QDir().mkpath(root + "/tools-package/tools/custom/stages");
    QDir().mkpath(root + "/tools-package/python/custom/stages");
    {
        QFile index(root + "/tools-package/module_index.json");
        require(index.open(QIODevice::ReadOnly), "index read");
        auto doc = QJsonDocument::fromJson(index.readAll()).object();
        index.close();
        auto tools = doc.value("tools").toArray();
        tools.append(QJsonObject{{"tool_id", "stages"}, {"definition", "tools/custom/stages/tool.json"}});
        doc["tools"] = tools;
        require(index.open(QIODevice::WriteOnly | QIODevice::Truncate), "index write");
        index.write(QJsonDocument(doc).toJson());
    }
    QFile script(root + "/tools-package/python/custom/stages/tool.py");
    require(script.open(QIODevice::WriteOnly), "script write");
    script.write("def run(values):\n    return {'outputs': {0: 0, 1: 'ok'}, 'detail': ''}\n");
    script.close();
    ToolDefinition tool;
    tool.nameEn = "stages"; tool.nameZh = "参数阶段";
    tool.kind = ToolKind::PythonFunction;
    tool.entry = {"stages", "tool.py", "run"};
    {
        PortDefinition image; image.id = 0; image.nameZh = "图像"; image.type = {Type::Image}; image.imageVisible = true;
        PortDefinition roi; roi.id = 1; roi.nameZh = "区域"; roi.type = {Type::Circle}; roi.image = {PortStage::Init, 0};
        PortDefinition factor; factor.id = 2; factor.nameZh = "系数"; factor.type = {Type::Int}; factor.defaultValue = std::int64_t{10};
        tool.initParams = {image, roi, factor};
    }
    {
        PortDefinition trainInt; trainInt.id = 0; trainInt.nameZh = "训练阈值"; trainInt.type = {Type::Int}; trainInt.defaultValue = std::int64_t{5};
        PortDefinition roi; roi.id = 1; roi.nameZh = "区域"; roi.type = {Type::Circle}; roi.image = {PortStage::Inputs, 1};
        tool.trainParams = {trainInt, roi};
    }
    {
        PortDefinition image; image.id = 1; image.nameZh = "图像"; image.type = {Type::Image}; image.imageVisible = true;
        PortDefinition roi; roi.id = 2; roi.nameZh = "区域"; roi.type = {Type::Circle}; roi.image = {PortStage::Inputs, 1};
        tool.inputs = {image, roi};
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
    settle();
    app.addNode("ReadImage", 0, 0); settle();
    const auto readerNode = app.selectedNode();
    require(!readerNode.isEmpty(), "ReadImage discovery");
    require(app.setParameter(5, false, QStringLiteral("1"))
                && app.setParameterPath(1, false, QUrl::fromLocalFile(file)),
            "ReadImage path setup");
    app.runNode(); settle();
    app.addNode("stages", 0, 1); settle();
    require(!app.selectedNode().isEmpty() && app.selectedNode() != readerNode, "generic tool discovery");
    for (const auto type : {Type::Direction, Type::CoordinateFrame2D, Type::TrainingSet})
    {
        bool offered = false;
        for (const auto &entry : app.parameterTypes()) offered |= entry.toMap().value("name").toInt() == static_cast<int>(type);
        require(offered, "type selector offers all ordinary parameter types");
    }
    // Image parameters stay upstream-only; ordinary Init/Train parameters toggle freely between literal and upstream.
    require(!app.setParameterMode(0, false, static_cast<int>(PortStage::Init)), "init image must reject literal mode");
    require(!app.setParameterMode(1, false, static_cast<int>(PortStage::Inputs)), "input image must reject literal mode");
    require(app.setParameterMode(2, true, static_cast<int>(PortStage::Init)), "init non-image parameter must switch to upstream");
    settle();
    require(app.setParameterMode(2, false, static_cast<int>(PortStage::Init)), "init non-image parameter must switch back to literal");
    settle();
    require(app.setParameterMode(0, true, static_cast<int>(PortStage::Train)), "train non-image parameter must switch to upstream");
    settle();
    require(app.setParameterMode(0, false, static_cast<int>(PortStage::Train)), "train non-image parameter must switch back to literal");
    settle();
    require(app.bindInput(app.portContext(0, false, static_cast<int>(PortStage::Init)), readerNode, 2),
            "init image binds to upstream ReadImage");
    settle();
    require(app.bindInput(app.portContext(1, false, static_cast<int>(PortStage::Inputs)), readerNode, 2),
            "input image binds to upstream ReadImage");
    settle();
    for (const auto &entry : app.parameters())
    {
        const auto row = entry.toMap();
        if (row.value("type").toInt() == static_cast<int>(Type::Image))
            require(row.value("inputMode").toInt() == static_cast<int>(BindingMode::Upstream)
                        && !row.value("literal").toString().contains(QStringLiteral("\"images\"")),
                    "image parameter must stay upstream without JSON pixel summary");
    }
    QQuickWindow window;
    ImageViewport viewport(window.contentItem()); viewport.setWidth(500); viewport.setHeight(400);
    app.attachViewport(&viewport);
    for (auto stage : {PortStage::Init, PortStage::Train, PortStage::Inputs})
    {
        const int roiPort = stage == PortStage::Inputs ? 2 : 1;
        require(app.startParameterRoi(roiPort, static_cast<int>(stage), ImageViewport::DrawCircle, false),
                "ROI editor supports each parameter stage");
        const auto kind = stage == PortStage::Init ? RoiTargetKind::Initialization : stage == PortStage::Train ? RoiTargetKind::TrainingParameter : RoiTargetKind::Input;
        require(QTest::qWaitFor([&] { return app.workspaceEnabled() && app.shapeToolsAvailable() && viewport.editTarget() && viewport.hasImage()
            && viewport.editTarget()->kind == kind && viewport.editTarget()->inputPortId == static_cast<PortId>(roiPort); }, 5000), "stage has its own current canvas target");
        auto target = *viewport.editTarget();
        viewport.roiCommitted(target, makeCircle({30, 40}, 12));
        settle();
        bool found = false;
        for (const auto &entry : app.parameters())
        {
            const auto row = entry.toMap();
            if (row.value("stage").toInt() != static_cast<int>(stage) || row.value("id").toInt() != roiPort || row.value("recordScoped").toBool()) continue;
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
    require(stepped, "record integer does not use the same numbered top-level train parameter");
    const auto draftOnly = app.workspaceEnabled();
    settle();
    require(draftOnly, "stepping a training field does not reinitialize the node");
    require(app.selectTrainingDataset(2), "first dataset is selectable");
    QStringList tabs;
    for (const auto &entry : app.parameterTabs()) tabs.push_back(entry.toMap().value("name").toString());
    require(tabs == QStringList{"基本输入", "初始化", "训练", "预处理", "检测", "结果处理"}, "peer parameter tab order");
}
catch (const std::exception &error) { std::cerr << error.what() << '\n'; return 1; }
