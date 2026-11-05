#include "ui/AppController.h"
#include "ui/ImageViewport.h"
#include "ui/OutputModel.h"
#include "ui/Theme.h"
#include <quickui/Color.h>

#include "core/ValueJson.h"

#include <QDir>
#include <QJsonArray>
#include <QJsonDocument>
#include <QFile>
#include <QFileInfo>
#include <QGuiApplication>
#include <QInputMethodEvent>
#include <QJSValue>
#include <QQmlComponent>
#include <QQmlContext>
#include <QQmlEngine>
#include <QQuickItem>
#include <QQuickWindow>
#include <QPointer>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QTest>
#include <QtMath>
#include <iostream>
#include <limits>
#include <memory>
#include <stdexcept>

using namespace visionrt;

// test.roi.editor: 图像 1, 区域 2, 矩形 3, 参考区域 4; its business outputs follow 4..10.
// test.roi.source publishes 图像 2, 源区域 3, 源矩形 4.
constexpr PortId kImagesInput             = 1;
constexpr PortId kRoisInput               = 2;
constexpr PortId kRectInput               = 3;
constexpr PortId kSourceImagesOutput      = 2;
constexpr PortId kSourceRoisOutput        = 3;
constexpr PortId kSourceRectOutput        = 4;
constexpr PortId kCountOutput             = 2;
constexpr PortId kImagesOutput            = 3;
constexpr PortId kRegionsOutput           = 4;
constexpr PortId kUnrelatedImagesOutput   = 5;
constexpr PortId kUnavailableImagesOutput = 6;
constexpr PortId kCroppedImagesOutput     = 7;
constexpr PortId kOriginsOutput           = 9;
constexpr PortId kUnrelatedRegionsOutput  = 10;

QString imageKey(PortId output)
{
    return QStringLiteral("outputs:%1").arg(output);
}



