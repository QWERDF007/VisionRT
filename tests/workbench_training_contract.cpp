#include "WorkbenchFixtures.h"
#include "ui/AppController.h"
#include "ui/ImageViewport.h"
#include "ui/Theme.h"
#include "log/LogService.h"
#include "log/LogModel.h"

#include <QGuiApplication>
#include <QFile>
#include <QQmlComponent>
#include <QQmlContext>
#include <QQmlEngine>
#include <QQuickItem>
#include <QQuickStyle>
#include <QQuickWindow>
#include <QTest>
#include <iostream>
#include <cmath>
#include <memory>
#include <stdexcept>

using namespace visionrt;

namespace {
void require(bool condition, const char *message)
{
    if (!condition)
        throw std::runtime_error(message);
}

QQuickItem *textButton(QQuickItem *parent, const QString &text)
{
    for (auto *item : parent->childItems())
    {
        if (item->property("text").toString() == text && item->property("normalColor").isValid())
            return item;
        if (auto *found = textButton(item, text))
            return found;
    }
    return nullptr;
}

bool visibleTextContains(QQuickItem *parent, const QString &text)
{
    for (auto *item : parent->childItems())
    {
        if (item->isVisible() && item->inherits("QQuickText") && item->property("text").toString().contains(text))
            return true;
        if (visibleTextContains(item, text))
            return true;
    }
    return false;
}
}

