#include "WorkbenchFixtures.h"
#include "log/LogModel.h"
#include "log/LogService.h"
#include "ui/AppController.h"
#include "ui/ImageViewport.h"
#include "ui/UiTypes.h"

#include <QFile>
#include <QGuiApplication>
#include <QQmlComponent>
#include <QQmlContext>
#include <QQmlEngine>
#include <QQuickItem>
#include <QQuickItemGrabResult>
#include <QQuickStyle>
#include <QQuickWindow>
#include <QTest>
#include <cmath>
#include <iostream>
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

// The panel never names a field by meaning; a test locates the control the same way QML does, by
// the declared type and number, not by a tool-specific label.
// The project tree is the observation point for "training must not write while editing": a new
// picture, sidecar or asset would change the list.
QStringList treeOf(const std::filesystem::path &root)
{
    QStringList result;
    for (const auto &entry : std::filesystem::recursive_directory_iterator(root))
        if (entry.is_regular_file())
            result.append(QString::fromStdWString(entry.path().wstring()) + "|"
                          + QString::number(static_cast<qulonglong>(entry.file_size())));
    result.sort();
    return result;
}

int fieldOfType(const QVariantList &rows, const QString &record, int type)
{
    for (const auto &row : rows)
    {
        const auto map = row.toMap();
        if (map.value("id").toString() != record)
            continue;
        for (const auto &field : map.value("fields").toList())
            if (field.toMap().value("type").toInt() == type)
                return field.toMap().value("id").toInt();
    }
    return 0;
}
} // namespace

