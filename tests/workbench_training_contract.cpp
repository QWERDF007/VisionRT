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
    app.runOnce();
    settle();
    app.selectNode("2");
    settle();
    QStringList tabs;
    for (const auto &tab : app.parameterTabs()) tabs.append(tab.toMap().value("name").toString());
    require(tabs == QStringList{"基本输入", "预处理", "训练", "检测", "结果处理"}, "parameter tab order differs from the contract");
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
    const auto before = app.project().flows.front().nodes[1].inputs.at(6).literal;
    require(app.setTrainingField(record, 2, "[[[2, 1200, 1570, 300, 90, 0], [2, 1880, 1000, 300, 90, 90]]]"),
            "training search regions could not be edited");
    require(equalValues(app.project().flows.front().nodes[1].inputs.at(6).literal, before),
            "editing training regions changed the detection regions");
    // Restore the known reference regions, then fit with the real tool.
    require(app.setTrainingField(record, 2, ValueJson::text(before, {Type::ROI})), "reference regions could not be restored");
    require(app.setTrainingParameter(1, ""), "default model location could not be chosen");
    app.applyTraining();
    settle();
    auto *outputs = qobject_cast<OutputModel *>(app.outputModel());
    require(outputs && app.outputTitle() == QStringLiteral("训练输出"), "training did not replace the visible result");
    const auto found = std::get<DataRef<ROI>>(outputs->capture(PortId{3})->value).get();
    const auto &line = found.front().front();
    auto capture = viewport->grabToImage();
    require(capture && QTest::qWaitFor([&] { return !capture->image().isNull(); }, 3000), "training viewport could not be captured");
    const auto rendered = capture->image();
    const auto point = viewport->imageToItem({(line[1] + line[3]) / 2, (line[2] + line[4]) / 2}) * (rendered.width() / viewport->width());
    const auto color = VisionRTColor::getInstance()->ROIOutput();
    bool measuredLineVisible = false;
    for (int y = std::max(0, qRound(point.y()) - 4); y < std::min(rendered.height(), qRound(point.y()) + 5); ++y)
        for (int x = std::max(0, qRound(point.x()) - 4); x < std::min(rendered.width(), qRound(point.x()) + 5); ++x)
        {
            const auto pixel = rendered.pixelColor(x, y);
            if (std::abs(pixel.red() - color.red()) < 35 && std::abs(pixel.green() - color.green()) < 35
                && std::abs(pixel.blue() - color.blue()) < 35) measuredLineVisible = true;
        }
    require(measuredLineVisible, "the purple measured edge does not match the shared output coordinates");
    QDir().mkpath(QStringLiteral(VISIONRT_BINARY_DIR "/evidence"));
    window->grabWindow().save(QStringLiteral(VISIONRT_BINARY_DIR "/evidence/training-shared-output.png"));
    app.selectNode("1");
    settle();
    require(!app.trainingAvailable() && app.parameterTabs().size() == 4 && !app.trainingMode(),
            "an ordinary tool retained training tabs");
    require(warnings.isEmpty(), qPrintable(warnings.join('\n')));
    std::cout << "Real workbench tabs, grouping, independent training parameters, and measured-edge rendering agree\n";
    return 0;
}
catch (const std::exception &error)
{
    std::cerr << error.what() << '\n';
    return 1;
}
