#include "ui/UiState.h"
#include "ui/AppController.h"
#include "TestPythonConfiguration.h"
#include "ui/DurationText.h"
#include "ui/ImageViewport.h"
#include "ui/OutputModel.h"
#include "ui/UiTypes.h"
#include "ui/ToolTreeModel.h"
#include "ui/LogHighlighter.h"
#include "runtime/Snapshot.h"
#include "core/ProjectArchive.h"
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
#include <QTextBlock>
#include <QTextDocument>
#include <QTextLayout>
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
    tool.nameEn          = "custom.precise";
    tool.nameZh = "精确整数";
    tool.version     = "1.0";
    tool.kind        = ToolKind::PythonFunction;
    tool.entry       = PythonEntry{"fixture", "precise.py", "execute"};
    tool.categories  = {"数据", "整数"};
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
    require(model.updateToolStatus("custom.precise", true, "可用"), "tool status did not change");
    model.setFilter("不存在");
    require(model.updateToolStatus("custom.precise", false, "实例失败"), "a hidden tool lost its status update");
    model.setFilter("");
    leaf = model.index(0, 0, model.index(0, 0, model.index(0, 0)));
    require(!model.data(leaf, ToolTreeModel::AvailableRole).toBool()
                && model.data(leaf, ToolTreeModel::StatusRole).toString() == "实例失败",
            "rebuilding the tree restored a stale availability");

    tool.nameEn = "custom.znested";
    tool.categories.push_back("精确整数");
    registry.addDefinition(tool);
    model.setRegistry(registry);
    group = model.index(0, 0, model.index(0, 0));
    require(model.rowCount(group) == 2, "a tool and an equally named directory merged");
    for (int row = 0; row < model.rowCount(group); ++row)
    {
        const auto item = model.index(row, 0, group);
        require(model.data(item, ToolTreeModel::LeafRole).toBool() == (model.rowCount(item) == 0),
                "a tool leaf became a directory");
    }
}