int main(int argc, char **argv)
try
{
    QGuiApplication application(argc, argv);
    QQuickStyle::setStyle("Basic");
    registerStyleModule();
    qmlRegisterType<ImageViewport>("VisionRT.View", 1, 0, "ImageViewport");
    testing::WorkbenchInstallation installation;
    const auto root = std::filesystem::path(installation.directory().toStdWString());
    const auto sample = std::filesystem::path(u8"examples/03_综合示例/02_识别线缆颜色");
    std::filesystem::create_directories((root / sample).parent_path());
    std::filesystem::copy(std::filesystem::path(VISIONRT_SOURCE_DIR) / sample, root / sample,
                          std::filesystem::copy_options::recursive);
    // Tool paths are installation-relative. Relocate the fixture's JSON references as well as its
    // files, so applying training writes only this temporary installation.
    for (const auto &entry : std::filesystem::recursive_directory_iterator(root / sample))
    {
        if (entry.path().extension() != ".json")
            continue;
        QFile file(QString::fromStdWString(entry.path().wstring()));
        require(file.open(QIODevice::ReadOnly), "fixture JSON could not be read");
        auto bytes = file.readAll();
        file.close();
        bytes.replace(QByteArray("examples/03_综合示例/02_识别线缆颜色"),
                      installation.directory().toUtf8() + QByteArray("/examples/03_综合示例/02_识别线缆颜色"));
        require(file.open(QIODevice::WriteOnly | QIODevice::Truncate) && file.write(bytes) == bytes.size(),
                "fixture JSON could not be relocated");
    }
    auto &logs = LogService::instance();
    require(logs.start({}, installation.directory() + "/logs/test.log"), "logs did not start");
    AppController app(2, installation.directory());
    const auto settle = [&] {
        require(QTest::qWaitFor([&] { return app.workspaceEnabled(); }, 15000), "operation did not settle");
        QTest::qWait(200);
        require(QTest::qWaitFor([&] { return app.workspaceEnabled(); }, 15000), "synchronize did not settle");
    };
    settle();
    app.requestOpen(QString::fromStdWString((root / sample / std::filesystem::path(u8"识别线缆颜色.vrt.json")).wstring()));
    app.resolveUnsaved("discard");
    settle();
    app.selectNode("2");
    settle();
    QQmlEngine engine;
    QStringList warnings;
    QObject::connect(&engine, &QQmlEngine::warnings, &engine, [&](const QList<QQmlError> &errors) {
        for (const auto &error : errors)
            warnings.append(error.toString());
    });
    engine.addImportPath(QStringLiteral(VISIONRT_BINARY_DIR "/qml"));
    engine.rootContext()->setContextProperty("app", &app);
    engine.rootContext()->setContextProperty("demoMenu", false);
    engine.rootContext()->setContextProperty("demoFileMenu", false);
    engine.rootContext()->setContextProperty("demoSettings", false);
    engine.rootContext()->setContextProperty("demoSelect", false);
    QQmlComponent component(&engine, QUrl::fromLocalFile(QStringLiteral(VISIONRT_SOURCE_DIR "/src/ui/qml/Main.qml")));
    std::unique_ptr<QObject> owner(component.create());
    if (!owner)
        throw std::runtime_error(component.errorString().toStdString());
    auto *window = qobject_cast<QQuickWindow *>(owner.get());
    require(window != nullptr, "workbench window is missing");
    window->show();
    require(QTest::qWaitForWindowExposed(window), "workbench window was not exposed");
    app.setTrainingMode(true);
    app.setEasyTouchEnabled(true);
    settle();
    if (!app.trainingMode() || app.trainingRecords().size() != 1)
        throw std::runtime_error("reference training did not open: selected=" + app.selectedNode().toStdString()
                                 + " kind=" + app.trainingKind().toStdString() + " records="
                                 + std::to_string(app.trainingRecords().size()) + " "
                                 + app.diagnostics().join(" | ").toStdString());
    auto *apply = textButton(window->contentItem(), QStringLiteral("应用训练数据"));
    require(apply && apply->isVisible() && apply->isEnabled(), "apply button is not usable");
    require(apply->property("normalColor").value<QColor>() != apply->property("textColor").value<QColor>(),
            "apply training has invisible white text on a white background");
    app.setLogViewActive(true);
    auto *history = qobject_cast<LogModel *>(app.logModel());
    require(history != nullptr, "log model is missing");
    require(QTest::qWaitFor([&] { return history->allText().contains(QStringLiteral("Ctrl")); }, 2000),
            "EasyTouch guidance did not reach the existing log");
    auto *viewport = window->findChild<ImageViewport *>("imageViewport");
    require(viewport != nullptr, "training viewport is missing");
    const auto zoomBefore = viewport->zoom();
    auto *zoomIn = textButton(window->contentItem(), QStringLiteral("放大"));
    require(zoomIn != nullptr, "zoom button is missing");
    QTest::mouseClick(window, Qt::LeftButton, {}, zoomIn->mapToScene(zoomIn->boundingRect().center()).toPoint());
    require(viewport->zoom() > zoomBefore, "toolbar zoom did not reach the viewport");
    auto *oneToOne = textButton(window->contentItem(), QStringLiteral("1:1 像素显示"));
    require(oneToOne != nullptr, "1:1 button is missing");
    QTest::mouseClick(window, Qt::LeftButton, {}, oneToOne->mapToScene(oneToOne->boundingRect().center()).toPoint());
    require(std::abs(viewport->zoom() - 1.0) < 1e-6, "toolbar 1:1 did not reach the viewport");
    auto *fit = textButton(window->contentItem(), QStringLiteral("适应窗口"));
    require(fit != nullptr, "fit button is missing");
    QTest::mouseClick(window, Qt::LeftButton, {}, fit->mapToScene(fit->boundingRect().center()).toPoint());
    require(std::abs(viewport->zoom() - zoomBefore) < 1e-6, "toolbar fit did not restore the viewport");
    QTest::mouseMove(window, viewport->mapToScene(viewport->imageToItem({1350, 1614})).toPoint(), 20);
    require(QTest::qWaitFor([&] { return !app.assistPreview().isEmpty(); }, 5000), "real edge helper found no candidate");
    require(app.assistClick(1350, 1614, false), "straight-edge replace was refused");
    require(QTest::qWaitFor([&] { return app.trainingRecords().front().toMap().value("regions").toInt() == 1; }, 5000),
            "straight-edge click did not replace the reference");
    require(app.assistClick(2046, 1150, true), "straight-edge append was refused");
    require(QTest::qWaitFor([&] { return app.trainingRecords().front().toMap().value("regions").toInt() == 2; }, 5000),
            "Ctrl-click did not add the second reference");
    app.applyTraining();
    settle();
    require(!app.trainingDirty(), "applying straight-edge training did not commit the draft");
    const auto guidanceCount = history->allText().count(QStringLiteral("Ctrl"));
    for (int index = 0; index < 32; ++index)
        app.assistHover(-50, -50);
    QTest::qWait(150);
    require(history->allText().count(QStringLiteral("Ctrl")) == guidanceCount, "hover repeated the interaction guidance");
    require(app.assistClick(-50, -50, false), "missed-target click could not be tested");
    require(QTest::qWaitFor([&] { return history->allText().contains(QStringLiteral("智能点选失败")); }, 5000),
            "an explicit missed pick did not reach the log");
    app.selectNode("3");
    settle();
    app.setTrainingMode(true);
    settle();
    require(!app.trainingRecords().isEmpty(), "color training records are missing");
    const auto parameters = app.parameters();
    const auto regionCount = app.trainingRecords().front().toMap().value("regions").toInt();
    app.setDrawShape(2);
    const auto from = viewport->mapToScene(viewport->imageToItem({500, 200})).toPoint();
    const auto to = viewport->mapToScene(viewport->imageToItem({650, 280})).toPoint();
    QTest::mousePress(window, Qt::LeftButton, {}, from);
    QTest::mouseMove(window, to, 20);
    QTest::mouseRelease(window, Qt::LeftButton, {}, to);
    require(app.trainingRecords().front().toMap().value("regions").toInt() == regionCount + 1,
            "manual drawing did not add a rectangle to the color record");
    require(app.parameters() == parameters, "a training rectangle changed processing parameters");
    const auto record = app.selectedTrainingRecord();
    app.setTrainingLabel(record, "edited-purple");
    require(app.trainingDirty(), "editing a label did not mark the draft");
    require(!visibleTextContains(window->contentItem(), QStringLiteral("待应用")), "training still displays a pending-state message");
    require(!visibleTextContains(viewport->parentItem(), QStringLiteral("智能点选")), "EasyTouch still overlays guidance on the image");
    app.setTrainingMode(false);
    app.setTrainingMode(true);
    require(window->findChild<ImageViewport *>("imageViewport") == viewport,
            "switching modes replaced the shared viewport");
    require(app.trainingDirty() && app.selectedTrainingRecord() == record
                && app.trainingRecords().front().toMap().value("label").toString() == "edited-purple",
            "switching processing and training discarded the unapplied draft");
    app.selectNode("1");
    require(!app.trainingAvailable() && !app.trainingMode(), "ordinary tool kept training UI");
    app.selectNode("3");
    require(app.trainingMode() && app.trainingDirty() && app.selectedTrainingRecord() == record,
            "returning to the node lost its training view and draft");
    const auto rows = app.trainingRecords();
    app.toggleLoop();
    require(QTest::qWaitFor([&] { return app.stopEnabled(); }, 5000), "loop did not start");
    require(!app.property("canAddTrainingImage").toBool() && !app.shapeToolsAvailable(), "run left training actions enabled");
    app.removeTrainingRecord(record);
    app.setTrainingLabel(record, "forbidden-edit");
    require(app.trainingRecords() == rows, "a running pipeline accepted a training edit");
    app.stop();
    settle();
    require(app.shapeToolsAvailable(), "training did not become editable after stopping");
    app.setTrainingMode(false);
    app.setImageOutput("inputs:1");
    require(QTest::qWaitFor([&] { return app.property("canAddTrainingImage").toBool(); }, 5000),
            "a valid processing image cannot be added to training");
    const auto output = app.selectedImageOutput();
    const auto imageIndex = app.imageIndex();
    QTest::qWait(100);
    QTest::mouseClick(window, Qt::RightButton, {}, viewport->mapToScene(viewport->boundingRect().center()).toPoint());
    auto *menu = window->findChild<QObject *>("trainingMenu");
    require(menu != nullptr, "training menu object is missing");
    require(QTest::qWaitFor([&] { return menu->property("visible").toBool(); }, 1000),
            "right click did not open the training menu");
    require(app.addTrainingImage(), "adding the actual processing image failed");
    require(QTest::qWaitFor([&] { return !menu->property("visible").toBool(); }, 1000),
            "training mode kept the processing menu open");
    require(app.trainingMode() && app.trainingRecords().size() == rows.size() + 1, "added image was not selected in training");
    require(!app.trainingSavable(), "an unlabelled image without regions must not be applicable");
    app.applyTraining();
    require(app.trainingDirty(), "failed apply discarded the draft");
    app.removeTrainingRecord(app.selectedTrainingRecord());
    app.applyTraining();
    settle();
    require(!app.trainingDirty(), "correcting an invalid draft did not allow applying it");
    app.setTrainingMode(false);
    require(app.selectedImageOutput() == output && app.imageIndex() == imageIndex, "training changed processing image selection");
    app.setTrainingMode(true);
    QQmlComponent filledComponent(&engine);
    filledComponent.setData("import QtQuick\nimport quickui\nQuiFilledButton { text: \"Confirm\" }", QUrl());
    std::unique_ptr<QObject> filled(filledComponent.create());
    require(filled && filled->property("normalColor").value<QColor>() != filled->property("textColor").value<QColor>(),
            "filled button is unreadable under the real workbench palette");
    for (const auto &size : {QSize(1548, 960), QSize(1280, 800), QSize(960, 540)})
    {
        window->resize(size);
        QTest::qWait(100);
        apply = textButton(window->contentItem(), QStringLiteral("应用训练数据"));
        require(apply && apply->isVisible() && apply->height() >= 28 && apply->width() >= 150,
                "training footer is clipped at a supported window size");
        const auto rect = QRectF(apply->mapToScene(QPointF()), apply->size());
        require(QRectF(QPointF(), window->size()).contains(rect), "training footer lies outside the window");
        require(window->grabWindow().save(QStringLiteral(VISIONRT_BINARY_DIR "/evidence/training_%1x%2.png")
                                             .arg(size.width()).arg(size.height())), "training screenshot was not saved");
    }
    require(warnings.empty(), warnings.join(" | ").toStdString().c_str());
    const auto rebound = root / sample / "training/rebound.json";
    std::filesystem::copy_file(root / sample / std::filesystem::path(u8"training/线缆颜色.json"), rebound);
    app.setTrainingLabel(app.selectedTrainingRecord(), "unapplied-rebind");
    require(app.trainingDirty(), "asset rebind setup did not create a draft");
    require(app.setParameterPath(0, true, QString::fromStdWString(rebound.wstring())), "training asset rebind failed");
    settle();
    require(!app.trainingMode() && app.trainingRecords().isEmpty(), "asset rebind kept the previous training session");
    app.setTrainingMode(true);
    require(!app.trainingDirty(), "new asset inherited the old draft");
    app.selectNode("");
    require(app.selectedNode().isEmpty() && !app.trainingAvailable() && !app.trainingMode(),
            "clearing the selection did not leave training");
    window->hide();
    logs.shutdown();
    std::cout << "WORKBENCH_TRAINING_PASS\n";
    return 0;
}
catch (const std::exception &error)
{
    std::cerr << error.what() << '\n';
    LogService::instance().shutdown();
    return 1;
}
