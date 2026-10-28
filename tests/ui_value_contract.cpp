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
}

int main(int argc, char **argv)
try
{
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
    write("tools/custom/fixture/source.json", R"({"schema_version":4,"tool_id":"test.ui.source",
        "display_name":"图像源","version":"1","kind":"python_function",
        "entry":{"package":"fixture","file":"tool.py","symbol":"source"},"init_params":[],"inputs":[],"outputs":[
        {"id":"images","name":"图像","type":"Image","display":{"color_order":"BGR"}},
        {"id":"values","name":"列表","type":"Array","dtype":"int64","rank":2},
        {"id":"epoch","name":"轮次","type":"Int"},
        {"id":"regions","name":"区域","type":"ROI","image_port":"images"}]})");
    write("tools/custom/fixture/editor.json", R"({"schema_version":4,"tool_id":"test.ui.editor",
        "display_name":"图像编辑","version":"1","kind":"python_function",
        "entry":{"package":"fixture","file":"tool.py","symbol":"edit"},"init_params":[],"inputs":[
        {"id":"images","name":"图像","type":"Image"},
        {"id":"rect","name":"裁剪区域","type":"ROI","editor":"rect","image_input":"images","default":[]},
        {"id":"regions","name":"区域","type":"ROI","editor":"roi","image_input":"images","default":[]}],
        "outputs":[{"id":"images","name":"图像","type":"Image","display":{"same_geometry_as_input":"images"}}]})");
    write("python/custom/fixture/tool.py",
          "import numpy as np, time\nepoch = 0\npixels = np.full((80, 120, 3), 29, dtype=np.uint8)\n"
          "def source(inputs):\n"
          " global epoch\n epoch += 1\n pixels[:40] = epoch\n time.sleep(0.12)\n pixels[40:] = epoch\n"
          " return {'status':{'code':0,'message':'','detail':''}, 'outputs':{'images':[pixels, pixels[:40,:60]], "
          "'epoch':epoch, 'values':[[9007199254740993],[],[4,5]], 'regions':[[[3.,20.,20.,5.]],[]]}}\n"
          "def edit(inputs):\n"
          " return {'status':{'code':0,'message':'','detail':''}, 'outputs':{'images':inputs['images']}}\n");
    AppController app(2, directory.path());
    const auto    idle = [&]
    {
        require(QTest::qWaitFor([&] { return !app.busy(); }, 5000), "UI operation did not finish");
    };
    idle();
    const auto metadata = app.toolMetadata("test.ui.source");
    require(metadata["entry"].toMap()["package"] == "fixture" && !metadata.contains("script"),
            "Python metadata still exposes the JSON-relative script path");
    require(!metadata["outputs"].toList()[1].toMap().contains("layout"), "UI metadata still exposes Array layout");
    app.addNode("test.ui.source", 0, 0);
    idle();
    const auto source = app.selectedNode();
    require(!source.isEmpty(), "Python source unavailable through scanned JSON");
    const auto stableNodes = [&]
    {
        const auto rows = app.nodes();
        for (int iteration = 0; iteration < 100; ++iteration)
            require(rows.isSharedWith(app.nodes()), "unchanged node readback rebuilt metadata storage");
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
    require(renamedNodes.front().toMap()["alias"] == "缓存节点"
                && originalNodes.front().toMap()["alias"].toString().isEmpty()
                && !renamedNodes.isSharedWith(originalNodes),
            "node rename did not publish independent updated metadata");
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
    require(OutputModel::fullText(*outputs->capture("__status_code")) == "0", "Python image source failed");
    const auto values = QJsonDocument::fromJson(OutputModel::fullText(*outputs->capture("values")).toUtf8()).array();
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
    app.requestOutputText(app.portContext("epoch", true), false, false);
    QTest::qWait(30);
    require(textReady.isEmpty(), "full text was read from a mutable publication during execution");
    idle();
    require(QTest::qWaitFor([&] { return textReady.size() == 1; }, 3000), "deferred full text was never delivered");
    require(textReady[0][0].toString() == "2", "deferred text did not capture the completed round");
    {
        QFile file(projectPath);
        require(file.open(QIODevice::ReadOnly), "deferred project file unavailable");
        const auto saved = QJsonDocument::fromJson(file.readAll());
        require(saved.isObject() && saved.object()["format_version"].toInt() == 5,
                "queued save was not committed after the run finished");
    }
    app.addNode("test.ui.editor", 100, 100);
    idle();
    ImageViewport viewport;
    viewport.setSize({480, 320});
    app.attachViewport(&viewport);
    require(app.editRoi("rect", false), "rectangle metadata did not select the existing editor");
    const auto session = app.roiEditor()["id"].toString();
    require(!session.isEmpty() && viewport.singleRectangle(), "ROI editor is not a single rectangle");
    require(app.setRoiRectangle(session, {100000000.125, 2.5, 20.5, 10.75, 17.5}), "native double rectangle rejected");
    require(app.roiEditor()["rectangle"].toList()[0].toDouble() == 100000000.125, "rectangle editor narrowed doubles");
    require(app.applyRoi(), "rectangle editor failed to apply");
    const auto parameter = [&](const QString &id)
    {
        for (const auto &row : app.parameters())
            if (row.toMap()["id"] == id)
                return row.toMap();
        return QVariantMap{};
    };
    const auto rectangle = parameter("rect");
    require(rectangle["type"] == "ROI" && rectangle["editor"] == "rect" && rectangle["hasRect"].toBool(),
            "rectangle editor reintroduced a separate value type");
    const auto encoded = QJsonDocument::fromJson(rectangle["literal"].toString().toUtf8()).array();
    require(encoded.size() == 1 && encoded[0].toArray().size() == 1
                && encoded[0].toArray()[0].toArray()[1].toDouble() == 100000000.125,
            "rectangle is not one group containing one native ROI record");
    require(app.clearRoi("rect"), "rectangle clearing failed");
    const auto cleared = QJsonDocument::fromJson(parameter("rect")["literal"].toString().toUtf8());
    require(cleared.isArray() && cleared.array().isEmpty(), "rectangle clearing did not produce empty ROI");
    require(app.bindInput(app.portContext("images"), source, "images"), "cannot bind writable source images");
    idle();
    app.runNode();
    idle();
    require(QTest::qWaitFor([&] { return viewport.hasImage(); }, 5000), "workbench did not convert cached image");
    require(app.imageCount() == 2, "native image list lost its batch size");
    require(app.editRoi("regions", false), "generic ROI editor failed to open");
    require(QTest::qWaitFor([&] { return viewport.editing() && !viewport.singleRectangle(); }, 3000),
            "generic ROI editor became rectangle-only");
    app.endRoiPreview();
    app.selectNode(source);
    textReady.clear();
    QString lastPublication;
    const auto frameText = QObject::connect(&viewport, &ImageViewport::imagePresented, &app,
                                            [&](quint64)
                                            {
                                                if (app.busy())
                                                {
                                                    const auto context = app.portContext("epoch", true);
                                                    const auto pub     = context.value("publication").toString();
                                                    if (!pub.isEmpty() && pub != lastPublication)
                                                    {
                                                        lastPublication = pub;
                                                        app.requestOutputText(context, false, false);
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
    app.requestOutputText(app.portContext("epoch", true), false, false);
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
