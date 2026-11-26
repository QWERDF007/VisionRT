#include "core/ValueJson.h"
#include "ui/AppController.h"
#include "ui/ImageViewport.h"
#include "ui/OutputModel.h"
#include "ui/UiTypes.h"

#include <quickui/Color.h>

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QGuiApplication>
#include <QJsonArray>
#include <QJsonDocument>
#include <QQmlComponent>
#include <QQmlContext>
#include <QQmlEngine>
#include <QQuickItem>
#include <QQuickWindow>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QTest>
#include <QtMath>
#include <iostream>
#include <memory>
#include <stdexcept>

using namespace visionrt;

// test.roi.editor: 图像 1, 区域 2, 矩形 3, 参考区域 4; its business outputs follow 4..10.
// test.roi.source publishes 图像 2, 源区域 3, 源矩形 4.
constexpr PortId kImagesInput             = 1;
constexpr PortId kRoisInput               = 2;
constexpr PortId kRectInput               = 3;
constexpr PortId kCircleInput             = 5;
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
        if (!condition)
            throw std::runtime_error(message);
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
          "4: [[np.array([1.25, 2.5, 3.75, 4.5, 17.5], dtype=np.float64).tolist()]], "
          "3: [[np.array([40, 40, 10, 10, 0], dtype=np.float64).tolist()]], "
          "0: 0, 1: ''}, 'detail': ''}\n");
    write("tools/custom/roi/source.json",
          R"json({
  "name_en": "test.roi.source",
  "name_zh": "ROI图像源",
  "version": "1.0.0",
  "implementation": {
    "kind": "python_function",
    "package": "roi",
    "file": "source.py",
    "symbol": "run"
  },
  "outputs": [
    {
      "name_zh": "图像",
      "type": "image",
      "display": {
        "color_order": "bgr",
        "visible": true
      },
      "id": 2
    },
    {
      "name_zh": "源区域",
      "type": "rectangle",
      "id": 3
    },
    {
      "name_zh": "源矩形",
      "type": "rectangle",
      "relations": {
        "image": {
          "stage": "outputs",
          "id": 2
        }
      },
      "id": 4
    }
  ],
  "schema_version": 4
})json");
    write("python/custom/roi/editor.py",
          "import numpy as np\ndef run(inputs):\n"
          "    return {'outputs': {2: len(inputs[2][0]), "
          "3: inputs[1], 4: [[np.array([60, 75, 15, 10, 0], dtype=np.float64).tolist()], []], "
          "5: inputs[1][:1], 6: [], "
          "7: [image[5:, 10:] for image in inputs[1]], "
          "8: [[np.array([25, 20, 10, 10, 0], dtype=np.float64).tolist()], []], "
          "9: np.array([[10, 5] for image in inputs[1]], dtype=np.float64).tolist(), "
          "10: [[np.array([30, 75, 15, 10, 0], dtype=np.float64).tolist()]], "
          "0: 0, 1: ''}, 'detail': ''}\n");
    write("tools/custom/roi/editor.json",
          R"json({
  "name_en": "test.roi.editor",
  "name_zh": "ROI编辑",
  "version": "1.0.0",
  "implementation": {
    "kind": "python_function",
    "package": "roi",
    "file": "editor.py",
    "symbol": "run"
  },
  "inputs": [
    {
      "name_zh": "图像",
      "type": "image",
      "id": 1
    },
    {
      "name_zh": "区域",
      "type": "rectangle",
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
      "type": "rectangle",
      "default": [
        [
          [
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
      "type": "rectangle",
      "default": [
        [
          [
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
    },
    {
      "id": 5,
      "name_zh": "圆",
      "type": "circle",
      "default": [],
      "relations": {
        "image": {
          "stage": "inputs",
          "id": 1
        }
      }
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
        "color_order": "bgr",
        "visible": true
      },
      "id": 3
    },
    {
      "name_zh": "结果区域",
      "type": "rectangle",
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
      "type": "rectangle",
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
      "type": {
        "type": "array",
        "dtype": "float64",
        "rank": 2
      },
      "id": 9
    },
    {
      "name_zh": "其他坐标区域",
      "type": "rectangle",
      "relations": {
        "image": {
          "stage": "outputs",
          "id": 5
        }
      },
      "id": 10
    }
  ],
  "schema_version": 4
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
        throw std::runtime_error(
            (QStringLiteral("ROI numpy image source failed: ") + output(kStatusMessagePort) + " " + app.diagnostic())
                .toStdString());
    app.addNode("test.roi.editor", 0, 1);
    require(QTest::qWaitFor([&] { return !app.busy(); }, 5000), "node instance synchronization did not finish");
    const auto target = app.selectedNode();
    require(target != source && app.bindInput(app.portContext(kImagesInput), source, kSourceImagesOutput),
            "ROI image input could not bind to the source");
    require(app.setParameter(kRoisInput, false, "[[[0,1.123456789,2,3,4]]]"), "point parameter could not be set");
    const auto literal = [&]
    {
        for (const auto &entry : app.parameters())
            if (entry.toMap().value("id").toUInt() == kRoisInput)
                return entry.toMap().value("literal").toString();
        return QString{};
    };
    QQmlEngine engine;
    quickui::QuiColor::getInstance()->setThemeMode(quickui::QuiColor::Light);
    registerUiTypes();
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
    if (!owner)
        throw std::runtime_error(component.errorString().toStdString());
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
    QObject *geometryField = nullptr;
    for (auto *item : owner->findChildren<QQuickItem *>())
        if (item->metaObject()->indexOfProperty("inputItem") >= 0
            && item->property("text").toString() == "[[[0,1.123457,2,3,4]]]")
            geometryField = item;
    require(geometryField != nullptr, "ROI field replaced the coordinates with a summary");
    auto *geometryInput = geometryField->property("inputItem").value<QObject *>();
    require(geometryInput && geometryInput->property("cursorPosition").toInt() == 0,
            "unfocused ROI field starts at the end of its coordinates");
    {
        const auto original = literal();
        QSignalSpy noOpChanges(&app, &AppController::parametersChanged);
        QMetaObject::invokeMethod(geometryField, "commitRequested");
        require(noOpChanges.isEmpty(), "untouched ROI text rewrote the parameter");
        auto *input = qobject_cast<QQuickItem *>(geometryInput);
        require(input != nullptr, "ROI text input is unavailable");
        input->forceActiveFocus();
        QTest::keyClick(&window, Qt::Key_Return);
        viewport.forceActiveFocus();
        QTest::qWait(20);
        require(literal() == original && noOpChanges.isEmpty(), "rounded ROI display changed the unedited value");
    }
    geometryField->setProperty("text", "[[[0,1,2,3,8]]]");
    QMetaObject::invokeMethod(geometryField, "commitRequested");
    require(QJsonDocument::fromJson(literal().toUtf8()).array()[0].toArray()[0].toArray()[4].toInt() == 8,
            "ROI text editing did not commit the coordinate");
    require(app.setParameter(kRoisInput, false, "[[]]"), "ROI editing fixture could not be cleared");
    const auto findAction = [&]
    {
        QQuickItem         *found = nullptr;
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
    const auto guidePoint   = viewport.mapToScene(viewport.imageToItem({90, 50})) * window.effectiveDevicePixelRatio();
    const auto guideColor   = layeredImage.pixelColor(qFloor(guidePoint.x()), qFloor(guidePoint.y()));
    require(guideColor.blue() > 180 && guideColor.red() < 100 && guideColor.green() > 60,
            "editing one ROI parameter hid another input's read-only geometry");
    QTest::mouseClick(&window, Qt::LeftButton, Qt::NoModifier,
                      viewport.mapToScene(viewport.imageToItem({90, 60})).toPoint());
    require(QTest::qWaitFor([&] { return app.activeRoiParameter() == 4 && viewport.selectedRoi() == 0; }, 3000),
            "clicking a literal ROI must select its actual input");
    require(app.editRoi(kRoisInput, true), "the original ROI input must remain editable");
    require(QTest::qWaitFor([&] { return viewport.editTarget() && viewport.editTarget()->inputPortId == kRoisInput; },
                            3000),
            "the selected input frame did not arrive");
    viewport.setDrawShape(2);
    const auto project = directory.filePath("roi.vrtapp");
    require(app.save(project) && !app.dirty(), "ROI baseline could not be saved");
    require(QTest::qWaitFor([&] { return app.workspaceEnabled(); }, 5000), "saved ROI workspace did not settle");
    require(QTest::qWaitFor([&] { return !app.busy(); }, 5000), "saved project instances did not synchronize");
    const auto initial = literal();
    QSignalSpy changes(&app, &AppController::projectChanged);
    const auto draw = [&](bool cancel, QPointF from = {10, 10}, QPointF to = {30, 30}, bool holdForPolling = false)
    {
        viewport.forceActiveFocus();
        const auto start = viewport.mapToScene(viewport.imageToItem(from)).toPoint();
        const auto end   = viewport.mapToScene(viewport.imageToItem(to)).toPoint();
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
        if (cancel)
            QTest::keyClick(&window, Qt::Key_Escape);
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
    auto       committed = literal();
    const auto rois      = std::get<visionrt::Rectangle>(ValueJson::parse(committed, {Type::Rectangle}));
    require(rois.get().size() == 1 && rois.get()[0].size() == 1 && roiKind(rois.get()[0][0]) == RoiKind::Rectangle,
            "ROI parameter lost its typed rectangle representation");
    require(app.save(project) && !app.dirty(), "ROI project could not be saved");
    require(QTest::qWaitFor([&] { return app.workspaceEnabled(); }, 5000), "saved ROI workspace did not settle");
    changes.clear();
    require(app.editRoi(kRoisInput, false), "Saved ROI could not reopen for read-only testing");
    require(QTest::qWaitFor(
                [&]
                {
                    return viewport.hasImage() && viewport.editTarget()
                        && viewport.editTarget()->inputPortId == kRoisInput;
                },
                3000),
            "Read-only ROI background was not presented");
    viewport.setReadOnly(true);
    viewport.setDrawShape(2);
    draw(false);
    QTest::keyClick(&window, Qt::Key_Delete);
    require(literal() == committed && !app.dirty() && changes.isEmpty(), "read-only ROI editor changed the project");
    viewport.setReadOnly(false);
    app.endRoiPreview();
    require(app.bindInput(app.portContext(kRoisInput), source, kSourceRoisOutput),
            "ROI input could not bind to the upstream ROI output");
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
    QTest::qWait(150);
    const auto copiedAfterDelete = renderedFrame();
    const auto deletedBoundary   = colorAt(copiedAfterDelete, {10, 20});
    const auto retainedBoundary  = colorAt(copiedAfterDelete, {40, 45});
    require(viewport.selectedRoi() == -1 && deletedBoundary.red() < 10 && deletedBoundary.green() < 10
                && deletedBoundary.blue() < 10 && retainedBoundary.blue() > 180 && retainedBoundary.red() < 100
                && retainedBoundary.green() > 60,
            "Deleting a copied ROI did not remove only the selected draft region and its handles");
    require(app.wires().first().toMap().value("count").toInt() == 1,
            "Deleting a copied ROI detached its upstream binding");
    viewport.setDrawShape(2);
    draw(false);
    const auto edited = std::get<visionrt::Rectangle>(ValueJson::parse(literal(), {Type::Rectangle}));
    require(edited.get().size() == 1 && edited.get()[0].size() == 2,
            "copied ROI input could not be edited independently");
    committed = literal();
    require(app.save(project), "copied ROI project could not be saved");
    require(QTest::qWaitFor([&] { return app.workspaceEnabled(); }, 5000), "saved ROI workspace did not settle");
    app.endRoiPreview();
    app.requestOpen(project);
    require(QTest::qWaitFor([&] { return !app.busy(); }, 3000), "ROI project did not reopen");
    app.selectNode(target);
    require(literal() == committed, "ROI parameter changed after save/reopen");
    app.setImageOutput(imageKey(kUnavailableImagesOutput));
    app.runOnce();
    require(QTest::qWaitFor([&] { return !app.busy(); }, 5000), "ROI workflow did not finish");
    require(output(kStatusCodePort) == "0" && output(kCountOutput) == "2",
            "Python did not receive the saved typed ROI");
    QSignalSpy matchingPresented(&viewport, &ImageViewport::imagePresented);
    app.setImageOutput(imageKey(kImagesOutput));
    app.setImageIndex(0);
    require(QTest::qWaitFor([&] { return !matchingPresented.isEmpty() && viewport.hasImage() && !viewport.editing(); },
                            3000),
            "Normal result view did not present its requested image before overlay inspection");
    const auto matchingOutput = renderedFrame();
    require(app.selectedImageOutput() == imageKey(kImagesOutput) && viewport.hasImage() && !viewport.editing(),
            "normal result view did not select the declared image output");
    const auto matchingRegion = colorAt(matchingOutput, {65, 75});
    require(matchingRegion.blue() > 160 && matchingRegion.red() > 100 && matchingRegion.green() < 100,
            "normal result view hid regions associated with its image output");
    const auto matchingInput = colorAt(matchingOutput, {90, 50});
    require(matchingInput.blue() > 180 && matchingInput.red() < 100 && matchingInput.green() > 60,
            "normal result view hid input geometry declared in matching coordinates");
    const auto unrelatedOnMatching = colorAt(matchingOutput, {35, 75});
    require(unrelatedOnMatching.red() < 10 && unrelatedOnMatching.green() < 10 && unrelatedOnMatching.blue() < 10,
            "normal result view mixed regions from a different image output");
    const auto savedRegions          = output(kRegionsOutput);
    const auto savedUnrelatedRegions = output(kUnrelatedRegionsOutput);
    require(!savedRegions.isEmpty() && savedRegions != "Missing" && !savedUnrelatedRegions.isEmpty()
                && savedUnrelatedRegions != "Missing",
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
    require(unrelatedRegion.blue() > 160 && unrelatedRegion.red() > 100 && unrelatedRegion.green() < 100,
            "switching image outputs did not display their associated regions");
    const auto mismatchingRegion = colorAt(unrelatedOutput, {65, 75});
    const auto mismatchingInput  = colorAt(unrelatedOutput, {90, 50});
    require(mismatchingRegion.red() < 10 && mismatchingRegion.green() < 10 && mismatchingRegion.blue() < 10
                && mismatchingInput.red() < 10 && mismatchingInput.green() < 10 && mismatchingInput.blue() < 10,
            "equal image pixels incorrectly implied matching ROI coordinates");
    require(output(kRegionsOutput) == savedRegions && output(kUnrelatedRegionsOutput) == savedUnrelatedRegions,
            "switching image outputs discarded undisplayed region values");
    std::cout << "[ROI image ownership] Normal output switching respects metadata, not equal pixels; undisplayed "
                 "region values remain available.\n";
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
    const auto cropRegion  = colorAt(croppedView, {30, 20});
    require(cropRegion.blue() > 160 && cropRegion.red() > 100 && cropRegion.green() < 100,
            "Cropped image output did not show regions declared in its own coordinates");
    const auto inputOnCrop             = colorAt(croppedView, {90, 50});
    const auto unprojectedResultOnCrop = colorAt(croppedView, {65, 75});
    const auto guessedProjectionOnCrop = colorAt(croppedView, {80, 45});
    require(inputOnCrop.red() < 10 && inputOnCrop.green() < 10 && inputOnCrop.blue() < 10
                && unprojectedResultOnCrop.red() < 10 && unprojectedResultOnCrop.green() < 10
                && unprojectedResultOnCrop.blue() < 10 && guessedProjectionOnCrop.red() < 10
                && guessedProjectionOnCrop.green() < 10 && guessedProjectionOnCrop.blue() < 10,
            "Crop origins implicitly overlaid or translated regions from the original image coordinates");
    require(output(kOriginsOutput) != "Missing" && !output(kOriginsOutput).isEmpty()
                && output(kRegionsOutput) == savedRegions && output(kUnrelatedRegionsOutput) == savedUnrelatedRegions,
            "Cropped output preview discarded the original region values or origin coordinates");
    std::cout << "[ROI crop coordinates] Cropped outputs display their declared regions without inferring projection "
                 "from origins.\n";
    app.setImageOutput(imageKey(kImagesOutput));
    require(app.imageCount() == 2 && app.imageIndex() == 0,
            "switching back to an image output did not restore its own batch count");
    require(app.editRoi(kRoisInput, false), "Executed ROI could not reopen with its result context");
    require(QTest::qWaitFor([&] { return viewport.editing() && viewport.hasImage(); }, 3000),
            "Executed ROI background was not presented");
    const auto resultLayers = renderedFrame();
    const auto resultPoint  = viewport.mapToScene(viewport.imageToItem({65, 75})) * window.effectiveDevicePixelRatio();
    const auto resultColor  = resultLayers.pixelColor(qFloor(resultPoint.x()), qFloor(resultPoint.y()));
    require(resultColor.blue() > 160 && resultColor.red() > 100 && resultColor.green() < 100,
            "ROI editing hid output regions with declared matching input geometry");
    const auto unrelatedPoint
        = viewport.mapToScene(viewport.imageToItem({35, 75})) * window.effectiveDevicePixelRatio();
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
    require(app.bindInput(app.portContext(kImagesInput), source, kSourceImagesOutput)
                && app.bindInput(app.portContext(kRoisInput), source, kSourceRoisOutput),
            "upstream ROI verification inputs could not bind");
    app.runNode();
    require(QTest::qWaitFor([&] { return !app.busy(); }, 5000), "upstream ROI verification did not finish");
    require(output(kStatusCodePort) == "0" && output(kCountOutput) == "1",
            "editing copied ROIs modified their source cache");
    editor->setVisible(false);
    QQmlComponent panelComponent(&engine);
    panelComponent.setData("import QtQuick\nimport \".\"\nParameterPanel { width: 720; height: 520 }",
                           QUrl::fromLocalFile(QStringLiteral(VISIONRT_SOURCE_DIR "/src/ui/qml/RoiPanelContract.qml")));
    std::unique_ptr<QObject> panelOwner(panelComponent.create());
    if (!panelOwner)
        throw std::runtime_error(panelComponent.errorString().toStdString());
    auto *panel = qobject_cast<QQuickItem *>(panelOwner.get());
    require(panel != nullptr, "ROI parameter panel is unavailable");
    panel->setParentItem(window.contentItem());
    window.resize(1220, 520);
    viewport.setX(740);
    const auto rectLiteral = [&]
    {
        for (const auto &entry : app.parameters())
            if (!entry.toMap().value("init").toBool() && entry.toMap().value("id").toUInt() == kRectInput)
                return entry.toMap().value("literal").toString();
        return QString{};
    };
    const auto rectangle = [&]
    {
        const auto rois = std::get<visionrt::Rectangle>(ValueJson::parse(rectLiteral(), {Type::Rectangle}));
        return rois.get().empty() || rois.get().front().empty() ? EncodedRegion{} : rois.get().front().front();
    };
    const auto settle = [&]
    {
        require(QTest::qWaitFor([&] { return app.workspaceEnabled(); }, 5000), "ROI workspace did not settle");
    };
    require(app.save(project) && !app.dirty(), "Rect baseline could not be saved");
    settle();
    const auto originalRectangle = rectLiteral();
    require(app.editRoi(kRectInput, false), "Rect could not open the image editor");
    require(QTest::qWaitFor([&] { return viewport.editing() && viewport.hasImage(); }, 3000),
            "Rect background was not presented");
    require(viewport.singleRectangle(), "Rect editor did not restrict shape and cardinality");
    QSignalSpy rectangleGestures(&viewport, &ImageViewport::roiCommitted);
    QSignalSpy unchangedEdit(&viewport, &ImageViewport::editingChanged);
    QSignalSpy unchangedOutput(outputs, &QAbstractItemModel::modelReset);
    QTest::qWait(120);
    require(unchangedEdit.isEmpty() && unchangedOutput.isEmpty(), "idle polling reset an active ROI session");
    viewport.setDrawShape(3);
    require(viewport.drawShape() != 3, "Rect editor accepted a circle drawing mode");
    viewport.setDrawShape(2);
    draw(false, {10, 10}, {30, 30}, true);
    require(rectangleGestures.count() == 1 && rectLiteral() != originalRectangle && app.dirty(),
            "Rect drawing did not commit exactly once");
    const auto beforeCanceledRedraw = rectangle();
    viewport.setDrawShape(2);
    draw(true, {40, 40}, {60, 60});
    require(viewport.editing() && viewport.drawShape() == 0 && rectangleGestures.count() == 1
                && rectangle() == beforeCanceledRedraw,
            "canceling a redraw changed the previous rectangle");
    viewport.setDrawShape(2);
    draw(false, {40, 40}, {40, 60});
    require(viewport.drawShape() == 2 && rectangleGestures.count() == 1 && rectangle() == beforeCanceledRedraw,
            "a zero-width redraw replaced the previous rectangle");
    const auto ungrabStart = viewport.mapToScene(viewport.imageToItem({40, 40})).toPoint();
    const auto ungrabEnd = viewport.mapToScene(viewport.imageToItem({60, 60})).toPoint();
    QTest::mousePress(&window, Qt::LeftButton, {}, ungrabStart);
    QTest::mouseMove(&window, ungrabEnd);
    viewport.ungrabMouse();
    QTest::mouseRelease(&window, Qt::LeftButton, {}, ungrabEnd);
    require(viewport.drawShape() == 0 && rectangle() == beforeCanceledRedraw,
            "losing the mouse grab changed the previous rectangle");
    viewport.setDrawShape(2);
    draw(false, {15, 15}, {25, 25});
    require(rectangleGestures.count() == 2 && std::abs(rectangle()[1] - 15) < 1
                && std::abs(rectangle()[3] - 10) < 1,
            "explicit redraw inside a rectangle resized it instead of replacing it");
    viewport.setDrawShape(2);
    draw(false, {60, 60}, {90, 80});
    const auto beforeImageSwitch = rectangle();
    app.setImageIndex(1);
    require(QTest::qWaitFor([&] { return viewport.displayedImage().size() == QSize(32, 24); }, 3000)
                && rectangle() == beforeImageSwitch,
            "switching the batch image scaled or clipped the rectangle");
    app.endRoiPreview();
    require(app.bindInput(app.portContext(kRectInput), source, kSourceRectOutput), "Rect upstream binding failed");
    settle();
    require(app.save(project), "bound Rect baseline could not be saved");
    settle();
    const auto boundRectangle = rectLiteral();
    require(app.editRoi(kRectInput, true), "Rect upstream copy could not begin");
    require(QTest::qWaitFor([&] { return viewport.editing() && viewport.hasImage(); }, 3000),
            "copied Rect background was not presented");
    const auto previousTarget = *viewport.editTarget();
    viewport.setDrawShape(2);
    const auto previousPress = viewport.mapToScene(viewport.imageToItem({15, 15})).toPoint();
    const auto previousRelease = viewport.mapToScene(viewport.imageToItem({25, 25})).toPoint();
    QTest::mousePress(&window, Qt::LeftButton, {}, previousPress);
    QTest::mouseMove(&window, previousRelease);
    app.endRoiPreview();
    require(rectLiteral() == boundRectangle && !app.dirty(), "canceling detached the copied upstream Rect");
    require(app.editRoi(kRectInput, true), "Rect upstream copy could not reopen");
    require(QTest::qWaitFor([&] { return viewport.editTarget() && viewport.hasImage(); }, 3000)
                && viewport.editTarget()->session != previousTarget.session,
            "reopened Rect reused an old session identity");
    const auto beforeLateEvents = rectLiteral();
    QTest::mouseRelease(&window, Qt::LeftButton, {}, previousRelease);
    emit viewport.roiCommitted(previousTarget, makeRectangle(50, 50, 5, 5, 0));
    emit viewport.roiDeleted(previousTarget);
    require(rectLiteral() == beforeLateEvents && !app.dirty(), "stale ROI callbacks changed the reopened input");
    app.endRoiPreview();

    app.addNode("test.roi.editor", 0, 2);
    settle();
    const auto withoutImage = app.selectedNode();
    require(app.setParameter(kRectInput, false, "[[[0,9.25,8,6,30]]]"),
            "image-free rectangle parameter was rejected");
    settle();
    require(rectangle()[2] == 9.25 && app.save(project), "image-free geometry could not be saved");
    settle();
    app.requestOpen(project);
    settle();
    app.selectNode(source);
    app.runNode();
    settle();
    app.selectNode(withoutImage);
    require(rectangle()[2] == 9.25, "image-free geometry did not survive reopening");
    require(app.editRoi(kRectInput, false) && !viewport.hasImage() && app.imageCount() == 0,
            "unbound ROI reused another node's background");
    const auto oldBackgroundTarget = *viewport.editTarget();
    const auto imageFreeValue = rectLiteral();
    const auto beforeImageFreeDraw = rectangleGestures.count();
    viewport.setDrawShape(2);
    QTest::mousePress(&window, Qt::LeftButton, {}, viewport.mapToScene({100, 100}).toPoint());
    QTest::mouseMove(&window, viewport.mapToScene({180, 180}).toPoint());
    QTest::mouseRelease(&window, Qt::LeftButton, {}, viewport.mapToScene({180, 180}).toPoint());
    require(rectLiteral() == imageFreeValue && rectangleGestures.count() == beforeImageFreeDraw,
            "drawing without a background invented geometry");
    require(app.bindInput(app.portContext(kImagesInput), source, kSourceImagesOutput), "background binding failed");
    settle();
    require(!viewport.editing() && app.save(project), "acquiring a background retained the old session");
    settle();
    require(app.editRoi(kRectInput, false), "Rect could not reopen with a real background");
    require(QTest::qWaitFor([&] { return viewport.hasImage() && viewport.editTarget(); }, 3000)
                && viewport.displayedImage().size() == QSize(128, 96), "replacement background was not presented");
    emit viewport.roiCommitted(oldBackgroundTarget, makeRectangle(50, 50, 5, 5, 0));
    require(rectLiteral() == imageFreeValue && !app.dirty(), "old background callback changed the new session");
    viewport.setDrawShape(2);
    const auto executeStart = viewport.mapToScene(viewport.imageToItem({20, 20})).toPoint();
    const auto executeEnd = viewport.mapToScene(viewport.imageToItem({25, 25})).toPoint();
    QTest::mousePress(&window, Qt::LeftButton, {}, executeStart);
    QTest::mouseMove(&window, executeEnd);
    app.runNode();
    require(!viewport.editing(), "execution retained an active ROI gesture");
    settle();
    QTest::mouseRelease(&window, Qt::LeftButton, {}, executeEnd);
    require(rectLiteral() == imageFreeValue && !app.dirty(), "late release after execution changed the ROI");
    require(app.clearRoi(kRectInput), "rectangle clear was rejected");
    settle();
    require(app.editRoi(kRectInput, false), "empty rectangle could not enter drawing mode");
    require(QTest::qWaitFor([&] { return viewport.editing() && viewport.hasImage(); }, 3000)
                && viewport.drawShape() == 2 && viewport.selectedRoi() == -1,
            "empty rectangle did not enter drawing mode");
    draw(true, {10, 10}, {30, 30});
    draw(false, {10, 10}, {10, 30});
    require(rectangle().empty() && viewport.drawShape() == 2, "canceled or invalid drawing created a rectangle");
    draw(false, {10, 10}, {30, 30});
    require(viewport.selectedRoi() == 0 && viewport.drawShape() == 0, "rectangle drawing did not enter selection mode");
    draw(false, {20, 20}, {30, 25});
    const auto moved = rectangle();
    require(std::abs(moved[1] - 20) < 0.5 && std::abs(moved[2] - 15) < 0.5
                && std::abs(moved[3] - 20) < 0.5 && std::abs(moved[4] - 20) < 0.5,
            "dragging a rectangle changed its dimensions");
    QTest::mouseClick(&window, Qt::LeftButton, {}, viewport.mapToScene(viewport.imageToItem({60, 40})).toPoint());
    QTest::keyClick(&window, Qt::Key_Delete);
    require(rectangle() == moved, "Delete without selection removed geometry");
    QTest::mouseClick(&window, Qt::LeftButton, {}, viewport.mapToScene(viewport.imageToItem({30, 25})).toPoint());
    QTest::keyClick(&window, Qt::Key_Delete);
    require(rectangle().empty() && viewport.drawShape() == 2, "deletion retained rectangle geometry or old mode");
    viewport.setDrawShape(2);
    draw(false, {12, 14}, {42, 34});
    const auto windowGeometry = rectangle();
    require(app.save(project), "window baseline could not save");
    settle();
    require(app.editRoi(kRectInput, false), "window ROI session could not begin");
    require(QTest::qWaitFor([&] { return viewport.editTarget() && viewport.hasImage(); }, 3000),
            "window ROI session did not reach the viewport");
    const auto windowSession = viewport.editTarget()->session;
    QQuickWindow otherWindow;
    otherWindow.resize(180, 100);
    otherWindow.show();
    otherWindow.requestActivate();
    require(QTest::qWaitForWindowActive(&otherWindow, 1000)
                && QTest::qWaitFor([&] { return !viewport.editing(); }, 1000)
                && rectangle() == windowGeometry && !app.dirty(), "window deactivation changed the ROI");
    otherWindow.close();
    window.requestActivate();
    require(QTest::qWaitForWindowActive(&window, 1000) && app.editRoi(kRectInput, false), "window ROI could not reopen");
    require(QTest::qWaitFor([&] { return viewport.editTarget() && viewport.editTarget()->session != windowSession
                                                   && viewport.hasImage(); }, 3000),
            "window activation reused the canceled session");
    const auto reopenedSession = viewport.editTarget()->session;
    QQuickWindow transientWindow;
    transientWindow.setTransientParent(&window);
    transientWindow.resize(180, 100);
    transientWindow.show();
    transientWindow.requestActivate();
    require(QTest::qWaitFor([&] { return QGuiApplication::focusWindow() == &transientWindow; }, 1000)
                && viewport.editTarget() && viewport.editTarget()->session == reopenedSession,
            "a transient child canceled the ROI session");
    transientWindow.close();
    window.requestActivate();
    require(QTest::qWaitForWindowActive(&window, 1000), "ROI window did not reactivate");
    app.endRoiPreview();

    app.selectNode(target);
    require(!app.bindInput(app.portContext(kCircleInput), source, kSourceRoisOutput),
            "a circle input accepted an upstream rectangle port");
    app.setParameterMode(kCircleInput, false);
    require(app.setParameter(kCircleInput, false, "[[[30,30,10]]]"), "circle fixture was rejected");
    settle();
    require(app.save(project), "circle baseline could not be saved");
    settle();
    require(app.editRoi(kCircleInput, false), "circle input could not enter editing");
    require(QTest::qWaitFor([&] { return viewport.editTarget() && viewport.hasImage(); }, 3000), "circle frame did not arrive");
    viewport.selectRegion(0);
    QSignalSpy circleCommits(&app, &AppController::projectChanged);
    QSignalSpy circleDrafts(&viewport, &ImageViewport::roiDraftChanged);
    const auto circleStart = viewport.mapToScene(viewport.imageToItem({40, 30})).toPoint();
    const auto circleEnd = viewport.mapToScene(viewport.imageToItem({50, 30})).toPoint();
    QTest::mousePress(&window, Qt::LeftButton, {}, circleStart);
    QTest::mouseMove(&window, circleEnd, 1);
    require(!circleDrafts.isEmpty() && std::abs(circleDrafts.last()[1].value<EncodedRegion>()[3] - 20) < 0.5
                && circleCommits.isEmpty() && !app.dirty(), "circle resize did not preview without committing");
    QTest::mouseRelease(&window, Qt::LeftButton, {}, circleEnd);
    require(circleCommits.size() == 1 && app.dirty(), "one circle gesture must produce one project commit");
    const QPointF wheelPos(viewport.width() * 0.35, viewport.height() * 0.4);
    QWheelEvent   wheel(wheelPos, viewport.mapToGlobal(wheelPos), QPoint(), QPoint(0, 120), Qt::NoButton,
                        Qt::NoModifier, Qt::NoScrollPhase, false);
    QCoreApplication::sendEvent(&viewport, &wheel);
    const auto zoomedState = viewport.viewState();
    require(zoomedState.mode == ImageViewport::Manual, "wheel zoom did not enter manual zoom mode");
    app.endRoiPreview();
    const auto afterEndState = viewport.viewState();
    require(afterEndState.mode == ImageViewport::Manual
                && std::abs(afterEndState.scale - zoomedState.scale) < 1e-6
                && std::abs(afterEndState.imageCenter.x() - zoomedState.imageCenter.x()) < 1e-3
                && std::abs(afterEndState.imageCenter.y() - zoomedState.imageCenter.y()) < 1e-3,
            "ending ROI edit must preserve manual zoom and pan");
    app.setParameterMode(kRectInput, false);
    require(app.setParameter(kRectInput, false, "[[[10.123456789,20.987654321,40,30,0.123456789]]]"),
            "precise rectangle fixture was rejected");
    settle();
    const auto exactRectangleText = rectLiteral();
    QObject *rectangleField = nullptr;
    require(QTest::qWaitFor([&]
    {
        QList<QQuickItem *> pending{panel};
        while (!pending.isEmpty())
        {
            auto *item = pending.takeLast();
            if (item->metaObject()->indexOfProperty("inputItem") >= 0
                && item->property("text").toString() == "[[[10.123457,20.987654,40,30,0.123457]]]")
                rectangleField = item;
            pending.append(item->childItems());
        }
        return rectangleField != nullptr;
    }, 1000), "rectangle field did not display at most six decimals");
    auto *rectangleInput = qobject_cast<QQuickItem *>(rectangleField->property("inputItem").value<QObject *>());
    require(rectangleInput != nullptr, "rectangle text input is missing");
    QSignalSpy roundedRectangleChanges(&app, &AppController::parametersChanged);
    rectangleInput->forceActiveFocus();
    QTest::keyClick(&window, Qt::Key_Return);
    viewport.forceActiveFocus();
    QTest::qWait(20);
    require(rectLiteral() == exactRectangleText && roundedRectangleChanges.isEmpty(),
            "unmodified rectangle input wrote its rounded display value");
}