void roiParameterContract()
{
    using namespace visionrt;
    const auto require = [](bool condition, const char *message)
    {
        if (!condition) throw std::runtime_error(message);
    };
    QTemporaryDir directory;
    require(directory.isValid(), "ROI fixture directory is unavailable");
    const auto package = directory.path() + "/tools-package/";
    require(QDir().mkpath(package + "tools/custom/roi"), "cannot create the ROI package fixture");
    const auto write = [&](const QString &name, const QByteArray &bytes)
    {
        QFile file(package + name);
        require(QDir().mkpath(QFileInfo(file).absolutePath()), "cannot create fixture parent directory");
        require(file.open(QIODevice::WriteOnly) && file.write(bytes) == bytes.size(), "cannot write the ROI fixture");
    };
    write("module_index.json",
          R"json({
  "schema_version": 1,
  "variant": "test",
  "tools": [
    {"tool_id": "test.roi.source", "definition": "tools/custom/roi/source.json"},
    {"tool_id": "test.roi.editor", "definition": "tools/custom/roi/editor.json"}
  ],
  "modules": [],
  "runtime_files": []
})json");
    write("python/custom/roi/source.py",
          "import numpy as np\ndef run(inputs):\n"
          "    return {'outputs': {2: [np.zeros((96, 128, 3), dtype=np.uint8), "
          "np.ones((24, 32, 3), dtype=np.uint8)], "
          "4: [[np.array([2, 1.25, 2.5, 3.75, 4.5, 17.5], dtype=np.float64).tolist()]], "
          "3: [[np.array([2, 40, 40, 10, 10, 0], dtype=np.float64).tolist()]], "
          "0: 0, 1: ''}, 'detail': ''}\n");
    write("tools/custom/roi/source.json",
          R"json({
  "schema_version": 6,
  "name_en": "test.roi.source",
  "name_zh": "ROI图像源",
  "version": "1.0.0",
  "implementation": {
    "kind": "python_function",
    "package": "roi",
    "file": "source.py",
    "symbol": "run"
  },
  "ports": {
    "outputs": [
      {
        "name_zh": "图像",
        "type": "image",
        "display": {
          "color_order": "BGR",
          "visible": true
        },
        "id": 2
      },
      {
        "name_zh": "源区域",
        "type": "roi",
        "id": 3
      },
      {
        "name_zh": "源矩形",
        "type": "roi",
        "relations": {
          "image": {
            "stage": "outputs",
            "id": 2
          }
        },
        "id": 4
      }
    ]
  },
  "next_port_id": {
    "init": 0,
    "inputs": 1,
    "outputs": 5
  }
})json");
    write("python/custom/roi/editor.py",
          "import numpy as np\ndef run(inputs):\n"
          "    return {'outputs': {2: len(inputs[2][0]), "
          "3: inputs[1], 4: [[np.array([2, 60, 75, 15, 10, 0], dtype=np.float64).tolist()], []], "
          "5: inputs[1][:1], 6: [], "
          "7: [image[5:, 10:] for image in inputs[1]], "
          "8: [[np.array([2, 25, 20, 10, 10, 0], dtype=np.float64).tolist()], []], "
          "9: np.array([[10, 5] for image in inputs[1]], dtype=np.float64).tolist(), "
          "10: [[np.array([2, 30, 75, 15, 10, 0], dtype=np.float64).tolist()]], "
          "0: 0, 1: ''}, 'detail': ''}\n");
    write("tools/custom/roi/editor.json",
          R"json({
  "schema_version": 6,
  "name_en": "test.roi.editor",
  "name_zh": "ROI编辑",
  "version": "1.0.0",
  "implementation": {
    "kind": "python_function",
    "package": "roi",
    "file": "editor.py",
    "symbol": "run"
  },
  "ports": {
    "inputs": [
      {
        "name_zh": "图像",
        "type": "image",
        "id": 1
      },
      {
        "name_zh": "区域",
        "type": "roi",
        "default": [
          []
        ],
        "relations": {
          "image": {
            "stage": "inputs",
            "id": 1
          }
        },
        "id": 2
      },
      {
        "name_zh": "矩形",
        "type": "roi",
        "default": [
          [
            [
              2,
              1.234567881,
              2.5,
              3.75,
              4.5,
              17.5
            ]
          ]
        ],
        "ui": {
          "editor": "rect"
        },
        "relations": {
          "image": {
            "stage": "inputs",
            "id": 1
          }
        },
        "id": 3
      },
      {
        "name_zh": "参考区域",
        "type": "roi",
        "default": [
          [
            [
              2,
              80,
              50,
              20,
              20,
              0
            ]
          ]
        ],
        "ui": {
          "editor": "rect"
        },
        "relations": {
          "image": {
            "stage": "inputs",
            "id": 1
          }
        },
        "id": 4
      }
    ],
    "outputs": [
      {
        "name_zh": "区域数量",
        "type": "int",
        "id": 2
      },
      {
        "name_zh": "图像",
        "type": "image",
        "relations": {
          "same_geometry_as": {
            "stage": "inputs",
            "id": 1
          }
        },
        "display": {
          "color_order": "BGR",
          "visible": true
        },
        "id": 3
      },
      {
        "name_zh": "结果区域",
        "type": "roi",
        "relations": {
          "image": {
            "stage": "outputs",
            "id": 3
          }
        },
        "id": 4
      },
      {
        "name_zh": "其他坐标图像",
        "type": "image",
        "display": {
          "visible": true
        },
        "id": 5
      },
      {
        "name_zh": "空图像输出",
        "type": "image",
        "display": {
          "visible": true
        },
        "id": 6
      },
      {
        "name_zh": "裁剪坐标图像",
        "type": "image",
        "display": {
          "visible": true
        },
        "id": 7
      },
      {
        "name_zh": "裁剪坐标区域",
        "type": "roi",
        "relations": {
          "image": {
            "stage": "outputs",
            "id": 7
          }
        },
        "id": 8
      },
      {
        "name_zh": "原点坐标",
        "type": "vec_vec_float",
        "id": 9
      },
      {
        "name_zh": "其他坐标区域",
        "type": "roi",
        "relations": {
          "image": {
            "stage": "outputs",
            "id": 5
          }
        },
        "id": 10
      }
    ]
  },
  "next_port_id": {
    "init": 0,
    "inputs": 5,
    "outputs": 11
  }
})json");

    AppController app(1, directory.path());
    require(QTest::qWaitFor([&] { return !app.busy(); }, 5000), "ROI catalog initialization did not finish");
    app.addNode("test.roi.source", 0, 0);
    require(QTest::qWaitFor([&] { return !app.busy(); }, 5000), "node instance synchronization did not finish");
    const auto source = app.selectedNode();
    require(!source.isEmpty(), "ROI source was not discovered");
    app.runNode();
    require(QTest::qWaitFor([&] { return !app.busy(); }, 5000), "ROI source execution did not finish");
    auto *outputs = qobject_cast<OutputModel *>(app.outputModel());
    require(outputs != nullptr, "ROI output model is unavailable");
    const auto output = [&](PortId port)
    {
        for (int row = 0; row < outputs->rowCount(); ++row)
            if (outputs->get(row).value("portId").toUInt() == port)
                return outputs->get(row).value("preview").toString();
        return QString{};
    };
    if (output(kStatusCodePort) != "0")
        throw std::runtime_error((QStringLiteral("ROI numpy image source failed: ") + output(kStatusMessagePort)
                                 + " " + app.diagnostic()).toStdString());
    app.addNode("test.roi.editor", 0, 1);
    require(QTest::qWaitFor([&] { return !app.busy(); }, 5000), "node instance synchronization did not finish");
    const auto target = app.selectedNode();
    require(target != source && app.bindInput(app.portContext(kImagesInput), source, kSourceImagesOutput), "ROI image input could not bind to the source");
    const auto literal = [&]
    {
        for (const auto &entry : app.parameters())
            if (entry.toMap().value("id").toUInt() == kRoisInput) return entry.toMap().value("literal").toString();
        return QString{};
    };
    QQmlEngine engine;
    quickui::QuiColor::getInstance()->setThemeMode(quickui::QuiColor::Light);
    quickui::QuiColor::getInstance()->setPrimary(themeForEngine(&engine)->surfaceBg());
    engine.addImportPath(QStringLiteral(VISIONRT_BINARY_DIR "/qml"));
    engine.rootContext()->setContextProperty("app", &app);
    QQmlComponent component(&engine);
    component.setData(
        "import QtQuick\nimport \".\"\nItem { id: root; width: 480; height: 36; "
        "readonly property var currentParameter: app.parameters.find(function(row) { return row.id === 2 }); "
        "Loader { anchors.fill: parent; active: root.currentParameter !== undefined; "
        "sourceComponent: ParameterEditor { parameter: root.currentParameter } } }",
        QUrl::fromLocalFile(QStringLiteral(VISIONRT_SOURCE_DIR "/src/ui/qml/RoiContract.qml")));
    std::unique_ptr<QObject> owner(component.create());
    if (!owner) throw std::runtime_error(component.errorString().toStdString());
    auto *editor = qobject_cast<QQuickItem *>(owner.get());
    require(editor != nullptr, "ROI parameter editor is unavailable");
    QQuickWindow window;
    window.resize(980, 520);
    editor->setParentItem(window.contentItem());
    ImageViewport viewport(window.contentItem());
    viewport.setX(500);
    viewport.setWidth(480);
    viewport.setHeight(520);
    app.attachViewport(&viewport);
    window.show();
    window.requestActivate();
    require(QTest::qWaitForWindowActive(&window), "ROI window did not become active");
    const auto renderedFrame = [&]
    {
        QSignalSpy frames(&window, &QQuickWindow::frameSwapped);
        window.update();
        require(frames.wait(3000), "ROI display did not present an updated frame");
        return window.grabWindow();
    };
    const auto colorAt = [&](const QImage &image, const QPointF &sourcePoint)
    {
        const auto point = viewport.mapToScene(viewport.imageToItem(sourcePoint)) * window.effectiveDevicePixelRatio();
        return image.pixelColor(qFloor(point.x()), qFloor(point.y()));
    };
    require(QTest::qWaitFor([&] { return editor->height() > 0; }, 1000), "ROI row has no layout");
    const auto findAction = [&]
    {
        QQuickItem *found   = nullptr;
        QList<QQuickItem *> pending{editor};
        while (!pending.isEmpty())
        {
            auto *item = pending.takeLast();
            if ((item->property("text").toString() == QStringLiteral("编辑输入 ROI")
                 || item->property("text").toString() == QStringLiteral("画矩形"))
                && item->metaObject()->indexOfSignal("clicked()") >= 0)
                found = item;
            pending.append(item->childItems());
        }
        return found;
    };
    QQuickItem *action = nullptr;
    require(QTest::qWaitFor([&] { return (action = findAction()) != nullptr && action->isEnabled(); }, 2000),
            "ROI action is missing from the parameter row");
    QTest::mouseClick(&window, Qt::LeftButton, Qt::NoModifier,
                      action->mapToScene(QPointF(action->width() / 2, action->height() / 2)).toPoint());
    require(QTest::qWaitFor([&] { return viewport.editing() && viewport.hasImage(); }, 3000),
            "parameter ROI action did not open the real input image editor");
    viewport.setDrawShape(0);
    const auto layeredImage = renderedFrame();
    const auto guidePoint = viewport.mapToScene(viewport.imageToItem({90, 50})) * window.effectiveDevicePixelRatio();
    const auto guideColor = layeredImage.pixelColor(qFloor(guidePoint.x()), qFloor(guidePoint.y()));
    require(guideColor.red() - guideColor.blue() > 100 && guideColor.green() - guideColor.blue() > 80,
            "editing one ROI parameter hid another input's read-only geometry");
    QTest::mouseClick(&window, Qt::LeftButton, Qt::NoModifier,
                      viewport.mapToScene(viewport.imageToItem({90, 60})).toPoint());
    require(viewport.selectedRoi() == -1, "read-only input geometry entered the editable selection");
    viewport.setDrawShape(2);
    const auto project = directory.filePath("roi.vrt.json");
    require(app.save(project) && !app.dirty(), "ROI baseline could not be saved");
    require(QTest::qWaitFor([&] { return !app.busy(); }, 5000), "saved project instances did not synchronize");
    const auto initial = literal();
    QSignalSpy changes(&app, &AppController::parametersChanged);
    const auto draw = [&](bool cancel, QPointF from = {10, 10}, QPointF to = {30, 30}, bool holdForPolling = false)
    {
        viewport.forceActiveFocus();
        const auto start = viewport.mapToScene(viewport.imageToItem(from)).toPoint();
        const auto end = viewport.mapToScene(viewport.imageToItem(to)).toPoint();
        QTest::mousePress(&window, Qt::LeftButton, Qt::NoModifier, start);
        QTest::mouseMove(&window, end, 20);
        if (holdForPolling)
        {
            QSignalSpy heldEdit(&viewport, &ImageViewport::editingChanged);
            QSignalSpy heldOutput(outputs, &QAbstractItemModel::modelReset);
            QSignalSpy heldCommit(&viewport, &ImageViewport::roiCommitted);
            QTest::qWait(140);
            require(heldEdit.isEmpty() && heldOutput.isEmpty() && heldCommit.isEmpty() && viewport.editing(),
                    "Idle polling interrupted an in-progress ROI gesture or reset its output model");
        }
        if (cancel) QTest::keyClick(&window, Qt::Key_Escape);
        QTest::mouseRelease(&window, Qt::LeftButton, Qt::NoModifier, end);
        QTest::qWait(30);
    };
    draw(true);
    require(literal() == initial && !app.dirty() && changes.isEmpty(), "Escape saved an unfinished ROI draft");
    draw(false);
    if (!(literal() != initial && app.dirty() && !changes.isEmpty()))
        throw std::runtime_error(
            (QStringLiteral("Completed ROI gesture was not committed: editing=%1, shape=%2, active=%3, dirty=%4, "
                            "signals=%5, initial=%6, actual=%7, diagnostic=%8")
                 .arg(viewport.editing())
                 .arg(viewport.drawShape())
                 .arg(app.activeRoiParameter())
                 .arg(app.dirty())
                 .arg(changes.count())
                 .arg(initial, literal(), app.diagnostic()))
                .toStdString());
    auto committed = literal();
    const auto rois      = std::get<DataRef<ROI>>(ValueJson::parse(committed, {Type::ROI}));
    require(rois.get().size() == 1 && rois.get()[0].size() == 1 && roiKind(rois.get()[0][0]) == RoiKind::Rectangle,
            "ROI parameter lost its typed rectangle representation");
    require(app.save(project) && !app.dirty(), "ROI project could not be saved");
    changes.clear();
    require(app.editRoi(kRoisInput, false), "Saved ROI could not reopen for read-only testing");
    require(QTest::qWaitFor([&] { return viewport.hasImage(); }, 3000), "Read-only ROI background was not presented");
    viewport.setReadOnly(true);
    viewport.setDrawShape(2);
    draw(false);
    QTest::keyClick(&window, Qt::Key_Delete);
    require(literal() == committed && !app.dirty() && changes.isEmpty(), "read-only ROI editor changed the project");
    viewport.setReadOnly(false);
    app.endRoiPreview();
    require(app.bindInput(app.portContext(kRoisInput), source, kSourceRoisOutput), "ROI input could not bind to the upstream ROI output");
    require(QTest::qWaitFor([&] { return !app.busy(); }, 5000), "upstream ROI binding did not settle");
    require(app.editRoi(kRoisInput, true), "Copied upstream session could reopen");
    require(QTest::qWaitFor([&] { return viewport.editing() && viewport.hasImage(); }, 3000),
            "Copied ROI background was presented");
    viewport.setDrawShape(2);
    draw(false);
    viewport.setDrawShape(0);
    QTest::mouseClick(&window, Qt::LeftButton, Qt::NoModifier,
                      viewport.mapToScene(viewport.imageToItem({20, 20})).toPoint());
    require(viewport.selectedRoi() == 1, "Copied ROI collection could not select its newly drawn rectangle");
    QTest::keyClick(&window, Qt::Key_Delete);
    const auto copiedAfterDelete = renderedFrame();
    const auto deletedBoundary = colorAt(copiedAfterDelete, {10, 20});
    const auto retainedBoundary = colorAt(copiedAfterDelete, {40, 45});
    require(viewport.selectedRoi() == -1 && deletedBoundary.red() < 10 && deletedBoundary.green() < 10
                && deletedBoundary.blue() < 10 && retainedBoundary.red() - retainedBoundary.blue() > 100
                && retainedBoundary.green() - retainedBoundary.blue() > 80,
            "Deleting a copied ROI did not remove only the selected draft region and its handles");
    require(app.wires().first().toMap().value("count").toInt() == 1,
            "Deleting a copied ROI detached its upstream binding");
    viewport.setDrawShape(2);
    draw(false);
    const auto edited = std::get<DataRef<ROI>>(ValueJson::parse(literal(), {Type::ROI}));
    require(edited.get().size() == 1 && edited.get()[0].size() == 2,
            "copied ROI input could not be edited independently");
    committed = literal();
    require(app.save(project), "copied ROI project could not be saved");
    app.endRoiPreview();
    app.requestOpen(project);
    require(QTest::qWaitFor([&] { return !app.busy(); }, 3000), "ROI project did not reopen");
    app.selectNode(target);
    require(literal() == committed, "ROI parameter changed after save/reopen");
    app.setImageOutput(imageKey(kUnavailableImagesOutput));
    app.runOnce();
    require(QTest::qWaitFor([&] { return !app.busy(); }, 5000), "ROI workflow did not finish");
    require(output(kStatusCodePort) == "0" && output(kCountOutput) == "2", "Python did not receive the saved typed ROI");
    QSignalSpy matchingPresented(&viewport, &ImageViewport::imagePresented);
    app.setImageOutput(imageKey(kImagesOutput));
    app.setImageIndex(0);
    require(QTest::qWaitFor([&] { return !matchingPresented.isEmpty() && viewport.hasImage() && !viewport.editing(); }, 3000),
            "Normal result view did not present its requested image before overlay inspection");
    const auto matchingOutput = renderedFrame();
    require(app.selectedImageOutput() == imageKey(kImagesOutput) && viewport.hasImage() && !viewport.editing(),
            "normal result view did not select the declared image output");
    const auto matchingRegion = colorAt(matchingOutput, {65, 75});
    require(matchingRegion.blue() - matchingRegion.red() > 100 && matchingRegion.green() - matchingRegion.red() > 50,
            "normal result view hid regions associated with its image output");
    const auto matchingInput = colorAt(matchingOutput, {90, 50});
    require(matchingInput.red() - matchingInput.blue() > 100 && matchingInput.green() - matchingInput.blue() > 80,
            "normal result view hid input geometry declared in matching coordinates");
    const auto unrelatedOnMatching = colorAt(matchingOutput, {35, 75});
    require(unrelatedOnMatching.red() < 10 && unrelatedOnMatching.green() < 10 && unrelatedOnMatching.blue() < 10,
            "normal result view mixed regions from a different image output");
    const auto savedRegions = output(kRegionsOutput);
    const auto savedUnrelatedRegions = output(kUnrelatedRegionsOutput);
    require(!savedRegions.isEmpty() && savedRegions != "Missing"
                && !savedUnrelatedRegions.isEmpty() && savedUnrelatedRegions != "Missing",
            "normal result view has no region values to inspect");
    QSignalSpy secondOutputPresented(&viewport, &ImageViewport::imagePresented);
    app.setImageIndex(1);
    require(QTest::qWaitFor([&] { return !secondOutputPresented.isEmpty() && viewport.hasImage(); }, 3000),
            "Normal result view did not present the second image in its selected output");
    renderedFrame();
    require(app.imageCount() == 2 && app.imageIndex() == 1 && viewport.hasImage(),
            "normal result view could not select the second image in its current output");
    QSignalSpy unrelatedPresented(&viewport, &ImageViewport::imagePresented);
    app.setImageOutput(imageKey(kUnrelatedImagesOutput));
    require(app.imageCount() == 1 && app.imageIndex() == 0,
            "switching image outputs retained another output's batch count or an invalid image index");
    require(QTest::qWaitFor([&] { return !unrelatedPresented.isEmpty() && viewport.hasImage(); }, 3000),
            "Normal result view did not present the newly selected output before region inspection");
    const auto unrelatedOutput = renderedFrame();
    require(app.selectedImageOutput() == imageKey(kUnrelatedImagesOutput) && viewport.hasImage() && !viewport.editing(),
            "normal result view could not switch to another image output");
    const auto unrelatedRegion = colorAt(unrelatedOutput, {35, 75});
    require(unrelatedRegion.blue() - unrelatedRegion.red() > 100 && unrelatedRegion.green() - unrelatedRegion.red() > 50,
            "switching image outputs did not display their associated regions");
    const auto mismatchingRegion = colorAt(unrelatedOutput, {65, 75});
    const auto mismatchingInput = colorAt(unrelatedOutput, {90, 50});
    require(mismatchingRegion.red() < 10 && mismatchingRegion.green() < 10 && mismatchingRegion.blue() < 10
                && mismatchingInput.red() < 10 && mismatchingInput.green() < 10 && mismatchingInput.blue() < 10,
            "equal image pixels incorrectly implied matching ROI coordinates");
    require(output(kRegionsOutput) == savedRegions && output(kUnrelatedRegionsOutput) == savedUnrelatedRegions,
            "switching image outputs discarded undisplayed region values");
    std::cout << "[ROI image ownership] Normal output switching respects metadata, not equal pixels; undisplayed region values remain available.\n";
    app.setImageOutput(imageKey(kUnavailableImagesOutput));
    renderedFrame();
    require(app.imageCount() == 0 && app.imageIndex() == 0 && !viewport.hasImage()
                && output(kRegionsOutput) == savedRegions && output(kUnrelatedRegionsOutput) == savedUnrelatedRegions,
            "an empty image output retained another output's preview, batch count or geometry state");
    app.setImageOutput(imageKey(kCroppedImagesOutput));
    QSignalSpy cropPresented(&viewport, &ImageViewport::imagePresented);
    require(QTest::qWaitFor([&] { return !cropPresented.isEmpty() && viewport.hasImage(); }, 3000)
                && app.imageCount() == 2 && app.imageIndex() == 0,
            "Cropped image output did not present its own image batch");
    const auto croppedView = renderedFrame();
    const auto cropRegion = colorAt(croppedView, {30, 20});
    require(cropRegion.blue() - cropRegion.red() > 100 && cropRegion.green() - cropRegion.red() > 50,
            "Cropped image output did not show regions declared in its own coordinates");
    const auto inputOnCrop = colorAt(croppedView, {90, 50});
    const auto unprojectedResultOnCrop = colorAt(croppedView, {65, 75});
    const auto guessedProjectionOnCrop = colorAt(croppedView, {80, 45});
    require(inputOnCrop.red() < 10 && inputOnCrop.green() < 10 && inputOnCrop.blue() < 10
                && unprojectedResultOnCrop.red() < 10 && unprojectedResultOnCrop.green() < 10 && unprojectedResultOnCrop.blue() < 10
                && guessedProjectionOnCrop.red() < 10 && guessedProjectionOnCrop.green() < 10 && guessedProjectionOnCrop.blue() < 10,
            "Crop origins implicitly overlaid or translated regions from the original image coordinates");
    require(output(kOriginsOutput) != "Missing" && !output(kOriginsOutput).isEmpty()
                && output(kRegionsOutput) == savedRegions && output(kUnrelatedRegionsOutput) == savedUnrelatedRegions,
            "Cropped output preview discarded the original region values or origin coordinates");
    std::cout << "[ROI crop coordinates] Cropped outputs display their declared regions without inferring projection from origins.\n";
    app.setImageOutput(imageKey(kImagesOutput));
    require(app.imageCount() == 2 && app.imageIndex() == 0,
            "switching back to an image output did not restore its own batch count");
    require(app.editRoi(kRoisInput, false), "Executed ROI could not reopen with its result context");
    require(QTest::qWaitFor([&] { return viewport.editing() && viewport.hasImage(); }, 3000),
            "Executed ROI background was not presented");
    const auto resultLayers = renderedFrame();
    const auto resultPoint = viewport.mapToScene(viewport.imageToItem({65, 75})) * window.effectiveDevicePixelRatio();
    const auto resultColor = resultLayers.pixelColor(qFloor(resultPoint.x()), qFloor(resultPoint.y()));
    require(resultColor.blue() - resultColor.red() > 100 && resultColor.green() - resultColor.red() > 50,
            "ROI editing hid output regions with declared matching input geometry");
    const auto unrelatedPoint = viewport.mapToScene(viewport.imageToItem({35, 75})) * window.effectiveDevicePixelRatio();
    const auto unrelatedColor = resultLayers.pixelColor(qFloor(unrelatedPoint.x()), qFloor(unrelatedPoint.y()));
    require(unrelatedColor.red() < 10 && unrelatedColor.green() < 10 && unrelatedColor.blue() < 10,
            "output ROI without declared matching coordinates leaked into the input editor");
    const auto cropOnInput = colorAt(resultLayers, {25, 25});
    require(cropOnInput.red() < 10 && cropOnInput.green() < 10 && cropOnInput.blue() < 10,
            "Region associated with cropped output coordinates leaked into the original input editor");
    require(QDir().mkpath(QStringLiteral(VISIONRT_BINARY_DIR "/evidence"))
                && resultLayers.save(QStringLiteral(VISIONRT_BINARY_DIR "/evidence/workbench_roi_layers_%1.png")
                                         .arg(window.effectiveDevicePixelRatio(), 0, 'f', 1)),
            "ROI layer evidence could not be saved");
    viewport.setDrawShape(0);
    QTest::mouseClick(&window, Qt::LeftButton, Qt::NoModifier,
                      viewport.mapToScene(viewport.imageToItem({65, 80})).toPoint());
    require(viewport.selectedRoi() == -1 && !app.dirty(), "read-only result geometry became editable");
    app.endRoiPreview();
    app.addNode("test.roi.editor", 1, 1);
    require(QTest::qWaitFor([&] { return !app.busy(); }, 5000), "node instance synchronization did not finish");
    require(app.bindInput(app.portContext(kImagesInput), source, kSourceImagesOutput) && app.bindInput(app.portContext(kRoisInput), source, kSourceRoisOutput),
            "upstream ROI verification inputs could not bind");
    app.runNode();
    require(QTest::qWaitFor([&] { return !app.busy(); }, 5000), "upstream ROI verification did not finish");
    require(output(kStatusCodePort) == "0" && output(kCountOutput) == "1", "editing copied ROIs modified their source cache");
    const auto verificationTarget = app.selectedNode();
    editor->setVisible(false);
    QQmlComponent panelComponent(&engine);
    panelComponent.setData("import QtQuick\nimport \".\"\nParameterPanel { width: 720; height: 520 }",
                           QUrl::fromLocalFile(QStringLiteral(VISIONRT_SOURCE_DIR "/src/ui/qml/RoiPanelContract.qml")));
    std::unique_ptr<QObject> panelOwner(panelComponent.create());
    if (!panelOwner) throw std::runtime_error(panelComponent.errorString().toStdString());
    auto *panel = qobject_cast<QQuickItem *>(panelOwner.get());
    require(panel != nullptr, "ROI parameter panel is unavailable");
    panel->setParentItem(window.contentItem());
    window.resize(1220, 520);
    viewport.setX(740);
    const auto click = [&](QQuickItem *item)
    {
        require(item && item->isVisible(), "ROI panel action is not visible");
        QTest::mouseClick(&window, Qt::LeftButton, Qt::NoModifier,
                         item->mapToScene(QPointF(item->width() / 2, item->height() / 2)).toPoint());
        QTest::qWait(30);
    };
    require(QMetaObject::invokeMethod(panel, "focusRoi"), "ROI toolbar command is unavailable");
    require(QTest::qWaitFor([&] { return viewport.editing(); }, 1000), "ROI toolbar entered direct drawing");
    app.endRoiPreview();
    app.runNode();
    require(QTest::qWaitFor([&] { return !app.busy(); }, 5000), "ROI toolbar copied input did not execute");
    require(output(kStatusCodePort) == "0" && output(kCountOutput) == "1", "ROI toolbar copy lost its upstream geometry");
    const auto rectLiteral = [&]
    {
        for (const auto &entry : app.parameters())
            if (entry.toMap().value("id").toUInt() == kRectInput) return entry.toMap().value("literal").toString();
        return QString{};
    };
    require(app.save(project) && !app.dirty(), "Rect baseline could not be saved");
    require(QTest::qWaitFor([&] { return !app.busy(); }, 5000), "post-save synchronization did not finish");
    const auto originalRectangle = rectLiteral();
    require(app.editRoi(kRectInput, false), "Scalar Rect could not open the image editor");
    require(QTest::qWaitFor([&] { return viewport.editing() && viewport.hasImage(); }, 3000),
            "Scalar Rect background was not presented");
    require(viewport.singleRectangle(), "Rect editor did not restrict shape and cardinality");
    const auto findCoordinate = [](auto &&self, QQuickItem *parent, const QString &name) -> QQuickItem *
    {
        for (auto *child : parent->childItems())
        {
            if (child->objectName() == name) return child;
            if (auto *found = self(self, child, name)) return found;
        }
        return nullptr;
    };
    require(findCoordinate(findCoordinate, panel, "roiCoordinate1") == nullptr,
            "Rect editing must not render the deleted coordinate spinbox form");
    require(app.setRoiRectangle(app.roiEditor().value("id").toString(), {6.125, 6.125, 30.0, 20.0, 0.0}),
            "Numeric commit did not update the same ROI session as the image editor");
    require(rectLiteral() != originalRectangle && app.dirty(), "Numeric ROI edit wrote the project directly");
    require(app.applyRoi(), "Numeric Apply succeeded");
    require(app.setParameter(kRectInput, false, originalRectangle), "Rect fixture reset was rejected");
    require(QTest::qWaitFor([&] { return !app.busy(); }, 5000), "Rect fixture reset did not settle");
    require(app.save(project), "Rect fixture could not reset");
    require(QTest::qWaitFor([&] { return !app.busy(); }, 5000), "Rect fixture reset did not settle");
    require(app.editRoi(kRectInput, false), "Rect could not reopen after numeric Apply");
    require(QTest::qWaitFor([&] { return viewport.editing() && viewport.hasImage(); }, 3000),
            "Rect background was not presented after numeric editing");
    QSignalSpy unchangedEdit(&viewport, &ImageViewport::editingChanged);
    QSignalSpy unchangedOutput(outputs, &QAbstractItemModel::modelReset);
    QTest::qWait(120);
    require(unchangedEdit.isEmpty() && unchangedOutput.isEmpty(),
            "Idle polling of an unchanged snapshot reset the active ROI gesture or output model");
    QSignalSpy rectangleGestures(&viewport, &ImageViewport::roiCommitted);
    viewport.setDrawShape(3);
    require(viewport.drawShape() != 3, "Rect editor accepted a circle drawing mode");
    viewport.setDrawShape(2);
    draw(false, {10, 10}, {30, 30}, true);
    require(rectLiteral() != originalRectangle && app.dirty(), "Rect gesture saved directly");
    require(findCoordinate(findCoordinate, panel, "roiCoordinate1") == nullptr,
            "Rectangle coordinate form must be deleted");
    if (rectangleGestures.count() != 1)
    {
        window.grabWindow().save(QStringLiteral(VISIONRT_BINARY_DIR "/evidence/roi-rect-failure.png"));
        throw std::runtime_error(QStringLiteral("Rect gesture count=%1, readOnly=%2, shape=%3, image=%4, enabled=%5")
                                     .arg(rectangleGestures.count()).arg(viewport.readOnly()).arg(viewport.drawShape())
                                     .arg(viewport.hasImage()).arg(viewport.isEnabled()).toStdString());
    }
    const auto beforeCanceledRedraw = app.roiEditor().value("rectangle").toList();
    viewport.setDrawShape(2);
    draw(true, {40, 40}, {60, 60});
    if (!(viewport.editing() && viewport.drawShape() == 0 && rectangleGestures.count() == 1
          && app.roiEditor().value("rectangle").toList() == beforeCanceledRedraw))
        throw std::runtime_error(QStringLiteral("Canceling a redraw did not restore selection mode and the previous "
                                                "rectangle: editing=%1 shape=%2 gestures=%3 before=%4 after=%5")
                                     .arg(viewport.editing())
                                     .arg(viewport.drawShape())
                                     .arg(rectangleGestures.count())
                                     .arg(QString::fromUtf8(QJsonDocument(QJsonArray::fromVariantList(beforeCanceledRedraw)).toJson(QJsonDocument::Compact)))
                                     .arg(QString::fromUtf8(QJsonDocument(QJsonArray::fromVariantList(app.roiEditor().value("rectangle").toList())).toJson(QJsonDocument::Compact)))
                                     .toStdString());
    viewport.setDrawShape(2);
    draw(false, {40, 40}, {40, 60});
    require(viewport.drawShape() == 2 && rectangleGestures.count() == 1
                && app.roiEditor().value("rectangle").toList() == beforeCanceledRedraw,
            "A zero-width redraw replaced the rectangle or stopped accepting a new redraw");
    const auto ungrabStart = viewport.mapToScene(viewport.imageToItem({40, 40})).toPoint();
    const auto ungrabEnd = viewport.mapToScene(viewport.imageToItem({60, 60})).toPoint();
    QTest::mousePress(&window, Qt::LeftButton, Qt::NoModifier, ungrabStart);
    QTest::mouseMove(&window, ungrabEnd);
    viewport.ungrabMouse();
    QTest::mouseRelease(&window, Qt::LeftButton, Qt::NoModifier, ungrabEnd);
    require(viewport.drawShape() == 0 && rectangleGestures.count() == 1
                && app.roiEditor().value("rectangle").toList() == beforeCanceledRedraw,
            "Losing the mouse grab did not restore the previous rectangle and selection mode");
    viewport.setDrawShape(2);
    draw(false, {15, 15}, {25, 25});
    require(rectangleGestures.count() == 2, "Explicit Rect redraw did not complete its gesture");
    const auto &redrawValue = rectangleGestures.last().at(1);
    require(redrawValue.metaType() == QMetaType::fromType<EncodedRegion>(),
            "Rect gesture did not carry typed geometry");
    const auto overlappingRedraw = *static_cast<const EncodedRegion *>(redrawValue.constData());
    require(std::abs(overlappingRedraw[1] - 15) < 1 && std::abs(overlappingRedraw[3] - 10) < 1,
            "Explicit redraw inside the old Rect must create replacement geometry, not resize a handle");
    viewport.setDrawShape(2);
    draw(false, {60, 60}, {90, 80});
    const auto beforeImageSwitch = app.roiEditor().value("rectangle").toList();
    QSignalSpy switchedImage(&viewport, &ImageViewport::imagePresented);
    app.setImageIndex(1);
    require(QTest::qWaitFor([&] { return !switchedImage.isEmpty() && viewport.hasImage(); }, 3000),
            "Second batch image was not presented");
    require(app.roiEditor().value("imageWidth").toInt() == 32
                && app.roiEditor().value("imageHeight").toInt() == 24
                && app.roiEditor().value("rectangle").toList() == beforeImageSwitch,
            "Switching to a smaller batch image scaled or clipped the shared rectangle");
    changes.clear();
    const bool applied = app.applyRoi();
    if (!applied)
        throw std::runtime_error((QStringLiteral("Rect Apply=%1: ").arg(applied)
                                 + app.diagnostic()).toStdString());
    const auto scalar   = ValueJson::parse(rectLiteral(), {Type::ROI});
    const auto geometry = std::get<DataRef<ROI>>(scalar).get()[0][0];
    if (!(geometry.size() == 6 && std::abs(geometry[1] - 60) < 1 && std::abs(geometry[2] - 60) < 1
          && std::abs(geometry[3] - 30) < 1 && std::abs(geometry[4] - 20) < 1))
        throw std::runtime_error((QStringLiteral("Rect redraw/batch switch geometry=%1, gestures=%2")
                                     .arg(rectLiteral()).arg(rectangleGestures.count())).toStdString());
    require(app.bindInput(app.portContext(kRectInput), source, kSourceRectOutput), "Scalar Rect could not bind to its upstream output");
    require(QTest::qWaitFor([&] { return !app.busy(); }, 5000), "Bound Rect baseline did not settle");
    require(app.save(project) && !app.dirty(), "Bound Rect baseline could not be saved");
    require(QTest::qWaitFor([&] { return !app.busy(); }, 5000), "Bound Rect baseline did not settle");
    const auto boundRectangle = rectLiteral();
    require(app.editRoi(kRectInput, true), "Scalar Rect upstream copy could not begin");
    require(QTest::qWaitFor([&] { return viewport.editing() && viewport.hasImage(); }, 3000),
            "Copied scalar Rect background was not presented");
    viewport.setDrawShape(2);
    const auto previousSession = app.roiEditor().value("id").toString();
    const auto previousPress = viewport.mapToScene(viewport.imageToItem({15, 15})).toPoint();
    const auto previousRelease = viewport.mapToScene(viewport.imageToItem({25, 25})).toPoint();
    QTest::mousePress(&window, Qt::LeftButton, Qt::NoModifier, previousPress);
    QTest::mouseMove(&window, previousRelease);
    app.endRoiPreview();
    require(rectLiteral() == boundRectangle && !app.dirty(), "Cancel saved or detached the copied scalar Rect");
    bool rectangleBound = false;
    for (const auto &entry : app.parameters())
        if (entry.toMap().value("id").toUInt() == kRectInput) rectangleBound = entry.toMap().value("mode") == "upstream";
    require(rectangleBound, "Cancel removed the scalar Rect upstream binding");
    const auto previousTarget = rectangleGestures.first().at(0).value<RoiEditTarget>();
    const auto previousGeometry
        = *static_cast<const EncodedRegion *>(rectangleGestures.first().at(1).constData());
    require(app.editRoi(kRectInput, true), "Scalar Rect could not reopen the same upstream-copy input");
    require(QTest::qWaitFor([&] { return viewport.editing() && viewport.hasImage(); }, 3000),
            "Reopened scalar Rect background was not presented");
    const auto reopenedRectangle = app.roiEditor().value("rectangle").toList();
    require(!previousSession.isEmpty() && previousSession != app.roiEditor().value("id").toString(),
            "Reopening the same ROI parameter reused its editing identity");
    const auto beforePreviousRelease = rectangleGestures.count();
    QTest::mouseRelease(&window, Qt::LeftButton, Qt::NoModifier, previousRelease);
    require(rectangleGestures.count() == beforePreviousRelease
                && app.roiEditor().value("rectangle").toList() == reopenedRectangle,
            "A canceled session's mouse release completed geometry in the reopened session");
    bool deliveredPrevious = false;
    QMetaObject::invokeMethod(&viewport, [&] {
        emit viewport.roiCommitted(previousTarget, previousGeometry);
        emit viewport.roiDeleted(previousTarget);
        deliveredPrevious = true;
    }, Qt::QueuedConnection);
    require(QTest::qWaitFor([&] { return deliveredPrevious; }, 1000), "Previous ROI callback was not delivered");
    require(app.roiEditor().value("rectangle").toList() == reopenedRectangle,
            "A previous ROI session's queued gesture changed the reopened input draft");
    require(!app.setRoiRectangle(previousSession, {0, 0, 12, 16, 0})
                && app.roiEditor().value("rectangle").toList() == reopenedRectangle,
            "The native numeric entry accepted a previous ROI session");
    app.endRoiPreview();
    require(rectLiteral() == boundRectangle && !app.dirty(), "Stale gesture changed the saved input binding");
    app.addNode("test.roi.editor", 0, 2);
    require(QTest::qWaitFor([&] { return !app.busy(); }, 5000), "node instance synchronization did not finish");
    const auto withoutImage = app.selectedNode();
    require(app.save(project) && !app.dirty(), "Image-free rectangle baseline could not be saved");
    const auto imageFreeOriginal = rectLiteral();
    require(app.editRoi(kRectInput, false), "Rect numeric editing must not require a cached image");
    require(app.property("roiEditor").toMap().value("node").toString() == withoutImage
                && app.property("roiEditor").toMap().value("imageWidth").toInt() == 0
                && app.imageCount() == 0 && !viewport.hasImage(),
            "Image-free Rect editing invented a background or selected another parameter");
    require(findCoordinate(findCoordinate, panel, "roiCoordinate1") == nullptr,
            "Image-free rectangle must not expose numeric coordinates");
    require(app.setRoiRectangle(app.roiEditor().value("id").toString(), {0.0, 9.25, 30.0, 20.0, 0.0}), "Set ROI rectangle");
    require(rectLiteral() != imageFreeOriginal && app.dirty(), "Image-free fields saved directly");
    changes.clear();
    require(app.applyRoi(), "Image-free numeric Apply succeeded");
    const auto imageFreeValue = std::get<DataRef<ROI>>(ValueJson::parse(rectLiteral(), {Type::ROI})).get()[0][0];
    require(imageFreeValue[2] == 9.25f, "Image-free numeric geometry was lost");
    require(app.save(project), "Image-free Rect could not be saved");
    require(QTest::qWaitFor([&] { return !app.busy(); }, 5000), "Image-free Rect save did not settle");
    require(app.editRoi(kRectInput, false), "Image-free Rect could not reopen");
    const auto previousImageFreeSession = app.roiEditor().value("id").toString();
    app.endRoiPreview();
    require(app.editRoi(kRectInput, false), "Image-free Rect could not start a new session for the same parameter");
    const auto staleValidation = QObject::connect(&app, &AppController::roiApplyRequested, &app, [&] {
        app.setRoiFieldsValid(previousImageFreeSession, false);
    });
    const bool unaffectedApply = app.applyRoi();
    QObject::disconnect(staleValidation);
    require(unaffectedApply && !app.dirty(), "A previous ROI field validation blocked applying the current session");
    require(app.editRoi(kRectInput, false), "Image-free Rect could not reopen after unchanged Apply");
    require(app.setRoiRectangle(app.roiEditor().value("id").toString(), {0, 0, 8, 6, 30}),
            "Image-free draft could change");
    require(app.dirty(), "Image-free numeric edit directly updated project");
    app.endRoiPreview();
    require(app.setParameter(kRectInput, false, "[]"), "Empty ROI rectangle baseline was rejected");
    require(QTest::qWaitFor([&] { return !app.busy(); }, 5000), "Empty ROI rectangle baseline did not settle");
    require(app.save(project), "Empty ROI rectangle baseline could not be saved");
    require(QTest::qWaitFor([&] { return !app.busy(); }, 5000), "Empty ROI rectangle save did not settle");
    require(app.editRoi(kRectInput, false), "Empty ROI rectangle could not enter image-free numeric editing");
    QQuickItem *createRectangle  = nullptr;
    QList<QQuickItem *> pendingItems{panel};
    while (!pendingItems.isEmpty())
    {
        auto *item = pendingItems.takeLast();
        if (item->isVisible() && item->property("text").toString() == QStringLiteral("新建矩形"))
            createRectangle = item;
        pendingItems.append(item->childItems());
    }
    require(createRectangle == nullptr, "Empty ROI rectangle must not offer 新建矩形 button");
    require(app.setRoiRectangle(app.roiEditor().value("id").toString(), {0, 0, 8, 6, 30}), "Set ROI rectangle");
    const auto rectList = app.property("roiEditor").toMap().value("rectangle").toList();
    const bool hasApplied = app.applyRoi();
    require(rectList.size() == 5 && app.dirty() && hasApplied, "Image-free Rect creation did not produce a valid draft");
    if (!app.save(project))
        throw std::runtime_error("Image-free rectangle could not save: " + app.diagnostic().toStdString());

    app.requestOpen(project);
    require(QTest::qWaitFor([&] { return !app.busy(); }, 5000), "Image-free rectangle reopen did not finish");
    app.selectNode(withoutImage);
    require(std::holds_alternative<DataRef<ROI>>(ValueJson::parse(rectLiteral(), {Type::ROI})),
            "Saved image-free Rect lost its scalar type");
    const auto beforeBackground = rectLiteral();
    app.selectNode(source);
    app.runNode();
    require(QTest::qWaitFor([&] { return !app.busy(); }, 5000) && output(kStatusCodePort) == "0",
            "Image source could not republish after reopening the project");
    app.selectNode(withoutImage);
    require(app.editRoi(kRectInput, false) && !viewport.hasImage() && app.imageCount() == 0,
            "Unbound rectangle input unexpectedly reused another node's cached background");
    const auto imageFreeSession = app.roiEditor().value("id").toString();
    require(app.setRoiRectangle(imageFreeSession, {7, 8, 9, 10, 25}),
            "Image-free background transition could not stage numeric geometry");
    const auto imageFreeDraft = app.roiEditor().value("rectangle").toList();
    QSignalSpy rejectedRectangleChanges(&app, &AppController::parametersChanged);
    require(!app.setRoiRectangle(imageFreeSession, {1, 2, 3, 4})
                && app.diagnostic().contains(QStringLiteral("矩形参数 端口 3"))
                && app.diagnostic().contains(QStringLiteral("实际 4 项")),
            "Malformed rectangle lost its parameter identity or actual coordinate count");
    require(!app.setRoiRectangle(imageFreeSession, {1, 2, std::numeric_limits<double>::infinity(), 4, 5})
                && app.diagnostic().contains(QStringLiteral("矩形参数 端口 3 的坐标[2]"))
                && app.diagnostic().contains(QStringLiteral("有限数值")),
            "Non-finite rectangle lost its parameter identity or invalid coordinate index");
    require(app.roiEditor().value("id").toString() == imageFreeSession
                && app.roiEditor().value("rectangle").toList() == imageFreeDraft && rejectedRectangleChanges.isEmpty(),
            "Rejected rectangle coordinates changed the active draft or committed a parameter");
    const auto beforeImageFreeDraw = rectangleGestures.count();
    viewport.setDrawShape(2);
    const auto emptyStart = viewport.mapToScene(QPointF(100, 100)).toPoint();
    const auto emptyEnd = viewport.mapToScene(QPointF(180, 180)).toPoint();
    QTest::mousePress(&window, Qt::LeftButton, Qt::NoModifier, emptyStart);
    QTest::mouseMove(&window, emptyEnd);
    QTest::mouseRelease(&window, Qt::LeftButton, Qt::NoModifier, emptyEnd);
    require(rectangleGestures.count() == beforeImageFreeDraw && !viewport.hasImage()
                && app.roiEditor().value("rectangle").toList() == imageFreeDraft && app.dirty(),
            "Pointer drawing without a background invented image geometry or changed the numeric draft");
    const RoiEditTarget oldBackgroundTarget{withoutImage.toULongLong(), kRectInput, 0, 0, imageFreeSession.toULongLong()};
    require(app.bindInput(app.portContext(kImagesInput), source, kSourceImagesOutput),
            "Image-free rectangle input could not acquire a real upstream image");
    const auto currentRect = rectLiteral();
    require(app.roiEditor().isEmpty() && !viewport.editing() && !app.applyRoi() && rectLiteral() == currentRect,
            "Acquiring a background preserved or committed the image-free ROI session");
    require(app.save(project) && !app.dirty(), "Newly bound rectangle baseline could not be saved");
    require(QTest::qWaitFor([&] { return !app.busy(); }, 5000), "Newly bound rectangle save did not settle");
    require(app.editRoi(kRectInput, false), "Rectangle editor could not reopen against the newly bound image");
    require(QTest::qWaitFor([&] { return viewport.editing() && viewport.hasImage(); }, 3000)
                && app.imageCount() == 2 && app.roiEditor().value("imageWidth").toInt() == 128
                && app.roiEditor().value("imageHeight").toInt() == 96,
            "New ROI session did not present the actual newly bound background");
    const auto acquiredSession = app.roiEditor().value("id").toString();
    const auto acquiredRectangle = app.roiEditor().value("rectangle").toList();
    require(acquiredSession != imageFreeSession && !app.setRoiRectangle(imageFreeSession, {50, 50, 5, 5, 0}),
            "New background accepted the old image-free numeric session");
    bool oldBackgroundDelivered = false;
    QMetaObject::invokeMethod(
        &viewport,
        [&]
        {
            emit viewport.roiCommitted(oldBackgroundTarget, EncodedRegion{2, 50, 50, 5, 5, 0});
            emit viewport.roiDeleted(oldBackgroundTarget);
            oldBackgroundDelivered = true;
        },
        Qt::QueuedConnection);
    require(QTest::qWaitFor([&] { return oldBackgroundDelivered; }, 1000)
                && app.roiEditor().value("rectangle").toList() == acquiredRectangle
                && rectLiteral() == currentRect && !app.dirty(),
            "Late image-free events modified the replacement background's session");
    app.endRoiPreview();
    require(!app.dirty() && rectLiteral() == currentRect,
            "Cancel after background acquisition changed the saved rectangle");
    require(app.editRoi(kRectInput, false), "Rectangle could not reopen for execution cancellation");
    require(QTest::qWaitFor([&] { return viewport.editing() && viewport.hasImage(); }, 3000),
            "Execution cancellation editor did not present its background");
    const auto beforeExecutionSession = app.roiEditor().value("id").toString();
    require(app.setRoiRectangle(beforeExecutionSession, {10, 10, 20, 20, 0}),
            "Execution cancellation could not stage an ROI draft");
    const auto executeStart = viewport.mapToScene(viewport.imageToItem({20, 20})).toPoint();
    const auto executeEnd = viewport.mapToScene(viewport.imageToItem({25, 25})).toPoint();
    QTest::mousePress(&window, Qt::LeftButton, Qt::NoModifier, executeStart);
    QTest::mouseMove(&window, executeEnd);
    app.runNode();
    require(app.roiEditor().isEmpty() && !viewport.editing() && !app.applyRoi(),
            "Starting execution retained an active ROI session or submitted its draft");
    require(QTest::qWaitFor([&] { return !app.busy(); }, 5000) && output(kStatusCodePort) == "0",
            "Execution after ROI cancellation did not publish a successful result");
    QTest::mouseRelease(&window, Qt::LeftButton, Qt::NoModifier, executeEnd);
    require(!app.setRoiRectangle(beforeExecutionSession, {99, 99, 1, 1, 0})
                && app.dirty() && rectLiteral() != currentRect,
            "A pre-execution gesture or numeric callback changed the saved rectangle after new results");
    require(app.setParameter(kRectInput, false, "[]"), "Rect clear was rejected");
    require(QTest::qWaitFor([&] { return !app.busy(); }, 5000), "Rect clear did not settle");
    require(app.save(project) && !app.dirty(),
            "Empty rectangle mode baseline could not be saved");
    const auto missingRectangle = rectLiteral();
    require(app.editRoi(kRectInput, false), "Empty rectangle could not enter image drawing mode");
    require(QTest::qWaitFor([&] { return viewport.editing() && viewport.hasImage(); }, 3000)
                && viewport.singleRectangle() && viewport.drawShape() == 2 && viewport.selectedRoi() == -1,
            "Empty rectangle did not start with one pending rectangle and no selection");
    const auto beforeModeGestures = rectangleGestures.count();
    draw(true, {10, 10}, {30, 30});
    draw(false, {10, 10}, {10, 30});
    require(rectangleGestures.count() == beforeModeGestures && viewport.drawShape() == 2
                && viewport.selectedRoi() == -1 && app.roiEditor().value("rectangle").toList().isEmpty(),
            "Canceled or zero-width initial drawing created a rectangle or stopped accepting drawing");
    draw(false, {10, 10}, {30, 30});
    require(rectangleGestures.count() == beforeModeGestures + 1 && viewport.drawShape() == 0
                && viewport.selectedRoi() == 0 && app.roiEditor().value("rectangle").toList().size() == 5,
            "Initial rectangle drawing did not switch to selecting exactly one rectangle");
    draw(false, {20, 20}, {30, 25});
    const auto movedRectangle = app.roiEditor().value("rectangle").toList();
    require(rectangleGestures.count() == beforeModeGestures + 2 && viewport.drawShape() == 0
                && movedRectangle.size() == 5 && std::abs(movedRectangle[0].toDouble() - 20) < 0.5
                && std::abs(movedRectangle[1].toDouble() - 15) < 0.5
                && std::abs(movedRectangle[2].toDouble() - 20) < 0.5
                && std::abs(movedRectangle[3].toDouble() - 20) < 0.5,
            "Dragging a newly created rectangle replaced its dimensions instead of moving it");
    QTest::mouseClick(&window, Qt::LeftButton, Qt::NoModifier,
                      viewport.mapToScene(viewport.imageToItem({60, 40})).toPoint());
    require(viewport.selectedRoi() == -1 && viewport.drawShape() == 0,
            "Clicking blank image space started drawing or kept an editable selection");
    QTest::keyClick(&window, Qt::Key_Delete);
    require(app.roiEditor().value("rectangle").toList() == movedRectangle,
            "Delete without an editable selection removed rectangle geometry");
    QTest::mouseClick(&window, Qt::LeftButton, Qt::NoModifier,
                      viewport.mapToScene(viewport.imageToItem({30, 25})).toPoint());
    require(viewport.selectedRoi() == 0, "New rectangle could not be reselected before deletion");
    QTest::keyClick(&window, Qt::Key_Delete);
    require(QTest::qWaitFor([&] { return viewport.selectedRoi() == -1 && viewport.drawShape() == 2; }, 1000)
                && app.roiEditor().value("rectangle").toList().isEmpty(),
            "Deleting the selected rectangle retained its selection or old drawing mode");
    const auto afterDeletion = colorAt(renderedFrame(), {20, 15});
    require(afterDeletion.red() < 10 && afterDeletion.green() < 10 && afterDeletion.blue() < 10,
            "Deleting a rectangle left its old handle on the image");
    require(rectLiteral() == missingRectangle && app.dirty() && output(kStatusCodePort) == "0" && !app.busy(),
            "Rectangle preview executed the tool or replaced the last successful publication");
    require(app.save(project) && !app.dirty(), "Save after delete");
    app.endRoiPreview();
    require(!viewport.editing() && viewport.selectedRoi() == -1 && viewport.drawShape() == 0
                && app.roiEditor().isEmpty() && rectLiteral() == missingRectangle && !app.dirty(),
            "Canceling a Empty rectangle left drawing mode or editing attachments active");
    require(app.editRoi(kRectInput, false), "Empty rectangle could not reopen after Cancel");
    require(QTest::qWaitFor([&] { return viewport.editing() && viewport.hasImage(); }, 3000)
                && viewport.drawShape() == 2 && app.applyRoi(),
            "Unchanged Empty rectangle could not apply without drawing");
    require(!viewport.editing() && viewport.selectedRoi() == -1 && viewport.drawShape() == 0
                && app.roiEditor().isEmpty() && rectLiteral() == missingRectangle && !app.dirty()
                && output(kStatusCodePort) == "0" && !app.busy(),
            "Applying an unchanged Empty rectangle retained drawing mode, executed or replaced the publication");
    require(app.editRoi(kRectInput, false), "Rectangle could not reopen for window deactivation");
    require(QTest::qWaitFor([&] { return viewport.editing() && viewport.hasImage(); }, 3000),
            "Window deactivation editor did not present its background");
    const auto windowSession = app.roiEditor().value("id").toString();
    require(app.setRoiRectangle(windowSession, {12.0, 14.0, 30.0, 20.0, 0.0}),
            "Window deactivation could not stage numeric geometry");
    QQuickWindow otherWindow;
    otherWindow.resize(180, 100);
    otherWindow.show();
    otherWindow.requestActivate();
    require(QTest::qWaitForWindowActive(&otherWindow, 1000)
                && qGuiApp->applicationState() == Qt::ApplicationActive && !window.isActive(),
            "Window deactivation did not activate another independent window in the active application");
    require(QTest::qWaitFor([&] { return app.roiEditor().isEmpty() && !viewport.editing(); }, 1000)
                && viewport.drawShape() == 0 && viewport.selectedRoi() == -1
                && app.dirty(),
            "Deactivating the ROI window retained the draft while another application window was active");
    otherWindow.close();
    window.requestActivate();
    require(QTest::qWaitForWindowActive(&window, 1000)
                && app.roiEditor().isEmpty() && !viewport.editing(),
            "Returning to the ROI window restored a canceled session");
    require(app.editRoi(kRectInput, false), "Rectangle could not reopen after window deactivation");
    const auto reopenedWindowSession = app.roiEditor().value("id").toString();
    require(reopenedWindowSession != windowSession
                && !app.setRoiRectangle(windowSession, {1.0, 2.0, 3.0, 4.0, 0.0})
                && app.roiEditor().value("rectangle").toList().size() == 5,
            "A canceled window session's numeric callback modified the reopened rectangle");
    require(app.save(project) && !app.dirty(), "Save window session state");
    require(QTest::qWaitFor([&] { return !app.busy(); }, 5000), "Window session save did not settle");
    require(app.setRoiRectangle(reopenedWindowSession, {12.0, 14.0, 30.0, 20.0, 0.0}),
            "Transient window test could not stage rectangle geometry");
    const auto transientRectangle = app.roiEditor().value("rectangle").toList();
    QQuickWindow transientWindow;
    transientWindow.setTransientParent(&window);
    transientWindow.resize(180, 100);
    transientWindow.show();
    transientWindow.requestActivate();
    require(QTest::qWaitFor([&] { return QGuiApplication::focusWindow() == &transientWindow; }, 1000)
                && window.isActive()
                && viewport.editing() && app.roiEditor().value("id").toString() == reopenedWindowSession
                && app.roiEditor().value("rectangle").toList() == transientRectangle,
            "Activating a transient child window canceled its owner's ROI session");
    transientWindow.close();
    window.requestActivate();
    require(QTest::qWaitFor([&] { return QGuiApplication::focusWindow() == &window; }, 1000) && viewport.editing(),
            "Closing a transient child window canceled its owner's ROI session");
    app.endRoiPreview();
    for (const bool copyUpstream : {false, true})
    {
        if (copyUpstream)
        {
            require(app.bindInput(app.portContext(kRectInput), source, kSourceRectOutput),
                    "Window gesture test could not bind an upstream rectangle");
            require(QTest::qWaitFor([&] { return !app.busy(); }, 5000),
                    "Window gesture upstream binding did not settle");
            require(app.save(project) && !app.dirty(),
                    "Window gesture test could not save an upstream rectangle binding");
        }
        else
        {
            require(app.save(project) && !app.dirty(), "Save literal binding baseline");
        }
        require(QTest::qWaitFor([&] { return !app.busy(); }, 5000), "Window gesture baseline did not settle");
        const auto windowLiteral = rectLiteral();
        const auto windowWires = app.wires();
        require(app.editRoi(kRectInput, copyUpstream), "Window gesture rectangle could not open");
        require(QTest::qWaitFor([&] { return viewport.editing() && viewport.hasImage(); }, 3000),
                "Window gesture rectangle did not present its background");
        const auto gestureSession = app.roiEditor().value("id").toString();
        renderedFrame();
        const auto gestureStart = viewport.mapToScene(viewport.imageToItem({25, 25})).toPoint();
        const auto gestureEnd = viewport.mapToScene(viewport.imageToItem({35, 30})).toPoint();
        viewport.forceActiveFocus();
        QTest::mousePress(&window, Qt::LeftButton, Qt::NoModifier, gestureStart);
        QTest::mouseMove(&window, gestureEnd, 20);
        const auto gestureCount = rectangleGestures.count();
        otherWindow.show();
        otherWindow.requestActivate();
        require(QTest::qWaitForWindowActive(&otherWindow, 1000)
                    && qGuiApp->applicationState() == Qt::ApplicationActive,
                "Window gesture test did not activate the independent window");
        require(QTest::qWaitFor([&] { return !viewport.editing() && app.roiEditor().isEmpty(); }, 1000)
                    && viewport.drawShape() == 0 && viewport.selectedRoi() == -1
                    && rectLiteral() == windowLiteral && app.wires() == windowWires && !app.dirty()
                    && rectangleGestures.count() == gestureCount,
                "Window deactivation committed an active gesture or detached the copied upstream rectangle");
        otherWindow.close();
        window.requestActivate();
        require(QTest::qWaitForWindowActive(&window, 1000) && app.editRoi(kRectInput, copyUpstream),
                "Rectangle could not reopen after deactivation during a gesture");
        const auto reopenedGeometry = app.roiEditor().value("rectangle").toList();
        require(app.roiEditor().value("id").toString() != gestureSession,
                "Window gesture reopen reused the canceled session identity");
        QTest::mouseRelease(&window, Qt::LeftButton, Qt::NoModifier, gestureEnd);
        require(!app.setRoiRectangle(gestureSession, {60.0, 60.0, 5.0, 5.0, 0.0})
                    && app.roiEditor().value("rectangle").toList() == reopenedGeometry
                    && rectangleGestures.count() == gestureCount && rectLiteral() == windowLiteral
                    && app.wires() == windowWires && !app.dirty(),
                "A canceled window gesture's late release or numeric callback modified the reopened session");
        app.endRoiPreview();
    }
    std::cout << "[ROI owning window] Independent-window activation cancels numeric and active pointer drafts, preserves local/upstream inputs and rejects late callbacks; transient children preserve the session.\n";
    std::cout << "[ROI single rectangle] Creation enters move mode; deletion clears fields and handles; Apply and Cancel end drawing without executing.\n";
    std::cout << "[ROI execution] Starting execution cancels an in-progress draft; late release and numeric edits cannot modify the new result.\n";
    std::cout << "[ROI background] Acquiring an upstream image cancels image-free drafts; reopened sessions reject late geometry and numeric callbacks.\n";
    std::cout << "[Evidence T-05] ROI toolbar copy honors panel enablement and node identity, then executes the copied input.\n";
    std::cout << "[Evidence T-05] ROI action, pointer edits, Escape, read-only, upstream copy isolation, save/reopen and Python execution verified.\n";
}
