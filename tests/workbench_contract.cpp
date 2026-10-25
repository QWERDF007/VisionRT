#include "ui/AppController.h"
#include "ui/DurationText.h"
#include "ui/ImageViewport.h"
#include "ui/OutputModel.h"
#include "ui/Theme.h"
#include "ui/ToolTreeModel.h"
#include "runtime/Snapshot.h"
#include "core/ValueJson.h"

#include <QFile>
#include <QFileInfo>
#include <QDir>
#include <QGuiApplication>
#include <QImage>
#include <QJsonDocument>
#include <QJsonObject>
#include <QProcess>
#include <QQmlApplicationEngine>
#include <QQmlContext>
#include <QQuickItem>
#include <QQuickStyle>
#include <QQuickWindow>
#include <QTemporaryDir>
#include <QTest>
#include <QUrl>
#include <cmath>
#include <filesystem>
#include <iostream>
#include <limits>
#include <stdexcept>
#include <utility>

using namespace visionrt;

void parameterEditorsContract();
void roiParameterContract();
void pathEditorsContract();
void flowCanvasContract();
void startupProjectContract();
void cancellationWindowContract();
void popupSessionsContract();
void outputCellReuseContract();
void hoverCardContract(visionrt::AppController &app, QQuickWindow *window, double scale);

