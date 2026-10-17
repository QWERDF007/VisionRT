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
#include <chrono>
#include <thread>

void nodeDiagnosticsContract() {
    QTemporaryDir directory;
    QDir(directory.path()).mkpath("tools/custom/test_category/test_diag");
    QFile pyFile(directory.path() + "/tools/custom/test_category/test_diag/test_diag.py");
    if (pyFile.open(QIODevice::WriteOnly)) {
        pyFile.write("def run(inputs):\n    return {'__status_code': 0, '__status_message': '', 'value': 42}\n");
        pyFile.close();
    }
    QFile yamlFile(directory.path() + "/tools/custom/test_category/test_diag/test_diag.yaml");
    if (yamlFile.open(QIODevice::WriteOnly)) {
        yamlFile.write(
            "schema_version: 1\n"
            "tool_id: test.diag.python\n"
            "display_name: 诊断测试\n"
            "version: 1.0.0\n"
            "kind: python_function\n"
            "entry:\n"
            "  file: test_diag.py\n"
            "  symbol: run\n"
            "init_params: []\n"
            "inputs: []\n"
            "outputs:\n"
            "- id: value\n"
            "  name: 输出\n"
            "  type: Int\n"
        );
        yamlFile.close();
    }
    QFile unfulfilledYaml(directory.path() + "/tools/custom/test_category/test_diag/unfulfilled.yaml");
    if (unfulfilledYaml.open(QIODevice::WriteOnly)) {
        unfulfilledYaml.write(
            "schema_version: 1\n"
            "tool_id: test.diag.unfulfilled\n"
            "display_name: 未提供实现测试\n"
            "version: 1.0.0\n"
            "kind: cpp_function\n"
            "entry:\n"
            "  registry_name: test.diag.missing_symbol\n"
            "init_params: []\n"
            "inputs: []\n"
            "outputs: []\n"
        );
        unfulfilledYaml.close();
    }

    AppController app(1, directory.path());
    for (int i = 0; i < 50 && app.busy(); ++i) {
        QCoreApplication::processEvents();
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
    app.addNode("test.diag.python", 100, 100);
    app.addNode("test.diag.unfulfilled", 200, 200);

    const auto nodes = app.nodes();
    require(nodes.size() == 2, "expected 2 nodes in nodes()");

    const auto map0 = nodes.at(0).toMap();
    require(map0.contains("state"), "node missing state");
    require(map0.value("state").toString() == "gray", "initial node state is not gray");
    require(map0.contains("defMissing"), "node missing defMissing");
    require(map0.value("defMissing").toBool() == false, "registered node marked defMissing");
    require(map0.contains("implUnavailable"), "node missing implUnavailable");
    require(map0.value("implUnavailable").toBool() == false, "available node marked implUnavailable");
    require(map0.contains("timingDetail"), "node missing timingDetail");

    const auto map1 = nodes.at(1).toMap();
    require(map1.value("defMissing").toBool() == false, "unfulfilled node marked defMissing");
    require(map1.value("implUnavailable").toBool() == true, "unfulfilled node not marked implUnavailable");

    app.runOnce();
    for (int i = 0; i < 50; ++i) {
        QCoreApplication::processEvents();
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
    const auto executedNodes = app.nodes();
    require(!executedNodes.isEmpty(), "executed node empty");
    const auto executedMap = executedNodes.first().toMap();
    const auto detail = executedMap.value("timingDetail").toString();
    require(detail.contains(QStringLiteral("工具调用(含桥接):")), "timingDetail missing bridge disclaimer");
    require(!detail.contains(QStringLiteral("纯算法")), "timingDetail falsely claims pure algorithm");
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