int main(int argc, char **argv)
try
{
    QGuiApplication application(argc, argv);
    QQuickStyle::setStyle("Basic");
    registerUiTypes();
    qmlRegisterType<ImageViewport>("VisionRT.View", 1, 0, "ImageViewport");
    testing::WorkbenchInstallation installation;
    const auto                     root   = std::filesystem::path(installation.directory().toStdWString());
    const auto                     sample = std::filesystem::path(u8"examples/03_综合示例/02_识别线缆颜色");
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
    const auto    settle = [&]
    {
        require(QTest::qWaitFor([&] { return app.workspaceEnabled(); }, 15000), "operation did not settle");
        QTest::qWait(200);
        require(QTest::qWaitFor([&] { return app.workspaceEnabled(); }, 15000), "synchronize did not settle");
    };
    settle();
    app.requestOpen(
        QString::fromStdWString((root / sample / std::filesystem::path(u8"识别线缆颜色.vrt.json")).wstring()));
    app.resolveUnsaved(visionrt::UnsavedDecision::Discard);
    settle();
    app.runOnce();
    settle();
    app.selectNode("2");
    settle();
    QQmlEngine  engine;
    QStringList warnings;
    QObject::connect(&engine, &QQmlEngine::warnings, &engine,
                     [&](const QList<QQmlError> &errors)
                     {
                         for (const auto &error : errors) warnings.append(error.toString());
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
    window->showMaximized();
    require(QTest::qWaitForWindowExposed(window), "workbench window was not exposed");
    auto *viewport = window->findChild<ImageViewport *>("imageViewport");
    require(viewport != nullptr, "training viewport is missing");
    const auto   outputColor = viewport->outputRoiColor();
    const QColor processingMarker(255, 0, 255);
    viewport->setOutputRoiColor(processingMarker);
    viewport->setOverlays({}, {makeCircle({300, 300}, 80)});
    const auto hasProcessingOverlay = [&]
    {
        auto capture = viewport->grabToImage();
        require(capture && QTest::qWaitFor([&] { return !capture->image().isNull(); }, 3000),
                "the actual viewport could not be captured");
        const auto image = capture->image();
        for (int y = 0; y < image.height(); ++y)
            for (int x = 0; x < image.width(); ++x)
                if (image.pixelColor(x, y) == processingMarker)
                    return true;
        return false;
    };
    require(hasProcessingOverlay(), "processing overlay control did not render");
    app.setTrainingMode(true);
    app.setEasyTouchEnabled(true);
    settle();
    if (!app.trainingMode() || app.trainingRecords().size() != 1)
        throw std::runtime_error("reference training did not open: selected=" + app.selectedNode().toStdString()
                                 + " records=" + std::to_string(app.trainingRecords().size()) + " "
                                 + app.diagnostics().join(" | ").toStdString());
    require(!hasProcessingOverlay(), "processing overlays leaked onto the training reference image");
    viewport->setOutputRoiColor(outputColor);
    require(app.trainingParameters().isEmpty(), "business inputs were duplicated as training parameters");
    auto *apply = textButton(window->contentItem(), QStringLiteral("应用训练数据"));
    require(apply && apply->isVisible() && apply->isEnabled(), "apply button is not usable");

    app.setLogViewActive(true);
    auto *history = qobject_cast<LogModel *>(app.logModel());
    require(history != nullptr, "log model is missing");
    require(QTest::qWaitFor([&] { return history->allText().contains(QStringLiteral("Ctrl")); }, 2000),
            "EasyTouch guidance did not reach the existing log");
    const auto zoomBefore = viewport->zoom();
    auto      *zoomIn     = textButton(window->contentItem(), QStringLiteral("放大"));
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
    app.removeTrainingDraftRecord(app.selectedTrainingDraftRecord());
    app.setTrainingMode(false);
    app.setImageOutput("inputs:1");
    settle();
    require(app.addTrainingImage(), "the reference frame could not start a new training record");
    settle();
    QTest::mouseMove(window, viewport->mapToScene(viewport->imageToItem({1350, 1614})).toPoint(), 20);
    require(QTest::qWaitFor([&] { return !app.assistPreview().isEmpty(); }, 5000),
            "real edge helper found no candidate");
    require(app.setParameter(5, false, "0.6"), "the user completeness parameter was rejected");
    settle();
    require(app.setParameter(2, false, "900.0"), "the contrast preset was rejected");
    settle();
    require(app.setParameter(3, false, "63"), "the width preset was rejected");
    settle();
    QTest::mouseClick(window, Qt::LeftButton, Qt::NoModifier,
                      viewport->mapToScene(viewport->imageToItem({1350, 1614})).toPoint());
    require(QTest::qWaitFor([&] { return app.trainingRecords().front().toMap().value("regions").toInt() == 1; }, 5000),
            "straight-edge click did not replace the reference");
    settle();
    QTest::mouseClick(window, Qt::LeftButton, Qt::ControlModifier,
                      viewport->mapToScene(viewport->imageToItem({2046, 1150})).toPoint());
    require(QTest::qWaitFor([&] { return app.trainingRecords().front().toMap().value("regions").toInt() == 2; }, 5000),
            "Ctrl-click did not add the second reference");
    settle();
    for (const auto &value : app.parameters())
    {
        const auto parameter = value.toMap();
        const auto id        = parameter.value("id").toInt();
        const auto literal   = parameter.value("literal").toString();
        if (id == 2 && !parameter.value("init").toBool())
            require(literal.toDouble() > 0 && literal.toDouble() < 900,
                    "edge contrast was not estimated from the image");
        if (id == 3 && !parameter.value("init").toBool())
            require(literal.toInt() > 0 && literal.toInt() < 63, "edge width was not estimated from the image");
        if (id == 5 && !parameter.value("init").toBool())
            require(literal.toDouble() == 0.6, "point selection overwrote user completeness");
    }
    require(app.trainingDirty(), "point selection must remain a draft until apply");
    const auto suggestedContrast = app.project().flows.front().nodes.at(1).inputs.at(2).literal;
    require(app.setParameter(2, false, "900.0"), "the training contrast override was rejected");
    settle();
    app.applyTraining();
    settle();
    require(app.trainingDirty(), "the trainer ignored the business contrast parameter");
    require(app.setParameter(2, false, QString::number(std::get<double>(suggestedContrast), 'g', 17)),
            "the suggested contrast could not be restored");
    settle();
    app.applyTraining();
    settle();
    require(!app.trainingDirty(),
            ("applying straight-edge training did not commit the draft: " + history->allText().toStdString()).c_str());
    const auto guidanceCount = history->allText().count(QStringLiteral("Ctrl"));
    for (int index = 0; index < 32; ++index) app.assistHover(-50, -50);
    QTest::qWait(150);
    require(history->allText().count(QStringLiteral("Ctrl")) == guidanceCount,
            "hover repeated the interaction guidance");
    require(app.assistClick(-50, -50, false), "missed-target click could not be tested");
    require(QTest::qWaitFor([&] { return history->allText().contains(QStringLiteral("智能点选失败")); }, 5000),
            "an explicit missed pick did not reach the log");
    app.selectNode("3");
    settle();
    app.setTrainingMode(true);
    settle();
    require(!app.trainingRecords().isEmpty(), "color training records are missing");
    const auto parameters             = app.parameters();
    const auto regionCount            = app.trainingRecords().front().toMap().value("regions").toInt();
    const auto projectFilesBeforeDraw = treeOf(root / sample);
    app.setDrawShape(2);
    const auto from = viewport->mapToScene(viewport->imageToItem({500, 200})).toPoint();
    const auto to   = viewport->mapToScene(viewport->imageToItem({650, 280})).toPoint();
    QTest::mousePress(window, Qt::LeftButton, {}, from);
    QTest::mouseMove(window, to, 20);
    QTest::mouseRelease(window, Qt::LeftButton, {}, to);
    require(app.trainingRecords().front().toMap().value("regions").toInt() == regionCount + 1,
            "manual drawing did not add a rectangle to the color record");
    require(app.parameters() == parameters, "a training rectangle changed processing parameters");
    require(treeOf(root / sample) == projectFilesBeforeDraw, "drawing a training rectangle wrote a file");
    const auto record = app.selectedTrainingDraftRecord();
    app.setTrainingField(record, 1, "edited-purple");
    require(app.trainingDirty(), "editing a label did not mark the draft");
    require(!visibleTextContains(window->contentItem(), QStringLiteral("待应用")),
            "training still displays a pending-state message");
    require(!visibleTextContains(viewport->parentItem(), QStringLiteral("智能点选")),
            "EasyTouch still overlays guidance on the image");
    app.setTrainingMode(false);
    app.setTrainingMode(true);
    require(window->findChild<ImageViewport *>("imageViewport") == viewport,
            "switching modes replaced the shared viewport");
    const auto labelField = fieldOfType(app.trainingRecords(), record, static_cast<int>(Type::String));
    require(labelField != 0, "the declared text field is missing from the record");
    require(app.trainingDirty() && app.selectedTrainingDraftRecord() == record,
            "switching processing and training discarded the unapplied draft");

    app.selectNode("1");
    require(!app.trainingAvailable() && !app.trainingMode(), "ordinary tool kept training UI");
    app.selectNode("3");
    require(app.trainingMode() && app.trainingDirty() && app.selectedTrainingDraftRecord() == record,
            "returning to the node lost its training view and draft");
    const auto rows = app.trainingRecords();
    app.toggleLoop();
    require(QTest::qWaitFor([&] { return app.stopEnabled(); }, 5000), "loop did not start");
    require(!app.property("canAddTrainingImage").toBool() && !app.shapeToolsAvailable(),
            "run left training actions enabled");
    app.removeTrainingDraftRecord(record);
    app.setTrainingField(record, 1, "forbidden-edit");
    require(app.trainingRecords() == rows, "a running pipeline accepted a training edit");
    app.stop();
    settle();
    require(app.shapeToolsAvailable(), "training did not become editable after stopping");
    app.runOnce();
    settle();
    app.setTrainingMode(false);
    app.setImageOutput("inputs:1");
    require(QTest::qWaitFor([&] { return app.property("canAddTrainingImage").toBool(); }, 5000),
            ("a valid processing image cannot be added to training: output=" + app.selectedImageOutput().toStdString()
             + " hasImage=" + std::to_string(viewport->hasImage()) + " " + history->allText().toStdString())
                .c_str());
    const auto output     = app.selectedImageOutput();
    const auto imageIndex = app.imageIndex();
    QTest::qWait(100);
    QTest::mouseClick(window, Qt::RightButton, {}, viewport->mapToScene(viewport->boundingRect().center()).toPoint());
    auto *menu = window->findChild<QObject *>("trainingMenu");
    require(menu != nullptr, "training menu object is missing");
    require(QTest::qWaitFor([&] { return menu->property("visible").toBool(); }, 1000),
            "right click did not open the training menu");
    const auto projectFiles     = treeOf(root / sample);
    const bool workspaceBefore  = std::filesystem::exists(root / "training");
    const auto parametersBefore = app.parameters();
    const auto dirtyBefore      = app.dirty();
    require(app.addTrainingImage(), "adding the actual processing image failed");
    require(treeOf(root / sample) == projectFiles, "adding a training image wrote into the project");
    require(std::filesystem::exists(root / "training") == workspaceBefore,
            "adding a training image created the host training workspace");
    require(app.parameters() == parametersBefore && app.dirty() == dirtyBefore,
            "adding a training image changed the runnable project");
    // Saving (and saving under another name) writes the project JSON only; the draft still has no
    // asset file of its own until apply.
    const auto savedProject           = QString::fromStdWString((root / "draft-save.vrt.json").wstring());
    const auto projectFilesBeforeSave = treeOf(root / sample);
    require(app.save(savedProject), "saving a project with an unapplied draft failed");
    settle();
    require(treeOf(root / sample) == projectFilesBeforeSave,
            "saving a project wrote training data for an unapplied draft");
    require(app.trainingDirty(), "saving the project discarded the unapplied draft");
    require(QTest::qWaitFor([&] { return !menu->property("visible").toBool(); }, 1000),
            "training mode kept the processing menu open");
    require(app.trainingMode() && app.trainingRecords().size() == rows.size() + 1,
            "added image was not selected in training");
    require(!app.trainingSavable(), "an unlabelled image without regions must not be applicable");
    app.applyTraining();
    require(app.trainingDirty(), ("failed apply discarded the draft: " + app.diagnostic().toStdString()).c_str());
    app.removeTrainingDraftRecord(app.selectedTrainingDraftRecord());
    app.applyTraining();
    settle();
    require(!app.trainingDirty(), "correcting an invalid draft did not allow applying it");
    app.setTrainingMode(false);
    require(app.selectedImageOutput() == output && app.imageIndex() == imageIndex,
            "training changed processing image selection");
    app.setTrainingMode(true);
    require(warnings.empty(), warnings.join(" | ").toStdString().c_str());
    // Deleting the node releases the draft instead of leaving an orphan session behind.
    app.setTrainingField(app.selectedTrainingDraftRecord(), 1, "delete-me");
    require(app.trainingDirty(), "the draft to delete was not dirty");
    app.deleteNodes(QVariantList{QStringLiteral("3")});
    settle();
    require(app.selectedNode() != QStringLiteral("3") && !app.trainingAvailable() && app.trainingRecords().isEmpty(),
            "deleting a node kept its training draft");
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