namespace {
void require(bool value, const char *message)
{
    if (!value)
        throw std::runtime_error(message);
}

void require(bool value, const std::string &message)
{
    if (!value)
        throw std::runtime_error(message);
}

void treeContract()
{
    ToolRegistry   registry;
    ToolDefinition tool;
    tool.id          = "custom.precise";
    tool.displayName = "精确整数";
    tool.version     = "1.0";
    tool.kind        = ToolKind::PythonFunction;
    tool.entry       = PythonEntry{"fixture", "precise.py", "execute"};
    tool.category    = "数据";
    tool.subcategory = "整数";
    registry.addDefinition(tool);
    ToolTreeModel model;
    model.setRegistry(registry);
    auto category = model.index(0, 0);
    auto group    = model.index(0, 0, category);
    auto leaf     = model.index(0, 0, group);
    require(model.parent(leaf) == group && model.parent(group) == category, "tree loses hierarchy");
    require(model.data(leaf, ToolTreeModel::ToolIdRole).toString() == "custom.precise", "leaf loses stable id");
    model.setFilter("不存在");
    require(model.rowCount() == 0, "filter retains unrelated categories");
    model.setFilter("精确");
    require(model.rowCount() == 1, "filter loses matching descendant");
}

void outputContract()
{
    require(OutputModel::fullText({Missing{}, {Type::Int}}) == "null", "Missing must copy as JSON null");
    const auto quoted = OutputModel::fullText({std::string("中文 \"引号\"\n路径"), {Type::String}});
    require(std::get<std::string>(ValueJson::parse(quoted, {Type::String})) == "中文 \"引号\"\n路径",
            "output text must escape strings as JSON");
    require(OutputModel::fullText({std::numeric_limits<double>::infinity(), {Type::Float}}).contains("non_finite"),
            "nonfinite output must have an explicit JSON description");
    require(typeText({Type::ROI}) == QStringLiteral("ROI"), "scalar rectangle type must be displayed safely");
    const auto                  rectangle = DataRef<ROI>(ROI{{{2, 1.25, -2.5, 3.75, 4.5, 17.5}}});
    const OutputModel::Captured rectangleCapture{rectangle, {Type::ROI}};
    require(OutputModel::fullText(rectangleCapture).contains("1.25"),
            "scalar rectangle output must retain floating geometry in full text");
    ToolDefinition tool;
    PortDefinition port;
    port.id   = "value";
    port.name = "值";
    port.type = {Type::String};
    tool.outputs.push_back(port);
    auto snapshot    = std::make_shared<NodeSnapshot>();
    snapshot->node   = 18446744073709551614ULL;
    snapshot->toolId = "test.output";
    snapshot->phase  = NodePhase::Convert;
    snapshot->status = {-2, "输出合同错误", "third-party \"detail\"\n完整详情"};
    snapshot->outputs.emplace("value", std::string(std::string(10000, 'x')));
    OutputModel model;
    model.setResult(&tool, snapshot,
                    {
                        {"value", 2}
    });
    require(model.get(0).value("portId").toString() == "__status_code", "status code is not first");
    require(model.get(1).value("portId").toString() == "__status_message", "status message is not second");
    require(model.get(0).value("preview").toString() == "-2", "status not taken from completed snapshot");
    require(model.get(1).value("preview").toString() == "输出合同错误", "status preview must contain only summary");
    require(model.get(1).value("tooltip").toString() == "输出合同错误", "status tooltip must contain only raw message");
    const auto diagnostic = QJsonDocument::fromJson(OutputModel::diagnosticText(*model.capture(1)).toUtf8()).object();
    require(diagnostic.value("node").toString() == "18446744073709551614", "diagnostic JSON loses node precision");
    require(diagnostic.value("tool_id").toString() == "test.output" && diagnostic.value("phase") == "convert",
            "diagnostic JSON loses typed context");
    require(diagnostic.value("message") == "输出合同错误"
                && diagnostic.value("detail") == "third-party \"detail\"\n完整详情",
            "diagnostic JSON loses raw summary/detail");
    require(model.get(2).value("preview").toString().size() <= 256, "preview is unbounded");
    require(model.get(2).value("tooltip").toString().size() <= 4096, "tooltip is unbounded");
    require(model.get(2).value("count").toInt() == 2, "relationship count is not input binding count");
    const auto frozen = model.capture(2);
    model.clear();
    require(frozen && OutputModel::fullText(*frozen).size() >= 10000,
            "captured output follows current selection or truncates");
    tool.outputs.front().typeFrom = PortTypeReference{"flow", "node", "port"};
    model.setResult(&tool, {}, {});
    require(model.get(2).value("type").toString() == QStringLiteral("未绑定"), "unbound output pretends to have a concrete type");
    require(!model.capture(2), "unbound output can be captured as a typed value");
    const TypeSpec resolved{Type::Array, DType::Float32, 2};
    const TypeMap outputs{{"value", resolved}};
    model.setResult(&tool, {}, {}, &outputs);
    require(model.get(2).value("type").toString() == typeText(resolved), "resolved dtype/rank/layout not displayed");
    const auto lifecycle = std::make_shared<const NodeDiagnostic>(NodeDiagnostic{
        1,
        "test.output",
        NodePhase::Release,
        {-4, "释放失败", "uninitialized release detail"}
    });
    model.setResult(&tool, {}, {}, &outputs, lifecycle);
    require(isMissing(model.capture(0)->value), "release failure must not create an execution status");
    const auto releaseJson = QJsonDocument::fromJson(OutputModel::diagnosticText(*model.capture(1)).toUtf8()).object();
    require(releaseJson.value("phase") == "release" && releaseJson.value("detail") == "uninitialized release detail",
            "release diagnostics without execution must remain independently exportable");

    ToolDefinition typed;
    typed.id          = "test.typed";
    typed.displayName = "类型展示";
    typed.version     = "1";
    typed.outputs     = {
        {  "zero",  "零", {Type::Int}},
        {  "flag", "标志", {Type::Bool}},
        {"list", "列表", {Type::Array, DType::Int64, 1}},
        {  "text", "文本", {Type::String}}
    };
    auto typedSnapshot    = std::make_shared<NodeSnapshot>();
    typedSnapshot->node   = 2;
    typedSnapshot->toolId = typed.id;
    typedSnapshot->status = {0, "运行成功"};
    typedSnapshot->outputs = {
        {  "zero", std::int64_t{0}},
        {  "flag",              false},
        {  "list", Array::empty(DType::Int64, 1)},
        {  "text",    std::string{}},
    };
    OutputModel typedModel;
    typedModel.setResult(&typed, typedSnapshot, {});
    require(typedModel.get(2).value("preview").toString() == QStringLiteral("0"), "zero preview became blank");
    require(typedModel.get(3).value("preview").toString() == QStringLiteral("false"), "false preview became blank");
    require(typedModel.get(4).value("preview").toString() == QStringLiteral("[]"), "empty array preview became null");
    require(typedModel.get(5).value("preview").toString().isEmpty(), "empty string preview is not blank");
    require(std::get<std::int64_t>(typedModel.capture(2)->value) == 0
                && !std::get<bool>(typedModel.capture(3)->value)
                && std::holds_alternative<Array>(typedModel.capture(4)->value)
                && std::get<std::string>(typedModel.capture(5)->value).empty(),
            "typed outputs lost zero/false/empty values");
    require(OutputModel::fullText(*typedModel.capture(2)) == QStringLiteral("0"), "zero export became null");
    require(OutputModel::fullText(*typedModel.capture(3)) == QStringLiteral("false"), "false export became null");
    require(OutputModel::fullText(*typedModel.capture(4)) == QStringLiteral("[]"), "empty array export became null");
    require(OutputModel::fullText(*typedModel.capture(5)) == QStringLiteral("\"\""),
            "empty string export became null");
    typedModel.setResult(&typed, {}, {});
    for (int row = 2; row <= 5; ++row)
    {
        require(typedModel.get(row).value("preview").toString().isEmpty(), "unexecuted output fabricated a preview");
        const auto captured = typedModel.capture(row);
        require(captured && isMissing(captured->value), "unexecuted output fabricated a typed value");
    }
}

void pendingContract()
{
    QTemporaryDir directory;
    AppController app(1, directory.path());
    int           confirmations = 0, closes = 0;
    QObject::connect(&app, &AppController::unsavedDecisionRequired, [&] { ++confirmations; });
    QObject::connect(&app, &AppController::closeReady, [&] { ++closes; });
    app.requestClose();
    require(confirmations == 1 && closes == 0, "dirty close bypasses decision");
    app.resolveUnsaved("cancel");
    require(closes == 0 && app.dirty(), "cancel changes project or closes");
    app.requestClose();
    app.resolveUnsaved("save", directory.path() + "/absent/project.vrt.json");
    require(closes == 0 && app.dirty(), "failed save permits close");
    app.resolveUnsaved("discard");
    require(closes == 1, "discard does not finish pending close exactly once");
}

#include <chrono>
#include <thread>

void durationTextContract()
{
    require(durationText(std::nullopt) == QStringLiteral("—"), "missing measurement must be unavailable");
    require(durationText(-1.0) == QStringLiteral("—"), "negative measurement must be unavailable");
    require(durationText(std::numeric_limits<double>::quiet_NaN()) == QStringLiteral("—"),
            "NaN measurement must be unavailable");
    require(durationText(std::numeric_limits<double>::infinity()) == QStringLiteral("—"),
            "infinite measurement must be unavailable");
    require(durationText(0.0) == QStringLiteral("0 ms"), "measured zero must stay zero");
    require(durationText(0.005) == QStringLiteral("<0.01 ms"), "sub-centisecond measurement is not compact");
    require(durationText(0.01) == QStringLiteral("0.01 ms"), "centisecond measurement lost precision");
    require(durationText(1.10) == QStringLiteral("1.1 ms"), "trailing zeros are not removed");
    require(durationText(1234.567) == QStringLiteral("1234.57 ms"), "two-decimal rounding is wrong");
    const auto absent = nodeTimingText(nullptr, false);
    require(absent.total == QStringLiteral("未运行") && absent.toolCall == QStringLiteral("未运行")
                && absent.preprocess == QStringLiteral("—") && absent.postprocess == QStringLiteral("—"),
            "absent execution snapshot fabricated a measurement");
    NodeSnapshot snapshot;
    snapshot.timing.totalMs = 1234.567;
    snapshot.timing.adapterExecuteMs = 1.1;
    snapshot.timing.prepareMs = 27;
    snapshot.timing.initMs = 31;
    snapshot.timing.validateMs = 43;
    const auto uncalled = nodeTimingText(&snapshot, false);
    require(uncalled.total == QStringLiteral("1234.57 ms") && uncalled.toolCall == QStringLiteral("—"),
            "uncalled tool used an unmeasured adapter duration");
    require(uncalled.preprocess == QStringLiteral("—") && uncalled.postprocess == QStringLiteral("—"),
            "framework stages were relabeled as algorithm stages");
    require(uncalled.prepare == QStringLiteral("—") && uncalled.init == QStringLiteral("—")
                && uncalled.validate == QStringLiteral("—"),
            "unmeasured stages must display —");
    snapshot.timing.toolCallMeasured = true;
    snapshot.timing.prepareMeasured = true;
    snapshot.timing.initMeasured = true;
    snapshot.timing.validateMeasured = true;
    const auto called = nodeTimingText(&snapshot, false);
    require(called.total == QStringLiteral("1234.57 ms") && called.toolCall == QStringLiteral("1.1 ms"),
            "node total and adapter call measurements were conflated");
    require(called.prepare == QStringLiteral("27 ms") && called.init == QStringLiteral("31 ms")
                && called.validate == QStringLiteral("43 ms"),
            "measured framework stages did not display real values");
    const std::pair<double, QString> measurements[] = {
        {0.0, QStringLiteral("0 ms")}, {0.005, QStringLiteral("<0.01 ms")}, {1.1, QStringLiteral("1.1 ms")},
        {-1.0, QStringLiteral("—")},
        {std::numeric_limits<double>::quiet_NaN(), QStringLiteral("—")},
        {std::numeric_limits<double>::infinity(), QStringLiteral("—")}
    };
    for (const auto &[value, expected] : measurements)
    {
        snapshot.timing.totalMs = value;
        snapshot.timing.adapterExecuteMs = value;
        const auto measured = nodeTimingText(&snapshot, false);
        require(measured.total == expected && measured.toolCall == expected,
                "controlled snapshot lost its measured zero, small value or invalid metric semantics");
        require(measured.preprocess == QStringLiteral("—") && measured.postprocess == QStringLiteral("—"),
                "controlled snapshot fabricated independent algorithm measurements");
    }
    const NodeSnapshot *previousSnapshots[] = {nullptr, &snapshot};
    for (const auto *previous : previousSnapshots)
    {
        const auto running = nodeTimingText(previous, true);
        require(running.total == QStringLiteral("运行中") && running.toolCall == QStringLiteral("运行中")
                    && running.preprocess == QStringLiteral("—") && running.postprocess == QStringLiteral("—"),
                "running node reused an earlier snapshot measurement");
    }
    std::cout << "[Evidence T-03] Duration text formatting covers missing, zero, tiny, regular and non-finite values.\n";
    std::cout << "[Evidence T-03] Controlled snapshots distinguish absent, uncalled, measured and running values without relabeling framework stages.\n";
}

void nodeDiagnosticsContract()
{
    QTemporaryDir directory;
    QDir(directory.path()).mkpath("tools-package/tools/custom/test_category/test_diag");
    QFile pyFile(directory.path() + "/tools-package/python/custom/test_category/test_diag/test_diag.py");
    require(QDir().mkpath(QFileInfo(pyFile).absolutePath()), "cannot create Python fixture directory");
    if (pyFile.open(QIODevice::WriteOnly))
    {
        pyFile.write("def run(inputs):\n    return {'__status_code': 0, '__status_message': '', 'value': 42}\n");
        pyFile.close();
    }
    QFile failPyFile(directory.path() + "/tools-package/python/custom/test_category/test_diag/test_diag_fail.py");
    require(QDir().mkpath(QFileInfo(failPyFile).absolutePath()), "cannot create Python fixture directory");
    if (failPyFile.open(QIODevice::WriteOnly))
    {
        failPyFile.write("def run(inputs):\n    raise RuntimeError('diag boom')\n");
        failPyFile.close();
    }
    QFile jsonFile(directory.path() + "/tools-package/tools/custom/test_category/test_diag/test_diag.json");
    if (jsonFile.open(QIODevice::WriteOnly))
    {
        jsonFile.write(
            R"json({
  "schema_version": 2,
  "tool_id": "test.diag.python",
  "display_name": "诊断测试",
  "version": "1.0.0",
  "kind": "python_function",
  "entry": {"package": "test_category/test_diag", "file": "test_diag.py", "symbol": "run"},
  "init_params": [],
  "inputs": [
    {"id": "x", "name": "输入", "type": "Int", "default": 1, "min": 0, "max": 100, "step": 1}
  ],
  "outputs": [
    {"id": "value", "name": "输出", "type": "Int"}
  ]
})json");
        jsonFile.close();
    }
    QFile failJsonFile(directory.path() + "/tools-package/tools/custom/test_category/test_diag/test_diag_fail.json");
    if (failJsonFile.open(QIODevice::WriteOnly))
    {
        failJsonFile.write(
            R"json({
  "schema_version": 2,
  "tool_id": "test.diag.fail",
  "display_name": "诊断失败",
  "version": "1.0.0",
  "kind": "python_function",
  "entry": {"package": "test_category/test_diag", "file": "test_diag_fail.py", "symbol": "run"},
  "init_params": [],
  "inputs": [],
  "outputs": [
    {"id": "value", "name": "输出", "type": "Int"}
  ]
})json");
        failJsonFile.close();
    }
    QFile unfulfilledJson(directory.path() + "/tools-package/tools/custom/test_category/test_diag/unfulfilled.json");
    if (unfulfilledJson.open(QIODevice::WriteOnly))
    {
        unfulfilledJson.write(
            R"json({
  "schema_version": 2,
  "tool_id": "test.diag.unfulfilled",
  "display_name": "未提供实现测试",
  "version": "1.0.0",
  "kind": "host",
  "init_params": [],
  "inputs": [],
  "outputs": [],
  "entry": {"binding": "nonexistent.binding"}
})json");
        unfulfilledJson.close();
    }
    QFile indexFile(directory.path() + "/tools-package/module_index.json");
    if (indexFile.open(QIODevice::WriteOnly))
    {
        indexFile.write(
            R"json({
  "schema_version": 1,
  "variant": "cpu",
  "tools": [
    {"tool_id": "test.diag.python", "definition": "tools/custom/test_category/test_diag/test_diag.json"},
    {"tool_id": "test.diag.fail", "definition": "tools/custom/test_category/test_diag/test_diag_fail.json"},
    {"tool_id": "test.diag.unfulfilled", "definition": "tools/custom/test_category/test_diag/unfulfilled.json"}
  ],
  "modules": [],
  "runtime_files": []
})json");
        indexFile.close();
    }

    AppController app(1, directory.path());
    for (int i = 0; i < 50 && app.busy(); ++i)
    {
        QCoreApplication::processEvents();
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
    QFile projectFile(directory.path() + "/initial.vrt.json");
    if (projectFile.open(QIODevice::WriteOnly))
    {
        projectFile.write(
            R"json({
  "format_version": 3,
  "project": {"name": "诊断工程", "next_node_seq": "4", "next_flow_seq": "2"},
  "flows": [
    {
      "flow_id": "flow_1",
      "name": "流程1",
      "run_selected": true,
      "nodes": [
        {
          "node_seq": "1",
          "tool_id": "test.diag.python",
          "tool_name_snapshot": "诊断测试",
          "alias": "",
          "pos": [100, 100],
          "init_params": {},
          "type_snapshot": {
            "init_params": {},
            "inputs": {"x": {"type": "Int"}},
            "outputs": {"value": {"type": "Int"}}
          },
          "inputs": {
            "x": {"mode": "literal", "literal": 1}
          }
        },
        {
          "node_seq": "2",
          "tool_id": "test.diag.unfulfilled",
          "tool_name_snapshot": "未提供实现测试",
          "alias": "",
          "pos": [200, 200],
          "init_params": {},
          "type_snapshot": {
            "init_params": {},
            "inputs": {},
            "outputs": {}
          },
          "inputs": {}
        },
        {
          "node_seq": "3",
          "tool_id": "test.diag.fail",
          "tool_name_snapshot": "诊断失败",
          "alias": "",
          "pos": [300, 200],
          "init_params": {},
          "type_snapshot": {
            "init_params": {},
            "inputs": {},
            "outputs": {"value": {"type": "Int"}}
          },
          "inputs": {}
        }
      ]
    }
  ]
})json");
        projectFile.close();
    }
    app.requestOpen(directory.path() + "/initial.vrt.json");
    app.resolveUnsaved("discard");
    require(QTest::qWaitFor([&] { return !app.busy() && app.nodes().size() == 3; }, 5000),
            "project with unfulfilled tool did not open");

    const auto nodes = app.nodes();
    QStringList toolIds;
    for (const auto &item : app.toolDefinitions()) toolIds << item.toMap().value("id").toString();
    require(nodes.size() == 3, "expected 3 nodes in nodes(); tools=" + toolIds.join(',').toStdString()
                                   + " diagnostic=" + app.diagnostic().toStdString());

    const auto map0 = nodes.at(0).toMap();
    require(map0.contains("state"), "node missing state");
    require(map0.value("state").toString() == "idle", "initial node state is not idle");
    require(map0.contains("defMissing"), "node missing defMissing");
    require(map0.value("defMissing").toBool() == false, "registered node marked defMissing");
    require(map0.contains("implUnavailable"), "node missing implUnavailable");
    require(map0.value("implUnavailable").toBool() == false, "available node marked implUnavailable");
    require(!map0.contains("timingDetail"), "obsolete timingDetail source must be removed");
    require(map0.value("timingText").toString() == QStringLiteral("未运行"), "unrun node total is not 未运行");
    require(map0.value("timingOperatorText").toString() == QStringLiteral("未运行"),
            "unrun node operator time is not 未运行");
    require(map0.value("timingPreprocessText").toString() == QStringLiteral("—"),
            "unmeasured preprocess time must be unavailable");
    require(map0.value("timingPostprocessText").toString() == QStringLiteral("—"),
            "unmeasured postprocess time must be unavailable");

    const auto map1 = nodes.at(1).toMap();
    require(map1.value("defMissing").toBool() == false, "unfulfilled node marked defMissing");
    require(map1.value("implUnavailable").toBool() == true, "unfulfilled node not marked implUnavailable");

    app.runOnce();
    for (int i = 0; i < 100 && app.busy(); ++i)
    {
        QCoreApplication::processEvents();
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
    const auto executedNodes = app.nodes();
    require(!executedNodes.isEmpty(), "executed node empty");
    const auto executedMap = executedNodes.first().toMap();
    require(executedMap.value("state").toString() == "succeeded", "executed node is not succeeded");
    require(executedMap.value("timingText").toString().endsWith(QStringLiteral(" ms")),
            "executed node total timing is not measured");
    require(executedMap.value("timingOperatorText").toString().endsWith(QStringLiteral(" ms")),
            "executed node operator timing is not measured");
    require(executedMap.value("timingPreprocessText").toString() == QStringLiteral("—"),
            "executed node fabricates preprocess timing");
    require(executedMap.value("timingPostprocessText").toString() == QStringLiteral("—"),
            "executed node fabricates postprocess timing");

    const auto unavailableMap = executedNodes.at(1).toMap();
    require(unavailableMap.value("state").toString() == "failed", "unavailable implementation is not failed");
    require(unavailableMap.value("timingOperatorText").toString() == QStringLiteral("—"),
            "implementation without a tool call must not report operator time");

    const auto failedMap = executedNodes.at(2).toMap();
    require(failedMap.value("state").toString() == "failed", "failing tool is not failed");
    require(failedMap.value("statusCode").toLongLong() != 0, "failing tool lost its status code");
    require(!failedMap.value("statusMessage").toString().isEmpty(), "failing tool lost its diagnostic message");
    require(failedMap.value("timingText").toString().endsWith(QStringLiteral(" ms")),
            "failing tool lost its measured total timing");

    const QString pythonId = map0.value("id").toString();
    app.selectNode(pythonId);
    require(app.setParameter("x", false, "2"), "valid integer parameter change was rejected");
    const auto staleMap = app.nodes().first().toMap();
    require(staleMap.value("state").toString() == "succeeded",
            "parameter change discarded the last successful display state");
    require(staleMap.value("timingText").toString().endsWith(QStringLiteral(" ms")),
            "parameter change discarded the previous total timing");
    require(staleMap.value("timingOperatorText").toString().endsWith(QStringLiteral(" ms")),
            "parameter change discarded the previous operator timing");
    require(staleMap.value("statusCode").toLongLong() == 0, "stale snapshot still exposes an old status");

    app.runOnce();
    for (int i = 0; i < 100 && app.busy(); ++i)
    {
        QCoreApplication::processEvents();
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
    require(app.nodes().first().toMap().value("timingText").toString().endsWith(QStringLiteral(" ms")),
            "re-run after a revision change does not refresh timing");
    std::cout << "[Evidence T-03] Structured node timing, stale revision invalidation and diagnostics verified.\n";
}


void mainQmlContract(double scale)
{
    QQuickStyle::setStyle("Basic");
    QTemporaryDir directory;
    const auto    dirPath = directory.path();

    // 1. Prepare plugin and JSON definitions
    QDir(dirPath).mkpath("tools-package/bin");
    const QString dllSrc = QCoreApplication::applicationDirPath() + "/test_plugin_module.dll";
    const QString dllDst = dirPath + "/tools-package/bin/test_plugin_module.dll";
    if (QFile::exists(dllSrc))
    {
        QFile::copy(dllSrc, dllDst);
    }

    QFile indexFile(dirPath + "/tools-package/module_index.json");
    if (indexFile.open(QIODevice::WriteOnly))
    {
        indexFile.write(
            R"json({
  "schema_version": 1,
  "variant": "cpu",
  "modules": [
    {"id": "test_module", "library": "bin/test_plugin_module.dll"}
  ],
  "tools": [
    {"tool_id": "test.plugin.counter", "module": "test_module", "definition": "tools/builtin/Plugin/Basic/counter.json"},
    {"tool_id": "test.plugin.sleep", "module": "test_module", "definition": "tools/builtin/Plugin/Basic/sleep.json"},
    {"tool_id": "test.plugin.fail_init", "module": "test_module", "definition": "tools/builtin/Plugin/Basic/fail.json"}
  ],
  "runtime_files": []
})json");
        indexFile.close();
    }

    QDir(dirPath).mkpath("tools-package/tools/builtin/Plugin/Basic");
    QFile sleepJson(dirPath + "/tools-package/tools/builtin/Plugin/Basic/sleep.json");
    if (sleepJson.open(QIODevice::WriteOnly))
    {
        sleepJson.write(
            R"json({
  "schema_version": 2,
  "tool_id": "test.plugin.sleep",
  "display_name": "Plugin Sleep",
  "version": "1.0.0",
  "kind": "cpp",
  "init_params": [],
  "inputs": [],
  "outputs": [
    {"id": "y", "name": "Y", "type": "Int"}
  ]
})json");
        sleepJson.close();
    }

    QFile counterJson(dirPath + "/tools-package/tools/builtin/Plugin/Basic/counter.json");
    if (counterJson.open(QIODevice::WriteOnly))
    {
        counterJson.write(
            R"json({
  "schema_version": 2,
  "tool_id": "test.plugin.counter",
  "display_name": "Plugin Counter",
  "version": "1.0.0",
  "kind": "cpp",
  "init_params": [],
  "inputs": [
    {"id": "step", "name": "Step", "type": "Int", "default": 1}
  ],
  "outputs": [
    {"id": "count", "name": "Count", "type": "Int"}
  ]
})json");
        counterJson.close();
    }

    QFile failJson(dirPath + "/tools-package/tools/builtin/Plugin/Basic/fail.json");
    if (failJson.open(QIODevice::WriteOnly))
    {
        failJson.write(
            R"json({
  "schema_version": 2,
  "tool_id": "test.plugin.fail_init",
  "display_name": "Fail Init Tool",
  "version": "1.0.0",
  "kind": "cpp",
  "init_params": [],
  "inputs": [],
  "outputs": []
})json");
        failJson.close();
    }

    AppController app(1, dirPath);
    for (int i = 0; i < 50 && app.busy(); ++i)
    {
        QCoreApplication::processEvents();
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }

    qmlRegisterType<visionrt::ImageViewport>("VisionRT.View", 1, 0, "ImageViewport");
    auto *theme = visionrt::Theme::instance();
    qmlRegisterSingletonInstance<visionrt::Theme>("VisionRT", 1, 0, "Theme", theme);

    QQmlApplicationEngine engine;
    const auto            appDir = QCoreApplication::applicationDirPath();
    engine.addImportPath(appDir + "/qml");
    engine.addImportPath(appDir + "/../qml");
