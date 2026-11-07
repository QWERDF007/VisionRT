#include "TestPythonConfiguration.h"
#include "ui/AppController.h"
#include "ui/ImageViewport.h"
#include "ui/OutputModel.h"

#include <QDir>
#include <QFile>
#include <QGuiApplication>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QTemporaryDir>
#include <QSignalSpy>
#include <QTest>
#include <iostream>

using namespace visionrt;

namespace {
void require(bool condition, const char *message)
{
    if (!condition)
        throw std::runtime_error(message);
}

// 夹具声明的端口编号：身份是数字，调用点直接用编号而不是中文名。
constexpr PortId kImagesOutput = 2;  // test.ui.source 输出
constexpr PortId kValuesOutput = 3;
constexpr PortId kEpochOutput  = 4;
constexpr PortId kImagesInput  = 1;  // test.ui.editor 输入
constexpr PortId kRectInput    = 2;
constexpr PortId kRegionsInput = 3;
}

int main(int argc, char **argv)
try
{
    // R50: the host runs Python only on an interpreter someone selected.
    testsupport::useBuildInterpreter();
    QGuiApplication application(argc, argv);
    QTemporaryDir   directory;
    require(directory.isValid(), "UI fixture directory unavailable");
    const auto root  = directory.path() + "/tools-package/";
    const auto write = [&](const QString &path, const QByteArray &content)
    {
        require(QDir().mkpath(QFileInfo(root + path).absolutePath()), "UI fixture parent unavailable");
        QFile file(root + path);
        require(file.open(QIODevice::WriteOnly) && file.write(content) == content.size(), "UI fixture write failed");
    };
    write("module_index.json", R"({"schema_version":1,"variant":"test","tools":[
        {"tool_id":"test.ui.source","definition":"tools/custom/fixture/source.json"},
        {"tool_id":"test.ui.editor","definition":"tools/custom/fixture/editor.json"}],"modules":[],"runtime_files":[]})");
    write("tools/custom/fixture/source.json", R"({"schema_version":6,"name_en":"test.ui.source","name_zh":"图像源","version":"1","implementation":{"kind":"python_function","package":"fixture","file":"tool.py","symbol":"source"},"next_port_id":{"init":0,"inputs":1,"outputs":6},"ports":{"outputs":[{"id":2,"name_zh":"图像","type":"image","display":{"color_order":"BGR","visible":true}},{"id":3,"name_zh":"列表","type":"vec_vec_int"},{"id":4,"name_zh":"轮次","type":"int"},{"id":5,"name_zh":"区域","type":"roi","relations":{"image":{"stage":"outputs","id":2}}}]}})");
    write("tools/custom/fixture/editor.json", R"({"schema_version":6,"name_en":"test.ui.editor","name_zh":"图像编辑","version":"1","implementation":{"kind":"python_function","package":"fixture","file":"tool.py","symbol":"edit"},"next_port_id":{"init":0,"inputs":4,"outputs":3},"ports":{"inputs":[{"id":1,"name_zh":"图像","type":"image","display":{"visible":true}},{"id":2,"name_zh":"裁剪区域","type":"roi","default":[],"ui":{"editor":"rect"},"relations":{"image":{"stage":"inputs","id":1}}},{"id":3,"name_zh":"区域","type":"roi","default":[],"ui":{"editor":"roi"},"relations":{"image":{"stage":"inputs","id":1}}}],"outputs":[{"id":2,"name_zh":"图像","type":"image","display":{"visible":true},"relations":{"same_geometry_as":{"stage":"inputs","id":1}}}]}})");
    write("python/custom/fixture/tool.py",
          "import numpy as np, time\nepoch = 0\npixels = np.full((80, 120, 3), 29, dtype=np.uint8)\n"
          "def source(inputs):\n"
          " global epoch\n epoch += 1\n pixels[:40] = epoch\n time.sleep(0.12)\n pixels[40:] = epoch\n"
          " return {'outputs':{2:[pixels, pixels[:40,:60]], "
          "4:epoch, 3:[[9007199254740993],[],[4,5]], 5:[[[3.,20.,20.,5.]],[]], "
          "0:0, 1:''}, 'detail':''}\n"
          "def edit(inputs):\n"
          " return {'outputs':{2:inputs[1], 0:0, 1:''}, 'detail':''}\n");
    AppController app(2, directory.path());
    const auto    idle = [&]
    {
        require(QTest::qWaitFor([&] { return !app.busy(); }, 5000), "UI operation did not finish");
    };
    idle();
    const auto metadata = app.toolMetadata("test.ui.source");
    require(metadata["entry"].toMap()["package"] == "fixture" && !metadata.contains("script"),
            "Python metadata still exposes the JSON-relative script path");
    const auto metadataOutput = [&](int port)
    {
        for (const auto &row : metadata["outputs"].toList())
            if (row.toMap()["id"].toInt() == port)
                return row.toMap();
        return QVariantMap{};
    };
    require(!metadataOutput(3).contains("layout"), "UI metadata still exposes Array layout");
    app.addNode("test.ui.source", 0, 0);
    idle();
    const auto source = app.selectedNode();
    require(!source.isEmpty(), "Python source unavailable through scanned JSON");
    const auto stableNodes = [&]
    {
        const auto rows = app.nodes();
        for (int iteration = 0; iteration < 100; ++iteration)
            require(app.nodes() == rows, "unchanged node readback changed its content");
        return rows;
    };
    const auto originalNodes = stableNodes();
    require(originalNodes.size() == 1 && originalNodes.front().toMap()["state"] == "idle",
            "initial node metadata is incorrect");
    QVariantList observedProjectNodes;
    const auto   projectObservation
        = QObject::connect(&app, &AppController::projectChanged, &app, [&] { observedProjectNodes = app.nodes(); });
    app.renameNode(source, "缓存节点");
    QObject::disconnect(projectObservation);
    require(observedProjectNodes.size() == 1 && observedProjectNodes.front().toMap()["alias"] == "缓存节点",
            "project observers received stale node metadata before nodesChanged");
    const auto renamedNodes = stableNodes();
    require(renamedNodes.front().toMap()["alias"] == "缓存节点" && originalNodes.front().toMap()["alias"].toString().isEmpty(),
            "node rename did not publish updated metadata without touching an earlier readback");
    require(app.moveNodes({source}, 1, 0), "cached node movement failed");
    const auto movedNodes = stableNodes();
    require(movedNodes.front().toMap()["x"] == 1 && renamedNodes.front().toMap()["x"] == 0,
            "node movement retained stale metadata or mutated an earlier readback");
    idle();
    require(app.moveNodes({source}, -1, 0), "cached node movement rollback failed");
    idle();
    const auto firstFlow = app.currentFlow();
    app.addFlow("缓存空流程");
    idle();
    require(app.currentFlow() != firstFlow && stableNodes().isEmpty(), "empty flow reused previous node metadata");
    QVariantList observedFlowNodes;
    const auto   flowObservation
        = QObject::connect(&app, &AppController::selectionChanged, &app, [&] { observedFlowNodes = app.nodes(); });
    app.selectFlow(firstFlow);
    QObject::disconnect(flowObservation);
    require(observedFlowNodes.size() == 1 && observedFlowNodes.front().toMap()["id"] == source,
            "flow observers received stale node metadata before nodesChanged");
    app.selectNode(source);
    app.runNode();
    idle();
    const auto executedNodes = stableNodes();
    require(executedNodes.front().toMap()["state"] == "succeeded" && originalNodes.front().toMap()["state"] == "idle",
            "node execution did not invalidate the metadata readback");
    auto *outputs = qobject_cast<OutputModel *>(app.outputModel());
    require(outputs != nullptr, "output model unavailable");
    require(OutputModel::fullText(*outputs->capture(kStatusCodePort)) == "0", "Python image source failed");
    const auto values = QJsonDocument::fromJson(OutputModel::fullText(*outputs->capture(3)).toUtf8()).array();
    require(values[0].toArray()[0].toInteger() == 9007199254740993LL && values[1].toArray().isEmpty(),
            "workbench lost nested native output values");
    const auto projectPath = directory.path() + "/project.vrt.json";
    require(app.save(projectPath), "initial project save failed");
    idle();
    {
        QFile file(projectPath);
        require(file.open(QIODevice::WriteOnly) && file.write("sentinel") == 8, "cannot seed deferred save witness");
    }
    QSignalSpy textReady(&app, &AppController::outputTextReady);
    app.runNode();
    require(app.busy(), "second execution was not submitted");
    require(!app.save(), "busy project serialization was performed before the run finished");
    {
        QFile file(projectPath);
        require(file.open(QIODevice::ReadOnly) && file.readAll() == "sentinel", "deferred save wrote during execution");
    }
    app.requestOutputText(app.portContext(kEpochOutput, true));
    QTest::qWait(30);
    require(textReady.isEmpty(), "full text was read from a mutable publication during execution");
    idle();
    require(QTest::qWaitFor([&] { return textReady.size() == 1; }, 3000), "deferred full text was never delivered");
    require(textReady[0][0].toString() == "2", "deferred text did not capture the completed round");
    {
        // A save requested during a run is refused, not queued: the witness file keeps its content
        // and nothing is written once the run finishes.
        QFile file(projectPath);
        require(file.open(QIODevice::ReadOnly), "project file unavailable");
        require(file.readAll() == "sentinel", "a refused save was written after the run finished");
    }
    require(app.save(), "idle save after the run failed");
    require(QTest::qWaitFor([&] { return !app.busy(); }, 5000), "saved project did not settle");
    {
        QFile file(projectPath);
        require(file.open(QIODevice::ReadOnly), "saved project file unavailable");
        const auto saved = QJsonDocument::fromJson(file.readAll());
        require(saved.isObject() && saved.object()["format_version"].toInt() == 7,
                "idle save after the run produced an incomplete project");
    }
    app.addNode("test.ui.editor", 100, 100);
    idle();
    ImageViewport viewport;
    viewport.setSize({480, 320});
    app.attachViewport(&viewport);
    require(app.editRoi(kRectInput, false), "rectangle metadata did not select the existing editor");
    const auto session = app.roiEditor()["id"].toString();
    require(!session.isEmpty() && viewport.singleRectangle(), "ROI editor is not a single rectangle");
    require(app.setRoiRectangle(session, {100000000.125, 2.5, 20.5, 10.75, 17.5}), "native double rectangle rejected");
    require(app.roiEditor()["rectangle"].toList()[0].toDouble() == 100000000.125, "rectangle editor narrowed doubles");
    require(app.applyRoi(), "rectangle editor failed to apply");
    const auto parameter = [&](PortId id)
    {
        for (const auto &row : app.parameters())
            if (row.toMap()["id"].toUInt() == id)
                return row.toMap();
        return QVariantMap{};
    };
    const auto rectangle = parameter(kRectInput);
    require(rectangle["type"] == "ROI" && rectangle["editor"] == "rect" && rectangle["hasRect"].toBool(),
            "rectangle editor reintroduced a separate value type");
    const auto encoded = QJsonDocument::fromJson(rectangle["literal"].toString().toUtf8()).array();
    require(encoded.size() == 1 && encoded[0].toArray().size() == 1
                && encoded[0].toArray()[0].toArray()[1].toDouble() == 100000000.125,
            "rectangle is not one group containing one native ROI record");
    require(app.clearRoi(kRectInput), "rectangle clearing failed");
    const auto cleared = QJsonDocument::fromJson(parameter(kRectInput)["literal"].toString().toUtf8());
    require(cleared.isArray() && cleared.array().isEmpty(), "rectangle clearing did not produce empty ROI");
    require(app.bindInput(app.portContext(kImagesInput), source, kImagesOutput), "cannot bind writable source images");
    idle();
    // The editor shows the image the user opened; the test opens that port explicitly.
    app.setImageOutput(QStringLiteral("inputs:%1").arg(kImagesInput));
    require(!app.selectedImageOutput().isEmpty(), "the editor image could not be selected");
    app.runNode();
    idle();
    require(QTest::qWaitFor([&] { return viewport.hasImage(); }, 5000), "workbench did not convert cached image");
    require(app.imageCount() == 2, "native image list lost its batch size");
    require(app.editRoi(kRegionsInput, false), "generic ROI editor failed to open");
    require(QTest::qWaitFor([&] { return viewport.editing() && !viewport.singleRectangle(); }, 3000),
            "generic ROI editor became rectangle-only");
    app.endRoiPreview();
    app.selectNode(source);
    // Returning to a node does not reopen a port for the user; the test opens the source image.
    app.setImageOutput(QStringLiteral("outputs:%1").arg(kImagesOutput));
    require(!app.selectedImageOutput().isEmpty(), "the source image could not be reopened");
    require(QTest::qWaitFor([&] { return viewport.hasImage(); }, 5000),
            "the reopened source image was not presented");
    textReady.clear();
    QString lastPublication;
    const auto frameText = QObject::connect(&viewport, &ImageViewport::imagePresented, &app,
                                            [&](quint64)
                                            {
                                                if (app.busy())
                                                {
                                                    const auto context = app.portContext(kEpochOutput, true);
                                                    const auto pub     = context.value("publication").toString();
                                                    if (!pub.isEmpty() && pub != lastPublication)
                                                    {
                                                        lastPublication = pub;
                                                        app.requestOutputText(context);
                                                    }
                                                }
                                            });
    app.toggleLoop();
    require(app.busy() && viewport.hasImage(), "loop discarded its previous rendered frame");
    QTest::qWait(300);
    require(textReady.isEmpty(), "loop exported text during frozen execution");
    app.toggleLoop();
    idle();
    QObject::disconnect(frameText);
    app.requestOutputText(app.portContext(kEpochOutput, true));
    require(QTest::qWaitFor([&] { return textReady.size() == 1; }, 3000),
            "stopped loop did not deliver final text");
    bool       valid   = false;
    const auto current = textReady[0][0].toString().toLongLong(&valid);
    require(valid && current > 2, "loop exported stale or repeated publication text");
    std::cout << "Workbench native lists, deferred/loop readback and Python image release passed\n";
}
catch (const std::exception &error)
{
    std::cerr << error.what() << '\n';
    return 1;
}
