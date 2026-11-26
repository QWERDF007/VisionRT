#include "WorkbenchFixtures.h"
#include "core/ValueJson.h"
#include "ui/AppController.h"
#include "ui/Color.h"
#include "ui/ImageViewport.h"
#include "ui/OutputModel.h"
#include "ui/UiTypes.h"

#include <QDir>
#include <QGuiApplication>
#include <QQmlComponent>
#include <QQmlContext>
#include <QQmlEngine>
#include <QQuickItemGrabResult>
#include <QQuickStyle>
#include <QQuickWindow>
#include <QTest>
#include <iostream>

using namespace visionrt;

namespace {
void require(bool condition, const char *message)
{
    if (!condition) throw std::runtime_error(message);
}
QQuickItem *button(QQuickItem *parent, const QString &text)
{
    for (auto *item : parent->childItems())
    {
        if (item->property("text").toString() == text && item->property("checked").isValid()) return item;
        if (auto *found = button(item, text)) return found;
    }
    return nullptr;
}

bool hasColorNear(const QImage &rendered, QPointF point, QColor color)
{
    for (int y = std::max(0, qRound(point.y()) - 4); y < std::min(rendered.height(), qRound(point.y()) + 5); ++y)
        for (int x = std::max(0, qRound(point.x()) - 4); x < std::min(rendered.width(), qRound(point.x()) + 5); ++x)
        {
            const auto pixel = rendered.pixelColor(x, y);
            if (std::abs(pixel.red() - color.red()) < 35 && std::abs(pixel.green() - color.green()) < 35
                && std::abs(pixel.blue() - color.blue()) < 35) return true;
        }
    return false;
}
}