#ifdef VISIONRT_SOURCE_DIR
    const auto sourceDir = QStringLiteral(VISIONRT_SOURCE_DIR);
    engine.addImportPath(sourceDir + "/src/ui/qml");
    engine.addImportPath(sourceDir + "/3rdparty/QuickUI/quickui");
    const auto qmlPath = sourceDir + "/src/ui/qml/Main.qml";
#else
    const auto qmlPath = appDir + "/../src/ui/qml/Main.qml";
#endif

    engine.rootContext()->setContextProperty("app", &app);
    engine.rootContext()->setContextProperty("Theme", theme);
    engine.rootContext()->setContextProperty("demoMenu", false);
    engine.rootContext()->setContextProperty("demoSelect", false);

    if (QFile::exists(qmlPath))
    {
        engine.load(QUrl::fromLocalFile(qmlPath));
    }
    else
    {
        engine.loadFromModule("VisionRT", "Main");
    }

    const auto roots = engine.rootObjects();
    require(!roots.isEmpty(), "Main.qml failed to instantiate");
    auto *window = qobject_cast<QQuickWindow *>(roots.first());
    require(window != nullptr, "Main.qml root is not QQuickWindow");

    // 1. DPI verification & logical viewport strictly 960x540
    window->setFlags(Qt::FramelessWindowHint | Qt::Window);
    window->resize(960, 540);
    window->show();
    for (int i = 0; i < 20; ++i) QCoreApplication::processEvents();

    const double dpr = window->devicePixelRatio();
    require(std::abs(dpr - scale) < 0.05, "DPR mismatch with target scale");
    require(window->width() == 960, "window width is not strictly 960");
    require(window->height() == 540, "window height is not strictly 540");

    // 2. Initial state verification & drawer items
    require(window->property("leftExpanded").toBool() == true, "drawer initially not expanded");
    app.save(dirPath + "/test.vrt.json");
    require(QTest::qWaitFor([&] { return !app.busy(); }, 5000), "node instance synchronization did not finish");
    require(app.dirty() == false, "project initially dirty after save");

    auto *leftHandle     = window->findChild<QQuickItem *>("leftHandle");
    auto *leftHandleMask = window->findChild<QQuickItem *>("leftHandleMask");
    auto *leftHost       = window->findChild<QQuickItem *>("leftHost");
    auto *leftContent    = window->findChild<QQuickItem *>("leftContent");
    require(leftHandle != nullptr, "missing leftHandle");
    require(leftHandleMask != nullptr, "missing leftHandleMask");
    require(leftHost != nullptr, "missing leftHost");
    require(leftContent != nullptr, "missing leftContent");
    require(leftContent->isVisible(), "leftContent initially not visible");
    require(leftHandle->width() == 1, "initial handle width != 1");

    // 3. Drawer Collapse (toggleLeft) & zero hit test on hidden handle/mask
    QMetaObject::invokeMethod(window, "toggleLeft");
    for (int i = 0; i < 20; ++i) QCoreApplication::processEvents();

    require(window->property("leftExpanded").toBool() == false, "drawer did not collapse");
    require(leftHost->width() == 28, "collapsed leftHost width != 28");
    require(leftHandle->implicitWidth() == 0, "collapsed leftHandle implicitWidth != 0");
    require(leftHandleMask->width() == 0, "collapsed leftHandleMask width != 0");
    require(!leftHandle->isVisible(), "collapsed leftHandle should be invisible");
    require(!leftHandle->isEnabled(), "collapsed leftHandle should be disabled");
    require(!leftHandle->hasActiveFocus(), "collapsed leftHandle must not have active focus");
    require(!leftContent->isVisible(), "collapsed leftContent should be hidden");

    // Hidden handle mouse click does not expand drawer
    QMouseEvent handleClick(QEvent::MouseButtonPress, QPointF(0, 0), Qt::LeftButton, Qt::LeftButton, Qt::NoModifier);
    QCoreApplication::sendEvent(leftHandle, &handleClick);
    require(!window->property("leftExpanded").toBool(), "mouse click on hidden handle expanded drawer");

    auto *drawerToggle = window->findChild<QQuickItem *>("drawerToggle");
    require(drawerToggle != nullptr && drawerToggle->isVisible(), "drawerToggle missing or invisible");
    require(window->activeFocusItem() == drawerToggle, "focus did not switch to drawerToggle upon collapse");
    require(app.dirty() == false, "collapsing drawer dirtied the project");

    // 4. Ctrl+F from collapsed drawer: expands drawer and focuses search field
    QKeyEvent ctrlFPress(QEvent::KeyPress, Qt::Key_F, Qt::ControlModifier, "f");
    QKeyEvent ctrlFRelease(QEvent::KeyRelease, Qt::Key_F, Qt::ControlModifier);
    QCoreApplication::sendEvent(window, &ctrlFPress);
    QCoreApplication::sendEvent(window, &ctrlFRelease);
    for (int i = 0; i < 20; ++i) QCoreApplication::processEvents();

    require(window->property("leftExpanded").toBool() == true, "Ctrl+F event did not expand left drawer");
    auto *flowSearch = window->findChild<QQuickItem *>("flowSearch");
    require(flowSearch != nullptr, "missing flowSearch input field");
    require(flowSearch->hasActiveFocus(), "flowSearch input does not have active focus after Ctrl+F");

    // 5. Tab key: visible focus target
    QKeyEvent tabPress(QEvent::KeyPress, Qt::Key_Tab, Qt::NoModifier);
    QKeyEvent tabRelease(QEvent::KeyRelease, Qt::Key_Tab, Qt::NoModifier);
    QCoreApplication::sendEvent(window, &tabPress);
    QCoreApplication::sendEvent(window, &tabRelease);
    for (int i = 0; i < 10; ++i) QCoreApplication::processEvents();

    auto *activeFocus = window->activeFocusItem();
    require(activeFocus != nullptr, "no active focus item after Tab key");
    require(activeFocus->isVisible(), "active focus item after Tab is not visible");

    // 6. Running state, loop execution, and drawer toggling
    app.addNode("test.plugin.sleep", 100, 100);
    require(QTest::qWaitFor([&] { return !app.busy(); }, 5000), "node instance synchronization did not finish");
    app.addNode("test.plugin.counter", 200, 100);
    require(QTest::qWaitFor([&] { return !app.busy(); }, 5000), "node instance synchronization did not finish");
    app.save(dirPath + "/test.vrt.json");
    require(QTest::qWaitFor([&] { return !app.busy(); }, 5000), "node instance synchronization did not finish");
    require(app.dirty() == false, "project dirty after save with nodes");

    const auto nodesBeforeRun = app.nodes();
    require(!nodesBeforeRun.isEmpty(), "nodes empty after adding nodes. diag: " + app.diagnostic().toStdString());
    require(nodesBeforeRun.first().toMap().value("state").toString() == "idle", "initial node state is not idle");

    // Start continuous loop
    app.toggleLoop();
    for (int i = 0; i < 50 && app.runState() != "RunningLoop"; ++i)
    {
        QCoreApplication::processEvents();
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
    require(app.runState() == "RunningLoop", "app failed to enter RunningLoop state");

    // Observe active running node state is "running"
    bool observedRunning = false;
    for (int i = 0; i < 50; ++i)
    {
        QCoreApplication::processEvents();
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
        for (const auto &n : app.nodes())
        {
            if (n.toMap().value("state").toString() == "running")
            {
                observedRunning = true;
                break;
            }
        }
        if (observedRunning)
            break;
    }
    require(observedRunning, "Running node did not exhibit 'running' state in RunningLoop");
    std::cout << "[Evidence HS5-03] Node state 'running' observed during RunningLoop execution.\n";

    // Drawer toggle while running: does not dirty project or mutate instance identities
    const auto *hostIdentityBefore    = leftHost;
    const auto *contentIdentityBefore = leftContent;
    QMetaObject::invokeMethod(window, "toggleLeft");
    for (int i = 0; i < 15; ++i) QCoreApplication::processEvents();
    require(app.dirty() == false, "toggling drawer during active execution dirtied project");

    QMetaObject::invokeMethod(window, "toggleLeft");
    for (int i = 0; i < 15; ++i) QCoreApplication::processEvents();
    require(app.dirty() == false, "re-expanding drawer during active execution dirtied project");
    require(window->findChild<QQuickItem *>("leftHost") == hostIdentityBefore,
            "leftHost identity changed across toggle");
    require(window->findChild<QQuickItem *>("leftContent") == contentIdentityBefore,
            "leftContent identity changed across toggle");

    // Stop loop and wait for Idle
    app.toggleLoop();
    for (int i = 0; i < 50 && app.busy(); ++i)
    {
        QCoreApplication::processEvents();
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
    require(app.runState() == "Idle", "app failed to return to Idle after stopping loop");

    // Node state after successful run is green
    const auto nodesAfterRun = app.nodes();
    require(!nodesAfterRun.isEmpty(), "nodes empty after running loop");
    require(nodesAfterRun.first().toMap().value("state").toString() == "succeeded",
            "node state after execution is not succeeded");

    // Add failing node and verify red error state
    app.addNode("test.plugin.fail_init", 300, 100);
    require(QTest::qWaitFor([&] { return !app.busy(); }, 5000), "node instance synchronization did not finish");
    app.runOnce();
    for (int i = 0; i < 50 && app.busy(); ++i)
    {
        QCoreApplication::processEvents();
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
    const auto nodesWithFail = app.nodes();
    require(nodesWithFail.size() == 3, "expected 3 nodes");
    require(nodesWithFail.at(2).toMap().value("state").toString() == "failed", "failing node state is not failed");

    // 7. Real ROI draft and parameter draft preservation across drawer collapse and expand
    {
        visionrt::ImageViewport roiViewport;
        roiViewport.setWidth(500);
        roiViewport.setHeight(400);
        roiViewport.beginRoiEdit({1, "rois", 0, -1}, {});
        roiViewport.setDrawShape(2); // Rectangle
        QMouseEvent pressEv(QEvent::MouseButtonPress, roiViewport.imageToItem({20, 20}), Qt::LeftButton, Qt::LeftButton,
                            Qt::NoModifier);
        QCoreApplication::sendEvent(&roiViewport, &pressEv);
        QMouseEvent moveEv(QEvent::MouseMove, roiViewport.imageToItem({60, 60}), Qt::NoButton, Qt::LeftButton,
                           Qt::NoModifier);
        QCoreApplication::sendEvent(&roiViewport, &moveEv);
        require(roiViewport.editing(), "ROI viewport not in editing state");

        // Layout / drawer width change preserves draft
        roiViewport.setWidth(700);
        require(roiViewport.editing(), "ROI draft lost upon layout expansion");
        roiViewport.setWidth(300);
        require(roiViewport.editing(), "ROI draft lost upon layout contraction");
        roiViewport.cancelDraft();
        roiViewport.endRoiEdit();
        std::cout << "[Evidence HS5-03] Real ROI draft preserved across drawer/layout width changes.\n";
    }

    QString counterNodeId;
    for (const auto &n : nodesWithFail)
    {
        if (n.toMap().value("toolId").toString() == "test.plugin.counter")
        {
            counterNodeId = n.toMap().value("id").toString();
            break;
        }
    }
    require(!counterNodeId.isEmpty(), "missing counter node");
    app.selectNode(counterNodeId);
    app.setParameter("step", false, "99");
    require(app.dirty() == true, "setting parameter did not mark project dirty");
    QMetaObject::invokeMethod(window, "toggleLeft");
    for (int i = 0; i < 10; ++i) QCoreApplication::processEvents();
    QMetaObject::invokeMethod(window, "toggleLeft");
    for (int i = 0; i < 10; ++i) QCoreApplication::processEvents();
    require(app.dirty() == true, "dirty parameter state lost across drawer toggle");
    bool paramPreserved = false;
    for (const auto &paramVar : app.parameters())
    {
        const auto pMap = paramVar.toMap();
        if (pMap.value("id").toString() == "step")
        {
            paramPreserved = (pMap.value("literal").toString() == "99" || pMap.value("valueText").toString() == "99");
        }
    }
    require(paramPreserved, "modified parameter draft was not preserved across drawer toggle");

    hoverCardContract(app, window, scale);

    // 8. Screenshot capture evidence for this scale
    QImage frame = window->grabWindow();
    require(!frame.isNull(), "grabWindow failed");
    const int expectedPhysicalWidth  = static_cast<int>(std::round(960.0 * dpr));
    const int expectedPhysicalHeight = static_cast<int>(std::round(540.0 * dpr));
    require(std::abs(frame.width() - expectedPhysicalWidth) <= 2, "Physical frame width does not match logical * DPR");
    require(std::abs(frame.height() - expectedPhysicalHeight) <= 2,
            "Physical frame height does not match logical * DPR");

    const std::filesystem::path evidenceDir = "build/evidence";
    std::filesystem::create_directories(evidenceDir);
    char scaleBuf[16];
    std::snprintf(scaleBuf, sizeof(scaleBuf), "%.1f", scale);
    const auto screenshotName = "workbench_dpi_" + std::string(scaleBuf) + ".png";
    const auto screenshotPath = evidenceDir / screenshotName;
    const bool saved          = frame.save(QString::fromStdString(screenshotPath.string()));
    require(saved, "Failed to save screenshot evidence to " + screenshotPath.string());
    require(std::filesystem::exists(screenshotPath) && std::filesystem::file_size(screenshotPath) > 0,
            "Saved screenshot file does not exist or is empty");

    std::cout << "[Evidence HS5-03] Child process scale=" << scaleBuf << ", actual DPR=" << dpr << ", logical size=("
              << window->width() << ", " << window->height() << ")"
              << ", physical size=(" << frame.width() << ", " << frame.height() << ")"
              << ", screenshot=" << screenshotPath.string() << "\n";
}

void packageDrivenCatalogAndRefreshContract()
{
    std::cout << "\n[PACKAGE-DRIVEN CATALOG & REFRESH CONTRACT START]\n";
    QTemporaryDir directory;
    const auto    dirPath = directory.path();

    // 1. Empty catalog on startup without package
    {
        AppController app(1, dirPath);
        for (int i = 0; i < 50 && app.busy(); ++i)
        {
            QCoreApplication::processEvents();
            std::this_thread::sleep_for(std::chrono::milliseconds(10));
        }
        require(app.toolDefinitions().isEmpty(),
                "workbench must start with empty tool catalog when no package is installed");
        std::cout << "[Evidence T-05] Empty catalog on startup without package verified.\n";

        // 2. Placing JSON into old host directories without package does NOT populate tools
        QDir(dirPath).mkpath("tools/builtin/flow/func");
        QFile legacyJson(dirPath + "/tools/builtin/flow/func/platform_func.json");
        if (legacyJson.open(QIODevice::WriteOnly))
        {
            legacyJson.write(
                R"json({
  "schema_version": 2,
  "tool_id": "test.legacy.func",
  "display_name": "遗留函数",
  "version": "1.0.0",
  "kind": "cpp",
  "init_params": [],
  "inputs": [],
  "outputs": []
})json");
            legacyJson.close();
        }
        QDir(dirPath).mkpath("tools/custom/user/py");
        QFile legacyPy(dirPath + "/tools/custom/user/py/legacy.py");
        if (legacyPy.open(QIODevice::WriteOnly))
        {
            legacyPy.write("def run(i): return {'val': 1}\n");
            legacyPy.close();
        }
        QFile legacyPyJson(dirPath + "/tools/custom/user/py/legacy.json");
        if (legacyPyJson.open(QIODevice::WriteOnly))
        {
            legacyPyJson.write(
                R"json({
  "schema_version": 2,
  "tool_id": "test.legacy.python",
  "display_name": "遗留脚本",
  "version": "1.0.0",
  "kind": "python_function",
  "entry": {"file": "legacy.py", "symbol": "run"},
  "init_params": [],
  "inputs": [],
  "outputs": []
})json");
            legacyPyJson.close();
        }
        app.rescanTools();
        for (int i = 0; i < 50 && app.busy(); ++i)
        {
            QCoreApplication::processEvents();
            std::this_thread::sleep_for(std::chrono::milliseconds(10));
        }
        require(app.toolDefinitions().isEmpty(),
                "putting JSON into old host directories must not populate tools without package");
        std::cout
            << "[Evidence T-05] Legacy directory exclusion verified: 0 tools discovered from unindexed legacy paths.\n";
    }

    // 3. Deploy valid tools-package with plugin C++ tools, Python tool, and platform receiver node
    QDir(dirPath).mkpath("tools-package/bin");
    const QString dllSrc = QCoreApplication::applicationDirPath() + "/test_plugin_module.dll";
    const QString dllDst = dirPath + "/tools-package/bin/test_plugin_module.dll";
    if (QFile::exists(dllSrc))
    {
        QFile::copy(dllSrc, dllDst);
    }

    // Package builtin C++ counter
    QDir(dirPath).mkpath("tools-package/tools/builtin/pkg/counter");
    QFile rootCounterJson(dirPath + "/tools-package/tools/builtin/pkg/counter/counter.json");
    if (rootCounterJson.open(QIODevice::WriteOnly))
    {
        rootCounterJson.write(
            R"json({
  "schema_version": 2,
  "tool_id": "test.plugin.counter",
  "display_name": "计数器插件",
  "version": "1.0.0",
  "kind": "cpp",
  "init_params": [
    {"id": "initial", "name": "初始值", "type": "Int", "default": 10}
  ],
  "inputs": [],
  "outputs": [
    {"id": "count", "name": "计数值", "type": "Int"}
  ]
})json");
        rootCounterJson.close();
    }

    // Package builtin C++ function
    QDir(dirPath).mkpath("tools-package/tools/builtin/pkg/function");
    QFile rootFunctionJson(dirPath + "/tools-package/tools/builtin/pkg/function/function.json");
    if (rootFunctionJson.open(QIODevice::WriteOnly))
    {
        rootFunctionJson.write(
            R"json({
  "schema_version": 2,
  "tool_id": "test.plugin.function",
  "display_name": "函数插件",
  "version": "1.0.0",
  "kind": "cpp",
  "init_params": [],
  "inputs": [
    {"id": "x", "name": "输入", "type": "Int", "default": 5}
  ],
  "outputs": [
    {"id": "y", "name": "输出", "type": "Int"}
  ]
})json");
        rootFunctionJson.close();
    }

    // Package Python tool
    QDir(dirPath).mkpath("tools-package/tools/custom/user/py");
    QFile pyScriptA(dirPath + "/tools-package/python/custom/user/py/user_node_a.py");
    require(QDir().mkpath(QFileInfo(pyScriptA).absolutePath()), "cannot create Python fixture directory");
    if (pyScriptA.open(QIODevice::WriteOnly))
    {
        pyScriptA.write(
            "def run(inputs):\n"
            "    return {'__status_code': 0, '__status_message': 'ok', 'result': 111, 'image': []}\n");
        pyScriptA.close();
    }
    QFile root3JsonA(dirPath + "/tools-package/tools/custom/user/py/user_node_a.json");
    if (root3JsonA.open(QIODevice::WriteOnly))
    {
        root3JsonA.write(
            R"json({
  "schema_version": 2,
  "tool_id": "test.user.pythonA",
  "display_name": "用户脚本A",
  "version": "1.0.0",
  "kind": "python_function",
  "entry": {"package": "user/py", "file": "user_node_a.py", "symbol": "run"},
  "init_params": [],
  "inputs": [],
  "outputs": [
    {"id": "result", "name": "结果", "type": "Int"},
    {"id": "image", "name": "图像", "type": "Image"}
  ]
})json");
        root3JsonA.close();
    }

    // Platform receiver tool with host_binding
    QDir(dirPath).mkpath("tools-package/tools/builtin/flow/recv");
    QFile recvJson(dirPath + "/tools-package/tools/builtin/flow/recv/receiver.json");
    if (recvJson.open(QIODevice::WriteOnly))
    {
        recvJson.write(
            R"json({
  "schema_version": 2,
  "tool_id": "test.custom.receiver",
  "display_name": "动态接收器",
  "version": "1.0.0",
  "kind": "host",
  "init_params": [
    {"id": "source_flow_id", "name": "源流程", "type": "String", "default": ""},
    {"id": "source_node_seq", "name": "源节点", "type": "String", "default": ""},
    {"id": "source_port_id", "name": "源端口", "type": "String", "default": ""},
    {"id": "timeout_ms", "name": "超时时间(ms)", "type": "Int", "default": 500}
  ],
  "inputs": [],
  "outputs": [
    {"id": "value", "name": "接收值", "type_from": {"flow": "source_flow_id", "node": "source_node_seq", "port": "source_port_id"}},
    {"id": "source_status_code", "name": "源状态码", "type": "Int"},
    {"id": "source_status_message", "name": "源状态消息", "type": "String"}
  ],
  "entry": {"binding": "visionrt.receiver"}
})json");
        recvJson.close();
    }

    // Unindexed JSON inside package
    QDir(dirPath).mkpath("tools-package/unindexed");
    QFile unindexedJson(dirPath + "/tools-package/unindexed/unindexed.json");
    if (unindexedJson.open(QIODevice::WriteOnly))
    {
        unindexedJson.write(
            R"json({
  "schema_version": 2,
  "tool_id": "test.unindexed.tool",
  "display_name": "未索引工具",
  "version": "1.0.0",
  "kind": "cpp",
  "init_params": [],
  "inputs": [],
  "outputs": []
})json");
        unindexedJson.close();
    }

    // module_index.json
    QFile indexFile(dirPath + "/tools-package/module_index.json");
    if (indexFile.open(QIODevice::WriteOnly))
    {
        indexFile.write(
            R"json({
  "schema_version": 1,
  "variant": "cpu",
  "modules": [
    {"id": "test_module", "library": "bin/test_plugin_module.dll"}
  ],
  "tools": [
    {"tool_id": "test.plugin.counter", "module": "test_module", "definition": "tools/builtin/pkg/counter/counter.json"},
    {"tool_id": "test.plugin.function", "module": "test_module", "definition": "tools/builtin/pkg/function/function.json"},
    {"tool_id": "test.user.pythonA", "definition": "tools/custom/user/py/user_node_a.json"},
    {"tool_id": "test.custom.receiver", "definition": "tools/builtin/flow/recv/receiver.json"}
  ],
  "runtime_files": []
})json");
        indexFile.close();
    }

    const QString projectFile = dirPath + "/test_project.vrt.json";
    {
        AppController app(1, dirPath);
        for (int i = 0; i < 50 && app.busy(); ++i)
        {
            QCoreApplication::processEvents();
            std::this_thread::sleep_for(std::chrono::milliseconds(10));
        }

        const auto defs = app.toolDefinitions();
        require(defs.size() == 4,
                "expected exactly 4 indexed tools in toolDefinitions, got " + std::to_string(defs.size()));

        bool hasCounter = false, hasFunction = false, hasPyA = false, hasReceiver = false, hasUnindexed = false;
        for (const auto &item : defs)
        {
            const auto map   = item.toMap();
            const auto id    = map.value("id").toString();
            const bool avail = map.value("available").toBool();
            if (id == "test.plugin.counter")
            {
                hasCounter = true;
                require(avail, "counter tool not available");
            }
            if (id == "test.plugin.function")
            {
                hasFunction = true;
                require(avail, "function tool not available");
            }
            if (id == "test.user.pythonA")
            {
                hasPyA = true;
                require(avail, "pythonA tool not available");
            }
            if (id == "test.custom.receiver")
            {
                hasReceiver = true;
                require(avail, "receiver tool not available");
                require(map.value("displayName").toString() == QStringLiteral("动态接收器"),
                        "receiver displayName mismatch");
            }
            if (id == "test.unindexed.tool")
                hasUnindexed = true;
        }
        require(hasCounter && hasFunction && hasPyA && hasReceiver, "definitions missing expected tools");
        require(!hasUnindexed, "unindexed tool must NOT appear in toolDefinitions");
        std::cout
            << "[Evidence T-05] Package-driven discovery verified: exactly 4 indexed tools, unindexed excluded.\n";

        // Initial flow 1
        const auto initialFlows = app.flows();
        require(!initialFlows.isEmpty(), "flows must not be empty");
        const auto flow1Name = initialFlows.first().toMap().value("name").toString();
        const auto flow1Id   = initialFlows.first().toMap().value("id").toString();

        app.addNode("test.plugin.function", 100, 100);


        require(QTest::qWaitFor([&] { return !app.busy(); }, 5000), "node instance synchronization did not finish");
        app.addNode("test.plugin.counter", 200, 100);
        require(QTest::qWaitFor([&] { return !app.busy(); }, 5000), "node instance synchronization did not finish");
        app.addNode("test.user.pythonA", 300, 100);
        require(QTest::qWaitFor([&] { return !app.busy(); }, 5000), "node instance synchronization did not finish");

        QString funcNodeTitle;
        QString pythonNodeTitle;
        QString funcNodeId;
        QString pythonNodeId;
        for (const auto &n : app.nodes())
        {
            const auto m = n.toMap();
            if (m.value("toolId").toString() == "test.plugin.function")
            {
                funcNodeTitle = m.value("title").toString();
                funcNodeId    = m.value("id").toString();
            }
            if (m.value("toolId").toString() == "test.user.pythonA")
            {
                pythonNodeTitle = m.value("title").toString();
                pythonNodeId    = m.value("id").toString();
            }
        }
        require(!funcNodeTitle.isEmpty(), "missing function node title");

        // Add Flow 2 for cross-flow receiver
        app.addFlow("flow_2");
        app.setFlowSelected("flow_2", true);
        app.addNode("test.custom.receiver", 100, 100);
        require(QTest::qWaitFor([&] { return !app.busy(); }, 5000), "node instance synchronization did not finish");

        QString recvNodeId;
        for (const auto &n : app.nodes())
        {
            const auto m = n.toMap();
            if (m.value("toolId").toString() == "test.custom.receiver")
            {
                recvNodeId = m.value("id").toString();
            }
        }
        require(!recvNodeId.isEmpty(), "missing receiver node in flow_2");

        app.selectNode(recvNodeId);
        auto *receiverOutputs = qobject_cast<OutputModel *>(app.outputModel());
        require(receiverOutputs->get(2).value("type").toString() == QStringLiteral("未绑定"), "receiver is typed before source selection");
        auto choices = [&](const QString &parameter, const QString &field)
        {
            QVariantList result;
            for (const auto &item : app.parameters())
            {
                const auto row = item.toMap();
                if (row.value("id").toString() != parameter)
                    continue;
                for (const auto &option : row.value("options").toList())
                    result.push_back(option.toMap().value(field));
            }
            return result;
        };
        require(choices("source_flow_id", "label").contains(flow1Name), "source flow choices are missing");
        require(choices("source_flow_id", "value").contains(flow1Id), "source flow identity is missing");
        app.setParameter("source_flow_id", true, flow1Id);
        require(QTest::qWaitFor([&] { return !app.busy(); }, 5000), "node instance synchronization did not finish");
        require(!pythonNodeTitle.isEmpty(), "missing Python image source");
        app.setParameter("source_node_seq", true, pythonNodeId);
        require(QTest::qWaitFor([&] { return !app.busy(); }, 5000), "node instance synchronization did not finish");
        app.setParameter("source_port_id", true, "image");
        require(QTest::qWaitFor([&] { return !app.busy(); }, 5000), "node instance synchronization did not finish");
        require(app.imageOutputs().size() == 1
                    && app.imageOutputs().first().toMap().value("id").toString() == "value",
                "source-derived image output is missing from the viewport selector");
        require(choices("source_node_seq", "label").contains(funcNodeTitle), "source node choices are missing");
        app.setParameter("source_node_seq", true, funcNodeId);
        require(QTest::qWaitFor([&] { return !app.busy(); }, 5000), "node instance synchronization did not finish");
        require(choices("source_port_id", "value").contains(QStringLiteral("y")), "source output choices are missing");
        app.setParameter("source_port_id", true, "y");
        require(QTest::qWaitFor([&] { return !app.busy(); }, 5000), "node instance synchronization did not finish");
        require(receiverOutputs->get(2).value("type").toString() == typeText({Type::Int}),
                "receiver output not resolved after source selection");
        app.setParameter("timeout_ms", true, "1000");
        require(QTest::qWaitFor([&] { return !app.busy(); }, 5000), "node instance synchronization did not finish");

        // Run once
        app.runOnce();
        for (int i = 0; i < 100 && app.busy(); ++i)
        {
            QCoreApplication::processEvents();
            std::this_thread::sleep_for(std::chrono::milliseconds(10));
        }

        auto *outModel = qobject_cast<OutputModel *>(app.outputModel());
        require(outModel != nullptr, "outputModel is null");

        // Receiver result
        app.selectNode(recvNodeId);
        const auto recvPreview = outModel->get(2).value("preview").toString();
        require(recvPreview == "10", "unexpected receiver output: " + recvPreview.toStdString());
        std::cout << "[Evidence T-05] Cross-flow receive of dynamic host_binding node verified, value="
                  << recvPreview.toStdString() << "\n";

        app.addNode("test.plugin.function", 200, 200);


        require(QTest::qWaitFor([&] { return !app.busy(); }, 5000), "node instance synchronization did not finish");
        const auto relationTarget = app.selectedNode();
        require(app.bindInput(app.portContext("x"), recvNodeId, "value"), "cannot bind source-derived receiver output");
        require(app.wires().size() == 1, "valid receiver relation is missing");
        const auto relationPorts = app.wires().front().toMap().value("ports").toList();
        require(relationPorts.size() == 1 && relationPorts.front().toMap().value("output").toString() == "value"
                    && relationPorts.front().toMap().value("input").toString() == "x",
                "grouped wire discarded the source and target port identity");
        app.selectNode(recvNodeId);
        app.setParameter("source_node_seq", true, pythonNodeId);
        require(QTest::qWaitFor([&] { return !app.busy(); }, 5000), "node instance synchronization did not finish");
        app.setParameter("source_port_id", true, "image");
        require(QTest::qWaitFor([&] { return !app.busy(); }, 5000), "node instance synchronization did not finish");
        require(app.wires().isEmpty(), "incompatible or unresolved receiver binding is still drawn as an active wire");
        require(outModel->get(2).value("count").toInt() == 0,
                "invalid receiver relation is still counted as an active output reference");
        app.setParameter("source_node_seq", true, funcNodeId);
        require(QTest::qWaitFor([&] { return !app.busy(); }, 5000), "node instance synchronization did not finish");
        app.setParameter("source_port_id", true, "y");
        require(QTest::qWaitFor([&] { return !app.busy(); }, 5000), "node instance synchronization did not finish");
        require(app.wires().size() == 1 && outModel->get(2).value("count").toInt() == 1,
                "restoring the source type did not restore the same input relation");
        app.deleteNodes({relationTarget});
        require(QTest::qWaitFor([&] { return !app.busy(); }, 5000), "node instance synchronization did not finish");
        app.selectNode(recvNodeId);

        // Save project
        require(app.save(projectFile), "save project failed");

        // Reopen project
        app.requestOpen(projectFile);
        for (int i = 0; i < 50 && app.busy(); ++i)
        {
            QCoreApplication::processEvents();
            std::this_thread::sleep_for(std::chrono::milliseconds(10));
        }
        app.runOnce();
        for (int i = 0; i < 100 && app.busy(); ++i)
        {
            QCoreApplication::processEvents();
            std::this_thread::sleep_for(std::chrono::milliseconds(10));
        }

        QString flow2Id;
        for (const auto &f : app.flows())
        {
            if (f.toMap().value("name").toString() == "flow_2")
            {
                flow2Id = f.toMap().value("id").toString();
                break;
            }
        }
        require(!flow2Id.isEmpty(), "flow_2 not found after reopen");
        app.selectFlow(flow2Id);

        QString reopenedRecvId;
        for (const auto &n : app.nodes())
        {
            const auto m = n.toMap();
            if (m.value("toolId").toString() == "test.custom.receiver")
            {
                reopenedRecvId = m.value("id").toString();
                break;
            }
        }
        require(!reopenedRecvId.isEmpty(), "receiver node not found in flow_2 after reopen");
        app.selectNode(reopenedRecvId);
        require(outModel->get(2).value("preview").toString() == "10", "reopened receiver output mismatch");
        std::cout << "[Evidence T-05] Save/reopen numerical equality confirmed.\n";

        // 4. Change receiver declaration only (metadata-only identity change)
        std::cout << "[Evidence T-05] Changing receiver declaration identity and display name...\n";
        QFile::remove(dirPath + "/tools-package/tools/builtin/flow/recv/receiver.json");
        QFile renamedJson(dirPath + "/tools-package/tools/builtin/flow/recv/renamed.json");
        if (renamedJson.open(QIODevice::WriteOnly))
        {
            renamedJson.write(
                R"json({
  "schema_version": 2,
  "tool_id": "test.custom.renamed_recv",
  "display_name": "改名接收器",
  "version": "1.0.0",
  "kind": "host",
  "init_params": [
    {"id": "source_flow_id", "name": "源流程", "type": "String", "default": ""},
    {"id": "source_node_seq", "name": "源节点", "type": "String", "default": ""},
    {"id": "source_port_id", "name": "源端口", "type": "String", "default": ""},
    {"id": "timeout_ms", "name": "超时时间(ms)", "type": "Int", "default": 500}
  ],
  "inputs": [],
  "outputs": [
    {"id": "value", "name": "接收值", "type_from": {"flow": "source_flow_id", "node": "source_node_seq", "port": "source_port_id"}},
    {"id": "source_status_code", "name": "源状态码", "type": "Int"},
    {"id": "source_status_message", "name": "源状态消息", "type": "String"}
  ],
  "entry": {"binding": "visionrt.receiver"}
})json");
            renamedJson.close();
        }

        // Update module_index.json with renamed tool
        if (indexFile.open(QIODevice::WriteOnly))
        {
            indexFile.write(
                R"json({
  "schema_version": 1,
  "variant": "cpu",
  "modules": [
    {"id": "test_module", "library": "bin/test_plugin_module.dll"}
  ],
  "tools": [
    {"tool_id": "test.plugin.counter", "module": "test_module", "definition": "tools/builtin/pkg/counter/counter.json"},
    {"tool_id": "test.plugin.function", "module": "test_module", "definition": "tools/builtin/pkg/function/function.json"},
    {"tool_id": "test.user.pythonA", "definition": "tools/custom/user/py/user_node_a.json"},
    {"tool_id": "test.custom.renamed_recv", "definition": "tools/builtin/flow/recv/renamed.json"}
  ],
  "runtime_files": []
})json");
            indexFile.close();
        }

        app.rescanTools();
        for (int i = 0; i < 50 && app.busy(); ++i)
        {
            QCoreApplication::processEvents();
            std::this_thread::sleep_for(std::chrono::milliseconds(10));
        }

        const auto defsAfterRename = app.toolDefinitions();
        bool       foundOld = false, foundRenamed = false;
        for (const auto &item : defsAfterRename)
        {
            const auto map = item.toMap();
            const auto id  = map.value("id").toString();
            if (id == "test.custom.receiver")
                foundOld = true;
            if (id == "test.custom.renamed_recv")
            {
                foundRenamed = true;
                require(map.value("available").toBool(), "renamed receiver tool must be available");
                require(map.value("displayName").toString() == QStringLiteral("改名接收器"),
                        "renamed receiver displayName mismatch");
            }
        }
        require(!foundOld, "old receiver tool ID still present in definitions");
        require(foundRenamed, "renamed receiver tool ID not found in definitions");

        // Old node in flow 2 must be marked defMissing
        app.selectFlow(flow2Id);
        bool nodeDefMissing = false;
        for (const auto &n : app.nodes())
        {
            const auto m = n.toMap();
            if (m.value("id").toString() == reopenedRecvId || m.value("id").toString() == recvNodeId)
            {
                nodeDefMissing = m.value("defMissing").toBool();
            }
        }
        require(nodeDefMissing, "node with obsolete tool ID must be marked defMissing");
        std::cout << "[Evidence T-05] Metadata-only receiver modification verified: recognized dynamically without "
                     "host changes.\n";

        // 5. Package removal
        std::cout << "[Evidence T-05] Removing package and testing empty catalog and defMissing diagnosis...\n";
        QDir(dirPath + "/tools-package").removeRecursively();
        app.rescanTools();
        for (int i = 0; i < 50 && app.busy(); ++i)
        {
            QCoreApplication::processEvents();
            std::this_thread::sleep_for(std::chrono::milliseconds(10));
        }
        require(app.toolDefinitions().isEmpty(), "catalog must be empty after package removal");
        for (const auto &n : app.nodes())
        {
            require(n.toMap().value("defMissing").toBool(),
                    "all nodes must be marked defMissing after package removal");
        }
        std::cout
            << "[Evidence T-05] Package removal verified: catalog is empty and project nodes marked defMissing.\n";

        // 6. Broken package diagnosis
        QDir(dirPath).mkpath("tools-package");
        QFile brokenIndex(dirPath + "/tools-package/module_index.json");
        if (brokenIndex.open(QIODevice::WriteOnly))
        {
            brokenIndex.write("{\"invalid\": [");
            brokenIndex.close();
        }
        app.rescanTools();
        for (int i = 0; i < 50 && app.busy(); ++i)
        {
            QCoreApplication::processEvents();
            std::this_thread::sleep_for(std::chrono::milliseconds(10));
        }
        require(app.toolDefinitions().isEmpty(), "broken package must result in empty tool catalog");
        bool hasBrokenDiag = false;
        for (const auto &d : app.diagnostics())
        {
            if (d.contains("tools-package") || d.contains("module_index.json"))
            {
                hasBrokenDiag = true;
                break;
            }
        }
        require(hasBrokenDiag, "diagnostic missing error report for broken package");
        std::cout << "[Evidence T-05] Broken package diagnosis verified.\n";
    }
    std::cout << "[PACKAGE-DRIVEN CATALOG & REFRESH CONTRACT PASSED]\n";
}