void outputContract()
{
    require(OutputModel::fullText({Missing{}, {Type::Int}}) == "null", "Missing must copy as JSON null");
    const auto quoted = OutputModel::fullText({std::string("中文 \"引号\"\n路径"), {Type::String}});
    require(std::get<std::string>(ValueJson::parse(quoted, {Type::String})) == "中文 \"引号\"\n路径",
            "output text must escape strings as JSON");
    require(OutputModel::fullText({std::numeric_limits<double>::infinity(), {Type::Float}}).contains("non_finite"),
            "nonfinite output must have an explicit JSON description");
    require(typeText({Type::Rectangle}) == QStringLiteral("Rectangle"), "scalar rectangle type must be displayed safely");
    const auto                  rectangle = Rectangle::fromEncoded(ROI{{{2, 1.25, -2.5, 3.75, 4.5, 17.5}}});
    const OutputModel::Captured rectangleCapture{rectangle, {Type::Rectangle}};
    require(OutputModel::fullText(rectangleCapture).contains("1.25"),
            "scalar rectangle output must retain floating geometry in full text");
    ToolDefinition tool;
    PortDefinition port;
    port.id     = 2;
    port.nameEn = "value";
    port.nameZh = "值";
    port.type   = {Type::String};
    tool.outputs.push_back(port);
    auto snapshot    = std::make_shared<NodeSnapshot>();
    snapshot->node   = 18446744073709551614ULL;
    snapshot->toolId = "test.output";
    snapshot->phase  = NodePhase::Convert;
    snapshot->status = {-2, "输出合同错误", "third-party \"detail\"\n完整详情"};
    snapshot->toolResultPublished = true;
    snapshot->outputs.emplace(2, std::string(std::string(10000, 'x')));
    OutputModel model;
    model.setResult(&tool, snapshot,
                    {
                        {2, 2}
    });
    require(model.get(0).value("portId").toUInt() == 0, "status code is not first");
    require(model.get(1).value("portId").toUInt() == 1, "status message is not second");
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
    require(model.get(2).value("preview").toString() == QString(10000, 'x'), "display truncated string data");
    require(model.get(2).value("tooltip").toString() == QString(10000, 'x'), "tooltip truncated string data");
    require(model.get(2).value("count").toInt() == 2, "relationship count is not input binding count");
    const auto frozen = model.capture(2);
    model.clear();
    require(frozen && OutputModel::fullText(*frozen).size() >= 10000,
            "captured output follows current selection or truncates");
    tool.outputs.front().typeFrom = PortTypeReference{{PortStage::Init, 1},
                                                      {PortStage::Init, 2},
                                                      {PortStage::Init, 3}};
    model.setResult(&tool, {}, {});
    require(model.get(2).value("type").toString() == QStringLiteral("未绑定"), "unbound output pretends to have a concrete type");
    require(!model.capture(2), "unbound output can be captured as a typed value");
    const TypeSpec resolved{Type::Array, DType::Float32, 2};
    const TypeMap outputs{{2, resolved}};
    model.setResult(&tool, {}, {}, nullptr, &outputs);
    require(model.get(2).value("type").toString() == typeText(resolved), "resolved dtype/rank/layout not displayed");
    const auto lifecycle = std::make_shared<const NodeDiagnostic>(NodeDiagnostic{
        1,
        "test.output",
        NodePhase::Release,
        {-4, "释放失败", "uninitialized release detail"}
    });
    model.setResult(&tool, {}, {}, nullptr, &outputs, lifecycle, *lifecycle);
    require(std::get<std::int64_t>(model.capture(0)->value) == -4, "release failure must appear in overall status");
    const auto releaseJson = QJsonDocument::fromJson(OutputModel::diagnosticText(*model.capture(1)).toUtf8()).object();
    require(releaseJson.value("phase") == "release" && releaseJson.value("detail") == "uninitialized release detail",
            "release diagnostics without execution must remain independently exportable");

    ToolDefinition typed;
    typed.nameEn          = "test.typed";
    typed.nameZh = "类型展示";
    typed.version     = "1";
    typed.outputs = {
        {2, "零", {Type::Int}},
        {3, "标志", {Type::Bool}},
        {4, "列表", {Type::Array, DType::Int64, 1}},
        {5, "文本", {Type::String}}
    };
    auto typedSnapshot    = std::make_shared<NodeSnapshot>();
    typedSnapshot->node   = 2;
    typedSnapshot->toolId = typed.nameEn;
    typedSnapshot->status = {0, "运行成功"};
    typedSnapshot->outputs = {
        {2, std::int64_t{0}},
        {3, false},
        {4, Array::empty(DType::Int64, 1)},
        {5, std::string{}},
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
    // S20: an untouched project holds nothing to lose, so closing it asks nothing.
    {
        QTemporaryDir directory;
        AppController app(1, directory.path());
        int           confirmations = 0;
        QObject::connect(&app, &AppController::unsavedDecisionRequired, [&] { ++confirmations; });
        require(!app.dirty(), "a new project starts dirty");
        app.requestClose();
        require(confirmations == 0 && app.isClosing(), "an unedited project asked to save before closing");
    }

    QTemporaryDir directory;
    AppController app(1, directory.path());
    int           confirmations = 0, closes = 0;
    QObject::connect(&app, &AppController::unsavedDecisionRequired, [&] { ++confirmations; });
    QObject::connect(&app, &AppController::closeReady, [&] { ++closes; });
    // The first real edit arms the decision; the flow is the smallest change that survives the
    // empty-project rule.
    app.addFlow(QStringLiteral("流程2"));
    require(app.dirty(), "adding a flow left the project clean");
    app.requestClose();
    require(confirmations == 1 && closes == 0, "dirty close bypasses decision");
    app.resolveUnsaved(visionrt::UnsavedDecision::Cancel);
    require(closes == 0 && app.dirty(), "cancel changes project or closes");
    app.requestClose();
    app.resolveUnsaved(visionrt::UnsavedDecision::Save, directory.path() + "/absent/project.vrtapp");
    require(closes == 0 && app.dirty(), "failed save permits close");
    app.resolveUnsaved(visionrt::UnsavedDecision::Discard);
    require(QTest::qWaitFor([&] { return closes == 1; }, 2000), "discard does not finish pending close exactly once");
    require(app.isClosing(), "app must report isClosing() after discard close");
    app.runOnce();
    app.refreshPython();
    require(app.runState() == RuntimeState::Idle, "runOnce in Ready must not change runState");

    // S-D03: Resolving unsaved decision with valid "save" writes to disk and completes close
    {
        QTemporaryDir saveDir;
        AppController saveApp(1, saveDir.path());
        require(QTest::qWaitFor([&] { return saveApp.workspaceEnabled(); }, 5000), "save-on-close workspace was not ready");
        int saveCloses = 0;
        QObject::connect(&saveApp, &AppController::closeReady, [&] { ++saveCloses; });
        saveApp.addFlow(QStringLiteral("流程2"));
        saveApp.requestClose();
        require(saveApp.dirty(), "edited project should be dirty");
        const QString validSavePath = saveDir.path() + "/saved_on_close.vrtapp";
        saveApp.resolveUnsaved(visionrt::UnsavedDecision::Save, validSavePath);
        require(QTest::qWaitFor([&] { return saveCloses == 1; }, 2000), "resolveUnsaved save did not complete close");
        require(QFile::exists(validSavePath), "resolveUnsaved save did not write file");
        require(!saveApp.dirty(), "project should be clean after save on close");
        require(saveApp.isClosing(), "saveApp must report isClosing() after save on close");
        saveApp.runOnce();
        saveApp.refreshPython();
        require(saveApp.runState() == RuntimeState::Idle, "runOnce in Ready must not change runState");
    }
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
    require(durationText(0.0) == QStringLiteral("0.000 ms"), "measured zero must stay zero");
    require(durationText(0.0009) == QStringLiteral("0.000 ms"), "sub-microsecond measurement must read as zero");
    require(durationText(0.001) == QStringLiteral("0.001 ms"), "microsecond measurement lost precision");
    require(durationText(0.0199) == QStringLiteral("0.020 ms"), "millisecond rounding is wrong");
    require(durationText(1.2) == QStringLiteral("1.200 ms"), "three-decimal format is not fixed");
    require(durationText(1234.567) == QStringLiteral("1234.567 ms"), "three-decimal rounding is wrong");

    // A node that never ran has nothing to show: no dash, no fabricated zero.
    const auto absent = nodeTimingText(nullptr, false);
    require(absent.total.isEmpty() && absent.toolCall.isEmpty() && absent.preprocess.isEmpty()
                && absent.postprocess.isEmpty(),
            "absent execution snapshot fabricated a measurement");
    require(timingPhasesOf(absent).size() == 3 && timingPhasesOf(absent).at(1).value == "0.000 ms", "absent timing must display three zero rows");

    NodeSnapshot snapshot;
    snapshot.timing.totalMs          = 1234.567;
    snapshot.timing.adapterExecuteMs = 1.1;
    snapshot.timing.prepareMs        = 27;
    snapshot.timing.initMs           = 31;
    snapshot.timing.validateMs       = 43;
    // A gated node publishes a terminal snapshot without entering a stage; it must stay blank even
    // though the snapshot carries array-initialised numbers.
    const auto gated = nodeTimingText(&snapshot, false);
    require(gated.total.isEmpty() && timingPhasesOf(gated).size() == 3,
            "a node that never entered a stage displayed a measurement");

    snapshot.timing.prepareMeasured = true;
    const auto uncalled             = nodeTimingText(&snapshot, false);
    require(uncalled.total == QStringLiteral("1234.567 ms") && uncalled.toolCall.isEmpty(),
            "uncalled tool used an unmeasured adapter duration");
    require(uncalled.preprocess == QStringLiteral("27.000 ms") && uncalled.postprocess.isEmpty(),
            "framework stages were relabeled as algorithm stages");
    require(uncalled.prepare == QStringLiteral("27.000 ms") && uncalled.init.isEmpty()
                && uncalled.validate.isEmpty(),
            "unmeasured stages must stay blank");
    require(timingPhasesOf(uncalled).size() == 3 && timingPhasesOf(uncalled).at(1).value == "0.000 ms", "uncalled API must retain its zero timing row");

    snapshot.timing.toolCallMeasured = true;
    snapshot.timing.initMeasured     = true;
    snapshot.timing.validateMeasured = true;
    const auto called = nodeTimingText(&snapshot, false);
    require(called.total == QStringLiteral("1234.567 ms") && called.toolCall == QStringLiteral("1.100 ms"),
            "node total and adapter call measurements were conflated");
    require(called.prepare == QStringLiteral("27.000 ms") && called.init == QStringLiteral("31.000 ms")
                && called.validate == QStringLiteral("43.000 ms"),
            "measured framework stages did not display real values");
    // The hover card has exactly three rows: preparation and post-processing aggregate the measured
    // phases; the operator row is the tool call itself.
    require(called.preprocess == QStringLiteral("58.000 ms") && called.postprocess == QStringLiteral("43.000 ms"),
            "tooltip phases did not aggregate the measured preparation/post-processing");
    const auto phases = timingPhasesOf(called);
    require(phases.size() == 3 && phases.at(0).label == QStringLiteral("预处理")
                && phases.at(1).label == QStringLiteral("API") && phases.at(2).label == QStringLiteral("后处理"),
            "hover rows lost their labels or order");

    const std::pair<double, std::optional<QString>> measurements[] = {
        {0.0, QStringLiteral("0.000 ms")}, {0.0009, QStringLiteral("0.000 ms")},
        {0.001, QStringLiteral("0.001 ms")}, {0.0199, QStringLiteral("0.020 ms")},
        {1.2, QStringLiteral("1.200 ms")}, {-1.0, std::nullopt},
        {std::numeric_limits<double>::quiet_NaN(), std::nullopt},
        {std::numeric_limits<double>::infinity(), std::nullopt}
    };
    snapshot.timing.initMeasured     = false;
    snapshot.timing.validateMeasured = false;
    for (const auto &[value, expected] : measurements)
    {
        snapshot.timing.totalMs          = value;
        snapshot.timing.adapterExecuteMs = value;
        const auto measured              = nodeTimingText(&snapshot, false);
        require(measured.total == expected.value_or(QString()), "node total lost its measurement semantics");
        require(measured.toolCall == expected.value_or(QString()),
                "operator time lost its measured zero, small value or invalid metric semantics");
        require(measured.preprocess == QStringLiteral("27.000 ms") && measured.postprocess.isEmpty(),
                "unmeasured phases must stay unavailable instead of fabricating a zero");
    }

    // A Python operator reports the real function call as its own time: the GIL wait and input
    // mapping are preparation, the output conversion is post-processing, and the whole bridge is
    // exactly their sum, so no stage is counted twice.
    snapshot.timing.prepareMs            = 0;
    snapshot.timing.initMs               = 0;
    snapshot.timing.prepareMeasured      = true;
    snapshot.timing.validateMeasured     = true;
    snapshot.timing.validateMs           = 1.0;
    snapshot.timing.pythonBridge         = true;
    snapshot.timing.pythonInputMeasured  = true;
    snapshot.timing.pythonCallMeasured   = true;
    snapshot.timing.pythonOutputMeasured = true;
    snapshot.timing.waitGilMs            = 2.0;
    snapshot.timing.pythonInputMs        = 3.0;
    snapshot.timing.pythonCallMs         = 4.0;
    snapshot.timing.pythonOutputMs       = 3.0;
    snapshot.timing.adapterExecuteMs     = 12.0;
    const auto pythonCall = nodeTimingText(&snapshot, false);
    require(pythonCall.toolCall == QStringLiteral("4.000 ms"),
            "a Python operator must report its real call time instead of the whole adapter boundary");
    require(pythonCall.preprocess == QStringLiteral("5.000 ms"),
            "GIL wait and input mapping belong to preparation and must be counted once");
    require(pythonCall.postprocess == QStringLiteral("4.000 ms"),
            "output conversion belongs to post-processing and must be counted once");
    snapshot.timing.pythonCallMeasured = false;
    require(nodeTimingText(&snapshot, false).toolCall.isEmpty(),
            "a bridge that never reached the function call must not claim operator time");

    // A running node shows no timing at all: the previous round's numbers describe work that is no
    // longer running, and the state is carried by colour and the status dot.
    const NodeSnapshot *previousSnapshots[] = {nullptr, &snapshot};
    for (const auto *previous : previousSnapshots)
    {
        const auto running = nodeTimingText(previous, true);
        require(running.total.isEmpty() && running.toolCall.isEmpty() && running.preprocess.isEmpty()
                    && running.postprocess.isEmpty() && timingPhasesOf(running).size() == 3 && timingPhasesOf(running).at(1).value == "0.000 ms",
                "running node reused an earlier snapshot measurement");
    }
    std::cout << "[Evidence T-03] Duration text formatting covers missing, zero, tiny, regular and non-finite values.\n";
    std::cout << "[Evidence T-03] Controlled snapshots distinguish absent, gated, uncalled, measured and running values, and hover rows retain three stages.\n";
}

void nodeDiagnosticsContract()
{
    QTemporaryDir directory;
    QDir(directory.path()).mkpath("tools-package/tools/custom/test_category/test_diag");
    QFile pyFile(directory.path() + "/tools-package/python/custom/test_category/test_diag/test_diag.py");
    require(QDir().mkpath(QFileInfo(pyFile).absolutePath()), "cannot create Python fixture directory");
    if (pyFile.open(QIODevice::WriteOnly))
    {
        pyFile.write("def run(inputs):\n    return {'outputs': {2: 42, 0: 0, 1: ''}, 'detail': ''}\n");
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
  "name_en": "test.diag.python",
  "name_zh": "诊断测试",
  "version": "1.0.0",
  "implementation": {
    "kind": "python_function",
    "package": "test_category/test_diag",
    "file": "test_diag.py",
    "symbol": "run"
  },
  "inputs": [
    {
      "name_zh": "输入",
      "type": "int",
      "default": 1,
      "constraints": {
        "min": 0,
        "max": 100
      },
      "ui": {
        "step": 1
      },
      "id": 1
    }
  ],
  "outputs": [
    {
      "name_zh": "输出",
      "type": "int",
      "id": 2
    }
  ],
  "schema_version": 4
})json");
        jsonFile.close();
    }
    QFile failJsonFile(directory.path() + "/tools-package/tools/custom/test_category/test_diag/test_diag_fail.json");
    if (failJsonFile.open(QIODevice::WriteOnly))
    {
        failJsonFile.write(
            R"json({
  "name_en": "test.diag.fail",
  "name_zh": "诊断失败",
  "version": "1.0.0",
  "implementation": {
    "kind": "python_function",
    "package": "test_category/test_diag",
    "file": "test_diag_fail.py",
    "symbol": "run"
  },
  "outputs": [
    {
      "name_zh": "输出",
      "type": "int",
      "id": 2
    }
  ],
  "schema_version": 4
})json");
        failJsonFile.close();
    }
    QFile unfulfilledJson(directory.path() + "/tools-package/tools/custom/test_category/test_diag/unfulfilled.json");
    if (unfulfilledJson.open(QIODevice::WriteOnly))
    {
        unfulfilledJson.write(
            R"json({
  "name_en": "test.diag.unfulfilled",
  "name_zh": "未提供实现测试",
  "version": "1.0.0",
  "implementation": {
    "kind": "host",
    "binding": "nonexistent.binding"
  },
  "schema_version": 4
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
    const QString initialPath = directory.path() + "/initial.vrtapp";
    ProjectArchive::writeProgramJson(
        std::filesystem::path(initialPath.toStdWString()),
        R"json({
  "format_version": 9,
  "project": {
    "name": "诊断工程",
    "notes": "",
    "app_version": "0.2.0",
    "contracts_version": "0.1.0",
    "next_node_seq": "4",
    "next_flow_seq": "2"
  },
  "flows": [
    {
      "id": "flow_1",
      "name": "流程1",
      "run_selected": true,
      "nodes": [
        {
          "id": "1",
          "tool": { "id": "test.diag.python", "name": "诊断测试", "version": "1.0.0" },
          "alias": "",
          "params": {
            "inputs": [
              { "port": 1, "type": "int", "value": 1 }
            ]
          }
        },
        {
          "id": "2",
          "tool": { "id": "test.diag.unfulfilled", "name": "未提供实现测试", "version": "1.0.0" },
          "alias": ""
        },
        {
          "id": "3",
          "tool": { "id": "test.diag.fail", "name": "诊断失败", "version": "1.0.0" },
          "alias": ""
        }
      ],
      "layout": [
        { "node": "1", "col": 100, "row": 100 },
        { "node": "2", "col": 200, "row": 200 },
        { "node": "3", "col": 300, "row": 200 }
      ],
      "connections": []
    }
  ]
})json");
    app.requestOpen(initialPath);
    app.resolveUnsaved(visionrt::UnsavedDecision::Discard);
    require(QTest::qWaitFor([&] { return !app.busy() && app.nodes().size() == 3; }, 5000),
            "project with unfulfilled tool did not open");

    const auto nodes = app.nodes();
    QStringList toolIds;
    for (const auto &item : app.toolDefinitions()) toolIds << item.toMap().value("id").toString();
    require(nodes.size() == 3, "expected 3 nodes in nodes(); tools=" + toolIds.join(',').toStdString()
                                   + " diagnostic=" + app.diagnostic().toStdString());

    const auto map0 = nodes.at(0).toMap();
    require(map0.contains("state"), "node missing state");
    require(map0.value("state").toInt() == static_cast<int>(NodeState::Idle), "initial node state is not idle");
    require(map0.contains("defMissing"), "node missing defMissing");
    require(map0.value("defMissing").toBool() == false, "registered node marked defMissing");
    require(map0.contains("implUnavailable"), "node missing implUnavailable");
    require(map0.value("implUnavailable").toBool() == false, "available node marked implUnavailable");
    require(!map0.contains("timingDetail"), "obsolete timingDetail source must be removed");
    // The card has no measured total before execution; the tooltip retains three zero rows.
    const auto phaseValue = [](const QVariantMap &node, const QString &label) -> QString
    {
        for (const auto &item : node.value("timingPhases").toList())
        {
            const auto phase = item.toMap();
            if (phase.value("label").toString() == label)
                return phase.value("value").toString();
        }
        return {};
    };
    require(map0.value("timingText").toString().isEmpty(), "unrun node total must stay empty");
    require(map0.value("timingPhases").toList().size() == 3, "unrun node must retain three timing rows");

    const auto map1 = nodes.at(1).toMap();
    require(map1.value("defMissing").toBool() == false, "unfulfilled node marked defMissing");
    require(map1.value("implUnavailable").toBool() == true, "unfulfilled node not marked implUnavailable");

    app.runOnce();
    for (int i = 0; i < 100 && app.busy(); ++i)
    {
        QCoreApplication::processEvents();
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
    // 单流程工程：底部的“流程”和“全流程”必须是同一个执行区间，否则说明两处各自计时。
    {
        const auto timingRow = app.timingText();
        const auto flowAt    = timingRow.indexOf(QStringLiteral("："));
        const auto totalAt   = timingRow.indexOf(QStringLiteral("全流程："));
        const auto elapsedAt = timingRow.indexOf(QStringLiteral("已运行："));
        require(flowAt >= 0 && totalAt > flowAt && elapsedAt > totalAt,
                "timing text lost one of its three values: " + timingRow.toStdString());
        const auto number = [&](int from, int to)
        {
            return timingRow.mid(from, to - from).split(QLatin1Char(' ')).value(0).toDouble();
        };
        const double flowMs = number(flowAt + 1, timingRow.indexOf(QStringLiteral(" ms")));
        const double roundMs = number(totalAt + 4, timingRow.indexOf(QStringLiteral(" ms"), totalAt));
        require(std::abs(flowMs - roundMs) <= 0.002,
                "a single-flow run reported different flow and total times: " + timingRow.toStdString());
    }

    const auto executedNodes = app.nodes();
    require(!executedNodes.isEmpty(), "executed node empty");
    const auto executedMap = executedNodes.first().toMap();
    require(executedMap.value("state").toInt() == static_cast<int>(NodeState::Succeeded), "executed node is not succeeded");
    require(executedMap.value("timingText").toString().endsWith(QStringLiteral(" ms")),
            "executed node total timing is not measured");
    require(phaseValue(executedMap, QStringLiteral("API")).endsWith(QStringLiteral(" ms")),
            "executed node operator timing is not measured");
    // The executed node measured its preparation and result validation, so the two aggregate
    // rows must show real values instead of an empty row.
    require(phaseValue(executedMap, QStringLiteral("预处理")).endsWith(QStringLiteral(" ms")),
            "executed node lost its measured preprocess timing");
    require(phaseValue(executedMap, QStringLiteral("后处理")).endsWith(QStringLiteral(" ms")),
            "executed node lost its measured postprocess timing");

    const auto unavailableMap = executedNodes.at(1).toMap();
    require(unavailableMap.value("state").toInt() == static_cast<int>(NodeState::Failed), "unavailable implementation is not failed");
    require(phaseValue(unavailableMap, QStringLiteral("API")) == "0.000 ms",
            "implementation without a tool call must not report operator time");

    const auto failedMap = executedNodes.at(2).toMap();
    require(failedMap.value("state").toInt() == static_cast<int>(NodeState::Failed), "failing tool is not failed");
    require(failedMap.value("statusCode").toLongLong() != 0, "failing tool lost its status code");
    require(!failedMap.value("statusMessage").toString().isEmpty(), "failing tool lost its diagnostic message");
    require(failedMap.value("timingText").toString().endsWith(QStringLiteral(" ms")),
            "failing tool lost its measured total timing");

    const QString pythonId = map0.value("id").toString();
    app.selectNode(pythonId);
    require(app.setParameter(1, false, "2"), "valid integer parameter change was rejected");
    const auto staleMap = app.nodes().first().toMap();
    require(staleMap.value("state").toInt() == static_cast<int>(NodeState::Succeeded),
            "parameter change discarded the last successful display state");
    require(staleMap.value("timingText").toString().endsWith(QStringLiteral(" ms")),
            "parameter change discarded the previous total timing");
    require(phaseValue(staleMap, QStringLiteral("API")).endsWith(QStringLiteral(" ms")),
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
  "name_en": "test.legacy.func",
  "name_zh": "遗留函数",
  "version": "1.0.0",
  "implementation": {
    "kind": "cpp"
  },
  "schema_version": 4
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
  "name_en": "test.legacy.python",
  "name_zh": "遗留脚本",
  "version": "1.0.0",
  "implementation": {
    "kind": "python_function",
    "file": "legacy.py",
    "symbol": "run"
  },
  "schema_version": 4
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
  "name_en": "test.plugin.counter",
  "name_zh": "计数器插件",
  "version": "1.0.0",
  "implementation": {
    "kind": "cpp"
  },
  "init_params": [
    {
      "name_zh": "初始值",
      "type": "int",
      "default": 10,
      "id": 1
    }
  ],
  "outputs": [
    {
      "name_zh": "计数值",
      "type": "int",
      "id": 2
    }
  ],
  "schema_version": 4
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
  "name_en": "test.plugin.function",
  "name_zh": "函数插件",
  "version": "1.0.0",
  "implementation": {
    "kind": "cpp"
  },
  "inputs": [
    {
      "name_zh": "输入",
      "type": "int",
      "default": 5,
      "id": 1
    }
  ],
  "outputs": [
    {
      "name_zh": "输出",
      "type": "int",
      "id": 2
    }
  ],
  "schema_version": 4
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
            "    return {'outputs': {2: 111, 3: [], 0: 0, 1: 'ok'}, 'detail': ''}\n");
        pyScriptA.close();
    }
    QFile root3JsonA(dirPath + "/tools-package/tools/custom/user/py/user_node_a.json");
    if (root3JsonA.open(QIODevice::WriteOnly))
    {
        root3JsonA.write(
            R"json({
  "name_en": "test.user.pythonA",
  "name_zh": "用户脚本A",
  "version": "1.0.0",
  "implementation": {
    "kind": "python_function",
    "package": "user/py",
    "file": "user_node_a.py",
    "symbol": "run"
  },
  "outputs": [
    {
      "name_zh": "结果",
      "type": "int",
      "id": 2
    },
    {
      "name_zh": "图像",
      "type": "image",
      "display": {
        "visible": true
      },
      "id": 3
    }
  ],
  "schema_version": 4
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
  "name_en": "test.custom.receiver",
  "name_zh": "动态接收器",
  "version": "1.0.0",
  "implementation": {
    "kind": "host",
    "binding": "visionrt.receiver"
  },
  "init_params": [
    {
      "name_zh": "源流程",
      "type": "string",
      "id": 0
    },
    {
      "name_zh": "源节点",
      "type": "string",
      "id": 1
    },
    {
      "name_zh": "源端口",
      "type": "int",
      "id": 2
    },
    {
      "name_zh": "超时时间(ms)",
      "type": "int",
      "default": 500,
      "id": 3
    }
  ],
  "outputs": [
    {
      "name_zh": "接收值",
      "type_from": {
        "flow": {
          "stage": "init",
          "id": 0
        },
        "node": {
          "stage": "init",
          "id": 1
        },
        "port": {
          "stage": "init",
          "id": 2
        }
      },
      "id": 2
    },
    {
      "name_zh": "源状态码",
      "type": "int",
      "id": 3
    },
    {
      "name_zh": "源状态消息",
      "type": "string",
      "id": 4
    }
  ],
  "schema_version": 4
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
  "name_en": "test.unindexed.tool",
  "name_zh": "未索引工具",
  "version": "1.0.0",
  "implementation": {
    "kind": "cpp"
  },
  "schema_version": 4
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

    const QString projectFile = dirPath + "/test_project.vrtapp";
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
                require(map.value("nameZh").toString() == QStringLiteral("动态接收器"),
                        "receiver nameZh mismatch");
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
        auto choices = [&](PortId parameter, const QString &field)
        {
            QVariantList result;
            for (const auto &item : app.parameters())
            {
                const auto row = item.toMap();
                if (row.value("id").toUInt() != parameter)
                    continue;
                for (const auto &option : row.value("options").toList())
                    result.push_back(option.toMap().value(field));
            }
            return result;
        };
        require(choices(0, "label").contains(flow1Name), "source flow choices are missing");
        require(choices(0, "value").contains(flow1Id), "source flow identity is missing");
        app.setParameter(0, true, flow1Id);
        require(QTest::qWaitFor([&] { return !app.busy(); }, 5000), "node instance synchronization did not finish");
        require(!pythonNodeTitle.isEmpty(), "missing Python image source");
        app.setParameter(1, true, pythonNodeId);
        require(QTest::qWaitFor([&] { return !app.busy(); }, 5000), "node instance synchronization did not finish");
        app.setParameter(2, true, "3");
        require(QTest::qWaitFor([&] { return !app.busy(); }, 5000), "node instance synchronization did not finish");
        require(app.imageOutputs().size() == 1
                    && app.imageOutputs().first().toMap().value("id").toString() == "outputs:2",
                "source-derived image output is missing from the viewport selector");
        require(choices(1, "label").contains(funcNodeTitle), "source node choices are missing");
        app.setParameter(1, true, funcNodeId);
        require(QTest::qWaitFor([&] { return !app.busy(); }, 5000), "node instance synchronization did not finish");
        require(choices(2, "value").contains(QStringLiteral("2")), "source output choices are missing");
        app.setParameter(2, true, "2");
        require(QTest::qWaitFor([&] { return !app.busy(); }, 5000), "node instance synchronization did not finish");
        require(receiverOutputs->get(2).value("type").toString() == typeText({Type::Int}),
                "receiver output not resolved after source selection");
        app.setParameter(3, true, "1000");
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
        if (recvPreview != "10")
        {
            QStringList rows;
            for (int row = 0; row < outModel->rowCount(); ++row)
                rows << QStringLiteral("%1=%2").arg(outModel->get(row).value("portId").toUInt()).arg(
                    outModel->get(row).value("preview").toString());
            throw std::runtime_error("unexpected receiver output: " + recvPreview.toStdString()
                                     + " rows=" + rows.join(',').toStdString()
                                     + " diag=" + app.diagnostic().toStdString());
        }
        std::cout << "[Evidence T-05] Cross-flow receive of dynamic host_binding node verified, value="
                  << recvPreview.toStdString() << "\n";

        app.addNode("test.plugin.function", 200, 200);


        require(QTest::qWaitFor([&] { return !app.busy(); }, 5000), "node instance synchronization did not finish");
        const auto relationTarget = app.selectedNode();
        require(app.bindInput(app.portContext(1), recvNodeId, 2), "cannot bind source-derived receiver output");
        require(app.wires().size() == 1, "valid receiver relation is missing");
        const auto relationPorts = app.wires().front().toMap().value("ports").toList();
        require(relationPorts.size() == 1 && relationPorts.front().toMap().value("output").toString() == "2"
                    && relationPorts.front().toMap().value("input").toString() == "1",
                "grouped wire discarded the source and target port identity");
        app.selectNode(recvNodeId);
        app.setParameter(1, true, pythonNodeId);
        require(QTest::qWaitFor([&] { return !app.busy(); }, 5000), "node instance synchronization did not finish");
        app.setParameter(2, true, "3");
        require(QTest::qWaitFor([&] { return !app.busy(); }, 5000), "node instance synchronization did not finish");
        require(app.wires().isEmpty(), "incompatible or unresolved receiver binding is still drawn as an active wire");
        require(outModel->get(2).value("count").toInt() == 0,
                "invalid receiver relation is still counted as an active output reference");
        app.setParameter(1, true, funcNodeId);
        require(QTest::qWaitFor([&] { return !app.busy(); }, 5000), "node instance synchronization did not finish");
        app.setParameter(2, true, "2");
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
  "name_en": "test.custom.renamed_recv",
  "name_zh": "改名接收器",
  "version": "1.0.0",
  "implementation": {
    "kind": "host",
    "binding": "visionrt.receiver"
  },
  "init_params": [
    {
      "name_zh": "源流程",
      "type": "string",
      "id": 0
    },
    {
      "name_zh": "源节点",
      "type": "string",
      "id": 1
    },
    {
      "name_zh": "源端口",
      "type": "int",
      "id": 2
    },
    {
      "name_zh": "超时时间(ms)",
      "type": "int",
      "default": 500,
      "id": 3
    }
  ],
  "outputs": [
    {
      "name_zh": "接收值",
      "type_from": {
        "flow": {
          "stage": "init",
          "id": 0
        },
        "node": {
          "stage": "init",
          "id": 1
        },
        "port": {
          "stage": "init",
          "id": 2
        }
      },
      "id": 2
    },
    {
      "name_zh": "源状态码",
      "type": "int",
      "id": 3
    },
    {
      "name_zh": "源状态消息",
      "type": "string",
      "id": 4
    }
  ],
  "schema_version": 4
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
                require(map.value("nameZh").toString() == QStringLiteral("改名接收器"),
                        "renamed receiver nameZh mismatch");
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

void coreContracts()
{
    treeContract();
    pendingContract();
    durationTextContract();
    nodeDiagnosticsContract();
}
} // namespace

int main(int argc, char **argv)
{
    visionrt::registerUiTypes();
    // R50: Python runs only on an interpreter someone selected. This runner states the build
    // interpreter once, before any controller exists, so every scenario sees the same catalog the
    // application shows after the user saves that choice.
    testsupport::useBuildInterpreter();
    if (argc >= 2
        && (std::string(argv[1]) == "--roi-child" || std::string(argv[1]) == "--output-child"
            || std::string(argv[1]) == "--parameters-child" || std::string(argv[1]) == "--paths-child"
            || std::string(argv[1]) == "--canvas-child" || std::string(argv[1]) == "--startup-child"
            || std::string(argv[1]) == "--catalog-child" || std::string(argv[1]) == "--cancellation-child"
            || std::string(argv[1]) == "--popups-child" || std::string(argv[1]) == "--output-reuse-child"
            || std::string(argv[1]) == "--core-child"))
    {
        QGuiApplication qt(argc, argv);
        try
        {
            if (std::string(argv[1]) == "--core-child")
            {
                coreContracts();
                require(AppController::staticMetaObject.indexOfMethod("saveToolMetadata(QVariantMap)") < 0,
                        "installed tool metadata must not expose a second authoring entry");
            }
            else if (std::string(argv[1]) == "--roi-child")
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

    QGuiApplication qt(argc, argv);
    try
    {
        coreContracts();
        require(AppController::staticMetaObject.indexOfMethod("saveToolMetadata(QVariantMap)") < 0,
                "installed tool metadata must not expose a second authoring entry");
        outputContract();
        packageDrivenCatalogAndRefreshContract();
        parameterEditorsContract();
        pathEditorsContract();

        const QString appPath = QCoreApplication::applicationFilePath();

        // ROI geometry drives real window focus and must stay in an isolated child process.
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

        std::cout << "workbench contract passed\n";
    }
    catch (const std::exception &error)
    {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