int main(int argc, char **argv)
try
{
    QGuiApplication application(argc, argv);
    QQuickStyle::setStyle("Basic");
    registerUiTypes();
    qmlRegisterType<ImageViewport>("VisionRT.View", 1, 0, "ImageViewport");
    testing::WorkbenchInstallation installation;
    const auto project = testing::copyCableExample(VISIONRT_SOURCE_DIR, installation.directory());
    AppController app(2, installation.directory());
    const auto settle = [&]
    {
        require(QTest::qWaitFor([&] { return app.workspaceEnabled(); }, 15000), "operation did not settle");
        QTest::qWait(100);
    };
    settle();
    app.requestOpen(project);
    app.resolveUnsaved(UnsavedDecision::Discard);
    settle();
    app.selectNode("2");
    settle();
    app.runOnce();
    settle();
    QStringList tabs;
    for (const auto &tab : app.parameterTabs()) tabs.append(tab.toMap().value("name").toString());
    require(tabs == QStringList{"基本输入", "训练", "预处理", "检测", "结果处理"}, "parameter tab order differs from the contract");
    QQmlEngine engine;
    QStringList warnings;
    QObject::connect(&engine, &QQmlEngine::warnings, &engine, [&](const QList<QQmlError> &errors)
    { for (const auto &error : errors) warnings.append(error.toString()); });
    engine.addImportPath(QStringLiteral(VISIONRT_BINARY_DIR "/qml"));
    engine.rootContext()->setContextProperty("app", &app);
    for (const auto *flag : {"demoMenu", "demoFileMenu", "demoSettings", "demoSelect"})
        engine.rootContext()->setContextProperty(flag, false);
    QQmlComponent component(&engine, QUrl::fromLocalFile(QStringLiteral(VISIONRT_SOURCE_DIR "/src/ui/qml/Main.qml")));
    std::unique_ptr<QObject> owner(component.create());
    if (!owner) throw std::runtime_error(component.errorString().toStdString());
    auto *window = qobject_cast<QQuickWindow *>(owner.get());
    require(window != nullptr, "workbench window is missing");
    window->showMaximized();
    require(QTest::qWaitForWindowExposed(window), "workbench window was not exposed");
    auto *panel = window->findChild<QQuickItem *>("parameterPanel");
    auto *viewport = window->findChild<ImageViewport *>("imageViewport");
    require(panel && viewport, "parameter panel or image viewport is missing");
    auto *trainingTab = button(panel, "训练");
    require(trainingTab != nullptr, "training parameter tab is missing");
    QTest::mouseClick(window, Qt::LeftButton, {}, trainingTab->mapToScene(trainingTab->boundingRect().center()).toPoint());
    require(panel->property("currentTab").toInt() == static_cast<int>(ParameterTab::Training) && !app.trainingMode(),
            "choosing training parameters changed the image mode");
    QStringList groups;
    for (const auto &value : app.parameterGroups())
    {
        const auto group = value.toMap();
        if (group.value("tab").toInt() == static_cast<int>(ParameterTab::BasicInput)) groups.append(group.value("name").toString());
    }
    require(groups == QStringList{"流程控制", "检测图像", "检测区域"}, "basic input groups have the wrong order");
    auto *detectTab = button(panel, "检测");
    QTest::mouseClick(window, Qt::LeftButton, {}, detectTab->mapToScene(detectTab->boundingRect().center()).toPoint());
    app.setTrainingMode(true);
    settle();
    require(panel->property("currentTab").toInt() == static_cast<int>(ParameterTab::Detection),
            "choosing the training image changed the parameter tab");
    const auto record = app.selectedTrainingDraftRecord();
    require(!record.isEmpty() && app.trainingSavable(), "reference record did not load");
    require(viewport->editing() && app.activeRoiParameter() == 2, "entering training mode did not seat the training ROI");
    app.setEasyTouchEnabled(true);
    require(app.assistAvailable(), "smart point selection is unavailable in training mode");
    const auto before = app.project().flows.front().nodes[1].inputs.at(6).literal;
    const auto &trainingSet = std::get<DataRef<TrainingSet>>(app.project().flows.front().nodes[1].trainParams.at(7)).get();
    const auto trainingBefore = trainingSet.records.begin()->second.fields.at(2);
    const auto inputRoiVisible = [&](const PortValue &expectedRoi)
    {
        auto capture = viewport->grabToImage();
        require(capture && QTest::qWaitFor([&] { return !capture->image().isNull(); }, 3000),
                "viewport could not be captured");
        const auto rendered = capture->image();
        const auto &firstRect = std::get<Rectangle>(expectedRoi).get().front().front();
        const auto corner = RegionView(firstRect).rectangle().corners().front();
        const auto edgePoint = viewport->imageToItem({corner.x, corner.y})
                             * (rendered.width() / viewport->width());
        return hasColorNear(rendered, edgePoint, VisionRTColor::getInstance()->ROIInput());
    };
    require(inputRoiVisible(trainingBefore), "training ROI rectangle is not visible on the training canvas");
    app.setEasyTouchEnabled(false);
    app.runNode();
    settle();
    require(viewport->editing() && app.activeRoiParameter() == 2,
            "running a node while in training mode cleared the training ROI");
    app.setTrainingMode(false);
    settle();
    require(inputRoiVisible(before), "input ROI rectangle is not visible after returning to normal mode");
    app.setTrainingMode(true);
    settle();
    require(app.setTrainingField(record, 2, "[[[1200, 1570, 300, 90, 0], [1880, 1000, 300, 90, 90]]]"),
            "training search regions could not be edited");
    require(equalValues(app.project().flows.front().nodes[1].inputs.at(6).literal, before),
            "editing training regions changed the detection regions");
    // Restore the known reference regions, then fit with the real tool.
    require(app.setTrainingField(record, 2, ValueJson::text(before, {Type::Rectangle})), "reference regions could not be restored");
    require(app.setTrainingParameter(1, ""), "default model location could not be chosen");
    app.applyTraining();
    settle();
    auto *outputs = qobject_cast<OutputModel *>(app.outputModel());
    require(outputs && app.outputTitle() == QStringLiteral("训练输出"), "training did not replace the visible result");
    const auto found = std::get<Line>(outputs->capture(PortId{3})->value).get();
    const auto &line = found.front().front();
    auto capture = viewport->grabToImage();
    require(capture && QTest::qWaitFor([&] { return !capture->image().isNull(); }, 3000), "training viewport could not be captured");
    const auto rendered = capture->image();
    const auto point = viewport->imageToItem({(line[1] + line[3]) / 2, (line[2] + line[4]) / 2}) * (rendered.width() / viewport->width());
    require(hasColorNear(rendered, point, VisionRTColor::getInstance()->ROIOutput()),
            "the purple measured edge does not match the shared output coordinates");
    QDir().mkpath(QStringLiteral(VISIONRT_BINARY_DIR "/evidence"));
    window->grabWindow().save(QStringLiteral(VISIONRT_BINARY_DIR "/evidence/training-shared-output.png"));
    app.selectNode("1");
    settle();
    require(!app.trainingAvailable() && app.parameterTabs().size() == 4 && !app.trainingMode(),
            "an ordinary tool retained training tabs");
    const auto calibration = testing::copyExample(VISIONRT_SOURCE_DIR, installation.directory(),
        std::filesystem::path(u8"examples/综合示例/变距尺子标定测量"), std::filesystem::path(u8"变距尺子标定测量.vrtapp"));
    app.requestOpen(calibration);
    app.resolveUnsaved(UnsavedDecision::Discard);
    settle();
    app.selectNode("2");
    settle();
    app.setTrainingMode(true);
    require(app.trainingSavable() && viewport->hasImage(), "calibration training needs a frame and no canvas ROI");
    require(app.setTrainingParameter(2, ""), "calibration model uses the training publication path");
    app.applyTraining();
    settle();
    require(app.outputTitle() == QStringLiteral("训练输出"), "calibration uses the shared training output");
    require(std::get<double>(outputs->capture(PortId{5})->value) < 0.5, "real plate satisfies the declared quality threshold");
    require(std::get<CoordinateFrame2D>(outputs->capture(PortId{6})->value).count(0) == 1, "training publishes the reusable coordinate frame");
    require(std::get<Circle>(outputs->capture(PortId{4})->value).get().front().size() > 500, "training locates the real plate marks");
    const auto installRoot = std::filesystem::path(installation.directory().toStdWString());
    const auto configuredModel = std::filesystem::u8path(std::get<std::string>(app.project().flows.front().nodes.at(1).trainParams.at(2)));
    const auto modelPath = std::filesystem::weakly_canonical(configuredModel.is_absolute() ? configuredModel : installRoot / configuredModel);
    const auto relativeModel = modelPath.lexically_relative(std::filesystem::weakly_canonical(installRoot));
    require(!relativeModel.empty() && *relativeModel.begin() != "..", "training assets belong to the controller installation");
    trainingTab = button(panel, "训练");
    QTest::mouseClick(window, Qt::LeftButton, {}, trainingTab->mapToScene(trainingTab->boundingRect().center()).toPoint());
    QTest::qWait(100);
    window->grabWindow().save(QStringLiteral(VISIONRT_BINARY_DIR "/evidence/calibration-training-workbench.png"));
    require(warnings.isEmpty(), qPrintable(warnings.join('\n')));
    std::cout << "Real workbench tabs, grouping, independent training parameters, and measured-edge rendering agree\n";
    return 0;
}
catch (const std::exception &error)
{
    std::cerr << error.what() << '\n';
    return 1;
}