void shortWindowContract()
{
    std::cout << "\n[SHORT WINDOW CONTRACT START]\n";
    QTemporaryDir directory;
    const auto    dirPath = directory.path();
    AppController app(1, dirPath);
    for (int i = 0; i < 50 && app.busy(); ++i)
    {
        QCoreApplication::processEvents();
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }

    qmlRegisterType<visionrt::ImageViewport>("VisionRT.View", 1, 0, "ImageViewport");
    auto *theme = visionrt::Theme::instance();
    qmlRegisterSingletonInstance<visionrt::Theme>("VisionRT", 1, 0, "Theme", theme);

    QQmlApplicationEngine engine;
    const auto            appDir = QCoreApplication::applicationDirPath();
    engine.addImportPath(appDir + "/qml");
    engine.addImportPath(appDir + "/../qml");
#ifdef VISIONRT_SOURCE_DIR
    const auto sourceDir = QStringLiteral(VISIONRT_SOURCE_DIR);
    engine.addImportPath(sourceDir + "/src/ui/qml");
    engine.addImportPath(sourceDir + "/3rdparty/QuickUI/quickui");
    const auto qmlPath = sourceDir + "/src/ui/qml/Main.qml";
#else
    const auto qmlPath = appDir + "/../src/ui/qml/Main.qml";
#endif

    engine.rootContext()->setContextProperty("app", &app);
    engine.rootContext()->setContextProperty("Theme", theme);
    engine.rootContext()->setContextProperty("demoMenu", false);
    engine.rootContext()->setContextProperty("demoSelect", false);

    if (QFile::exists(qmlPath))
    {
        engine.load(QUrl::fromLocalFile(qmlPath));
    }
    else
    {
        engine.loadFromModule("VisionRT", "Main");
    }

    const auto roots = engine.rootObjects();
    require(!roots.isEmpty(), "Main.qml failed to instantiate in shortWindowContract");
    auto *window = qobject_cast<QQuickWindow *>(roots.first());
    require(window != nullptr, "Main.qml root is not QQuickWindow");

    window->setFlags(Qt::FramelessWindowHint | Qt::Window);
    window->resize(800, 480);
    window->show();
    for (int i = 0; i < 20; ++i) QCoreApplication::processEvents();

    auto *leftHost    = window->findChild<QQuickItem *>("leftHost");
    auto *leftContent = window->findChild<QQuickItem *>("leftContent");
    require(leftHost != nullptr && leftContent != nullptr, "missing drawer items in short window");

    // Drawer toggle in short window
    QMetaObject::invokeMethod(window, "toggleLeft");
    for (int i = 0; i < 15; ++i) QCoreApplication::processEvents();
    require(window->property("leftExpanded").toBool() == false, "short window drawer collapse failed");

    QMetaObject::invokeMethod(window, "toggleLeft");
    for (int i = 0; i < 15; ++i) QCoreApplication::processEvents();
    require(window->property("leftExpanded").toBool() == true, "short window drawer expand failed");

    QImage frame = window->grabWindow();
    require(!frame.isNull(), "short window grabWindow failed");

    const std::filesystem::path evidenceDir = "build/evidence";
    std::filesystem::create_directories(evidenceDir);
    const auto screenshotPath = evidenceDir / "workbench_short_window.png";
    const bool saved          = frame.save(QString::fromStdString(screenshotPath.string()));
    require(saved, "Failed to save short window screenshot");
    require(std::filesystem::exists(screenshotPath) && std::filesystem::file_size(screenshotPath) > 0,
            "Saved short window screenshot file does not exist or is empty");

    std::cout << "[Evidence HS5-03] Short window contract verified at (" << window->width() << "x" << window->height()
              << "), screenshot=" << screenshotPath.string() << "\n";
    std::cout << "[SHORT WINDOW CONTRACT PASSED]\n";
}
} // namespace

