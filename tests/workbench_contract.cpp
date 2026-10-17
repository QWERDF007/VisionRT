#include "ui/ToolTreeModel.h"
#include "ui/OutputModel.h"
#include "ui/AppController.h"
#include <QGuiApplication>
#include <QTemporaryDir>
#include <iostream>
#include <stdexcept>

using namespace visionrt;
namespace {
void require(bool value, const char* message) { if (!value) throw std::runtime_error(message); }
void treeContract() {
    ToolRegistry registry;
    ToolDefinition tool;
    tool.id = "custom.precise";
    tool.displayName = "精确整数";
    tool.version = "1.0";
    tool.kind = ToolKind::PythonFunction;
    tool.category = "数据";
    tool.subcategory = "整数";
    registry.addDefinition(tool);
    ToolTreeModel model;
    model.setRegistry(registry);
    auto category = model.index(0, 0);
    auto group = model.index(0, 0, category);
    auto leaf = model.index(0, 0, group);
    require(model.parent(leaf) == group && model.parent(group) == category, "tree loses hierarchy");
    require(model.data(leaf, ToolTreeModel::ToolIdRole).toString() == "custom.precise", "leaf loses stable id");
    model.setFilter("不存在");
    require(model.rowCount() == 0, "filter retains unrelated categories");
    model.setFilter("精确");
    require(model.rowCount() == 1, "filter loses matching descendant");
}
void outputContract() {
    ToolDefinition tool;
    PortDefinition port;
    port.id = "value"; port.name = "值"; port.type = {Type::String};
    tool.outputs.push_back(port);
    auto snapshot = std::make_shared<NodeSnapshot>();
    snapshot->status = {-2, "conversion failed"};
    snapshot->outputs.emplace("value", Utf8(std::string(10000, 'x')));
    OutputModel model;
    model.setResult(&tool, snapshot, {{"value", 2}});
    require(model.get(0).value("portId").toString() == "__status_code", "status code is not first");
    require(model.get(1).value("portId").toString() == "__status_message", "status message is not second");
    require(model.get(0).value("preview").toString() == "-2", "status not taken from completed snapshot");
    require(model.get(2).value("preview").toString().size() <= 256, "preview is unbounded");
    require(model.get(2).value("tooltip").toString().size() <= 4096, "tooltip is unbounded");
    require(model.get(2).value("count").toInt() == 2, "relationship count is not input binding count");
    const auto frozen = model.capture(2);
    model.clear();
    require(frozen && OutputModel::fullText(*frozen).size() >= 10000, "captured output follows current selection or truncates");
}
void pendingContract() {
    QTemporaryDir directory;
    AppController app(1, directory.path());
    int confirmations = 0, closes = 0;
    QObject::connect(&app, &AppController::unsavedDecisionRequired, [&] { ++confirmations; });
    QObject::connect(&app, &AppController::closeReady, [&] { ++closes; });
    app.requestClose();
    require(confirmations == 1 && closes == 0, "dirty close bypasses decision");
    app.resolveUnsaved("cancel");
    require(closes == 0 && app.dirty(), "cancel changes project or closes");
    app.requestClose();
    app.resolveUnsaved("save", directory.path() + "/absent/project.vrt");
    require(closes == 0 && app.dirty(), "failed save permits close");
    app.resolveUnsaved("discard");
    require(closes == 1, "discard does not finish pending close exactly once");
}
void nodeDiagnosticsContract() {
    QTemporaryDir directory;
    QDir(directory.path()).mkpath("tools/builtin/read/images");
    QFile yamlFile(directory.path() + "/tools/builtin/read/images/read.yaml");
    if (yamlFile.open(QIODevice::WriteOnly)) {
        yamlFile.write(
            "schema_version: 1\n"
            "tool_id: visionrt.image.read\n"
            "display_name: 图像读取\n"
            "version: 1.0.0\n"
            "kind: cpp_function\n"
            "entry:\n"
            "  registry_name: visionrt.image.read\n"
            "init_params: []\n"
            "inputs:\n"
            "- id: mode\n"
            "  name: 读取模式\n"
            "  type: String\n"
            "  default: file\n"
            "- id: path\n"
            "  name: 文件或目录路径\n"
            "  type: String\n"
            "  default: ''\n"
            "- id: color\n"
            "  name: 色彩模式\n"
            "  type: String\n"
            "  default: bgr\n"
            "- id: limit\n"
            "  name: 最大数量\n"
            "  type: Int\n"
            "  default: 0\n"
            "outputs:\n"
            "- id: images\n"
            "  name: 图像\n"
            "  type: ImageBatch\n"
        );
        yamlFile.close();
    }
    AppController app(1, directory.path());
    app.addNode("visionrt.image.read", 100, 100);
    const auto nodes = app.nodes();
    if (nodes.isEmpty()) {
        std::cerr << "Diagnostic: " << app.diagnostic().toStdString() << "\n";
    }
    require(!nodes.isEmpty(), "added node not found in nodes()");
    const auto map = nodes.first().toMap();
    require(map.contains("state"), "node missing state");
    require(map.value("state").toString() == "gray", "initial node state is not gray");
    require(map.contains("defMissing"), "node missing defMissing");
    require(map.value("defMissing").toBool() == false, "registered node marked defMissing");
    require(map.contains("implUnavailable"), "node missing implUnavailable");
    require(map.value("implUnavailable").toBool() == false, "CPU node marked implUnavailable");
    require(map.contains("timingDetail"), "node missing timingDetail");
}
}
int main(int argc, char** argv) {
    QGuiApplication qt(argc, argv);
    try {
        treeContract(); outputContract(); pendingContract(); nodeDiagnosticsContract();
        std::cout << "workbench contract passed\n";
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n'; return 1;
    }
}