int main(int argc, char **argv)
{
    if (argc >= 2
        && (std::string(argv[1]) == "--roi-child" || std::string(argv[1]) == "--output-child"
            || std::string(argv[1]) == "--parameters-child" || std::string(argv[1]) == "--paths-child"
            || std::string(argv[1]) == "--canvas-child" || std::string(argv[1]) == "--startup-child"
            || std::string(argv[1]) == "--catalog-child" || std::string(argv[1]) == "--cancellation-child"
            || std::string(argv[1]) == "--popups-child" || std::string(argv[1]) == "--output-reuse-child"))
    {
        QGuiApplication qt(argc, argv);
        try
        {
            if (std::string(argv[1]) == "--roi-child")
                roiParameterContract();
            else if (std::string(argv[1]) == "--output-child")
                outputContract();
            else if (std::string(argv[1]) == "--parameters-child")
                parameterEditorsContract();
            else if (std::string(argv[1]) == "--canvas-child")
                flowCanvasContract();
            else if (std::string(argv[1]) == "--startup-child")
                startupProjectContract();
            else if (std::string(argv[1]) == "--cancellation-child")
                cancellationWindowContract();
            else if (std::string(argv[1]) == "--popups-child")
                popupSessionsContract();
            else if (std::string(argv[1]) == "--output-reuse-child")
                outputCellReuseContract();
            else if (std::string(argv[1]) == "--catalog-child")
                packageDrivenCatalogAndRefreshContract();
            else
                pathEditorsContract();
            return 0;
        }
        catch (const std::exception &error)
        {
            std::cerr << "[Workbench Focused Error] " << error.what() << '\n';
            return 1;
        }
    }
    if (argc >= 2 && std::string(argv[1]) == "--short-window-child")
    {
        QGuiApplication qt(argc, argv);
        try
        {
            shortWindowContract();
            return 0;
        }
        catch (const std::exception &error)
        {
            std::cerr << "[Short Window Error] " << error.what() << '\n';
            return 1;
        }
    }
    if (argc >= 3 && std::string(argv[1]) == "--dpi-child")
    {
        const double scale = std::stod(argv[2]);
        qputenv("QT_SCALE_FACTOR", QByteArray::number(scale, 'f', 1));
        QGuiApplication qt(argc, argv);
        try
        {
            mainQmlContract(scale);
            return 0;
        }
        catch (const std::exception &error)
        {
            std::cerr << "[DPI Child Error for scale=" << scale << "] " << error.what() << '\n';
            return 1;
        }
    }

    QGuiApplication qt(argc, argv);
    try
    {
        treeContract();
        require(AppController::staticMetaObject.indexOfMethod("saveToolMetadata(QVariantMap)") < 0,
                "installed tool metadata must not expose a second authoring entry");
        outputContract();
        pendingContract();
        durationTextContract();
        nodeDiagnosticsContract();
        packageDrivenCatalogAndRefreshContract();
        parameterEditorsContract();
        pathEditorsContract();

        const QString appPath = QCoreApplication::applicationFilePath();

        {
            QProcess roiProc;
            roiProc.start(appPath, {"--roi-child"});
            const bool finished = roiProc.waitForFinished(30000);
            if (!finished)
            {
                roiProc.kill();
                roiProc.waitForFinished();
            }
            std::cout << roiProc.readAllStandardOutput().toStdString();
            std::cerr << roiProc.readAllStandardError().toStdString();
            require(finished && roiProc.exitStatus() == QProcess::NormalExit && roiProc.exitCode() == 0,
                    "ROI child process failed or timed out");
        }

        // 1. Run short window contract in child process
        {
            QProcess shortProc;
            shortProc.start(appPath, {"--short-window-child"});
            const bool shortFinished = shortProc.waitForFinished(45000);
            require(shortFinished, "Short window child process timed out");
            std::cout << shortProc.readAllStandardOutput().toStdString();
            if (shortProc.exitCode() != 0)
            {
                std::cerr << shortProc.readAllStandardError().toStdString();
                throw std::runtime_error("Short window child process failed");
            }
            const auto shortScreenshot = std::filesystem::path("build/evidence/workbench_short_window.png");
            require(std::filesystem::exists(shortScreenshot) && std::filesystem::file_size(shortScreenshot) > 0,
                    "Short window screenshot missing");
        }

        // 2. Run 3 DPI scale child processes (1.0, 1.5, 2.0)
        const double scales[] = {1.0, 1.5, 2.0};
        for (double scale : scales)
        {
            QProcess proc;
            proc.start(appPath, {"--dpi-child", QString::number(scale, 'f', 1)});
            const bool finished = proc.waitForFinished(45000);
            require(finished, "DPI child process timed out for scale " + std::to_string(scale));
            std::cout << proc.readAllStandardOutput().toStdString();
            if (proc.exitCode() != 0)
            {
                std::cerr << proc.readAllStandardError().toStdString();
                throw std::runtime_error("DPI child process failed for scale " + std::to_string(scale));
            }
            char scaleBuf[16];
            std::snprintf(scaleBuf, sizeof(scaleBuf), "%.1f", scale);
            const auto screenshot
                = std::filesystem::path("build/evidence") / ("workbench_dpi_" + std::string(scaleBuf) + ".png");
            require(std::filesystem::exists(screenshot) && std::filesystem::file_size(screenshot) > 0,
                    "Screenshot missing for scale " + std::string(scaleBuf));
        }

        std::cout << "workbench contract passed\n";
    }
    catch (const std::exception &error)
    {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
