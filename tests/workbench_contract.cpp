#include "ui/AppController.h"
#include "ui/DurationText.h"
#include "ui/ImageViewport.h"
#include "ui/OutputModel.h"
#include "ui/Theme.h"
#include "ui/ToolTreeModel.h"
#include "runtime/Snapshot.h"

#include <QGuiApplication>
#include <QImage>
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
    tool.script      = "precise.py";
    tool.symbol      = "execute";
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
    require(typeText({Type::Rect}) == QStringLiteral("Rect"), "scalar rectangle type must be displayed safely");
    const auto rectangle = Roi::rectangle(1.25F, -2.5F, 3.75F, 4.5F, 17.5F);
    const OutputModel::Captured rectangleCapture{rectangle, {Type::Rect}};
    require(OutputModel::fullText(rectangleCapture).contains("1.25"),
            "scalar rectangle output must retain floating geometry in full text");
    ToolDefinition tool;
    PortDefinition port;
    port.id   = "value";
    port.name = "值";
    port.type = {Type::String};
    tool.outputs.push_back(port);
    auto snapshot    = std::make_shared<NodeSnapshot>();
    snapshot->status = {-2, "conversion failed"};
    snapshot->outputs.emplace("value", Utf8(std::string(10000, 'x')));
    OutputModel model;
    model.setResult(&tool, snapshot,
                    {
                        {"value", 2}
    });
    require(model.get(0).value("portId").toString() == "__status_code", "status code is not first");
    require(model.get(1).value("portId").toString() == "__status_message", "status message is not second");
    require(model.get(0).value("preview").toString() == "-2", "status not taken from completed snapshot");
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
    const TypeSpec resolved{Type::NumericArray, DType::Float32, 2, Layout::BatchRagged};
    const TypeMap outputs{{"value", resolved}};
    model.setResult(&tool, {}, {}, &outputs);
    require(model.get(2).value("type").toString() == typeText(resolved), "resolved dtype/rank/layout not displayed");
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
    app.resolveUnsaved("save", directory.path() + "/absent/project.vrt");
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
    snapshot.timing.toolCallMeasured = true;
    const auto called = nodeTimingText(&snapshot, false);
    require(called.total == QStringLiteral("1234.57 ms") && called.toolCall == QStringLiteral("1.1 ms"),
            "node total and adapter call measurements were conflated");
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
    QDir(directory.path()).mkpath("tools-package/assets/tools/custom/test_category/test_diag");
    QFile pyFile(directory.path() + "/tools-package/assets/tools/custom/test_category/test_diag/test_diag.py");
    if (pyFile.open(QIODevice::WriteOnly))
    {
        pyFile.write("def run(inputs):\n    return {'__status_code': 0, '__status_message': '', 'value': 42}\n");
        pyFile.close();
    }
    QFile failPyFile(directory.path()
                     + "/tools-package/assets/tools/custom/test_category/test_diag/test_diag_fail.py");
    if (failPyFile.open(QIODevice::WriteOnly))
    {
        failPyFile.write("def run(inputs):\n    raise RuntimeError('diag boom')\n");
        failPyFile.close();
    }
    QFile yamlFile(directory.path() + "/tools-package/assets/tools/custom/test_category/test_diag/test_diag.yaml");
    if (yamlFile.open(QIODevice::WriteOnly))
    {
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
            "inputs:\n"
            "- id: x\n"
            "  name: 输入\n"
            "  type: Int\n"
            "  default: 1\n"
            "  min: 0\n"
            "  max: 100\n"
            "  step: 1\n"
            "outputs:\n"
            "- id: value\n"
            "  name: 输出\n"
            "  type: Int\n");
        yamlFile.close();
    }
    QFile failYamlFile(directory.path()
                       + "/tools-package/assets/tools/custom/test_category/test_diag/test_diag_fail.yaml");
    if (failYamlFile.open(QIODevice::WriteOnly))
    {
        failYamlFile.write(
            "schema_version: 1\n"
            "tool_id: test.diag.fail\n"
            "display_name: 诊断失败\n"
            "version: 1.0.0\n"
            "kind: python_function\n"
            "entry:\n"
            "  file: test_diag_fail.py\n"
            "  symbol: run\n"
            "init_params: []\n"
            "inputs: []\n"
            "outputs:\n"
            "- id: value\n"
            "  name: 输出\n"
            "  type: Int\n");
        failYamlFile.close();
    }
    QFile unfulfilledYaml(directory.path()
                          + "/tools-package/assets/tools/custom/test_category/test_diag/unfulfilled.yaml");
    if (unfulfilledYaml.open(QIODevice::WriteOnly))
    {
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
            "outputs: []\n");
        unfulfilledYaml.close();
    }
    QFile indexFile(directory.path() + "/tools-package/module_index.yaml");
    if (indexFile.open(QIODevice::WriteOnly))
    {
        indexFile.write(
            "schema_version: 2\n"
            "variant: cpu\n"
            "tools:\n"
            "  test.diag.python:\n"
            "    definition: assets/tools/custom/test_category/test_diag/test_diag.yaml\n"
            "  test.diag.fail:\n"
            "    definition: assets/tools/custom/test_category/test_diag/test_diag_fail.yaml\n"
            "  test.diag.unfulfilled:\n"
            "    host_binding: nonexistent.binding\n"
            "    definition: assets/tools/custom/test_category/test_diag/unfulfilled.yaml\n");
        indexFile.close();
    }

    AppController app(1, directory.path());
    for (int i = 0; i < 50 && app.busy(); ++i)
    {
        QCoreApplication::processEvents();
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
    app.addNode("test.diag.python", 100, 100);
    app.addNode("test.diag.unfulfilled", 200, 200);
    app.addNode("test.diag.fail", 300, 200);

    const auto nodes = app.nodes();
    QStringList toolIds;
    for (const auto &item : app.toolDefinitions())
        toolIds << item.toMap().value("id").toString();
    require(nodes.size() == 3, "expected 3 nodes in nodes(); tools=" + toolIds.join(',').toStdString()
                                   + " diagnostic=" + app.diagnostic().toStdString());

    const auto map0 = nodes.at(0).toMap();
    require(map0.contains("state"), "node missing state");
    require(map0.value("state").toString() == "gray", "initial node state is not gray");
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
    require(executedMap.value("state").toString() == "green", "executed node is not green");
    require(executedMap.value("timingText").toString().endsWith(QStringLiteral(" ms")),
            "executed node total timing is not measured");
    require(executedMap.value("timingOperatorText").toString().endsWith(QStringLiteral(" ms")),
            "executed node operator timing is not measured");
    require(executedMap.value("timingPreprocessText").toString() == QStringLiteral("—"),
            "executed node fabricates preprocess timing");
    require(executedMap.value("timingPostprocessText").toString() == QStringLiteral("—"),
            "executed node fabricates postprocess timing");

    const auto unavailableMap = executedNodes.at(1).toMap();
    require(unavailableMap.value("state").toString() == "red", "unavailable implementation is not red");
    require(unavailableMap.value("timingOperatorText").toString() == QStringLiteral("—"),
            "implementation without a tool call must not report operator time");

    const auto failedMap = executedNodes.at(2).toMap();
    require(failedMap.value("state").toString() == "red", "failing tool is not red");
    require(failedMap.value("statusCode").toLongLong() != 0, "failing tool lost its status code");
    require(!failedMap.value("statusMessage").toString().isEmpty(), "failing tool lost its diagnostic message");
    require(failedMap.value("timingText").toString().endsWith(QStringLiteral(" ms")),
            "failing tool lost its measured total timing");

    const QString pythonId = map0.value("id").toString();
    app.selectNode(pythonId);
    require(app.setParameter("x", false, "2"), "valid integer parameter change was rejected");
    const auto staleMap = app.nodes().first().toMap();
    require(staleMap.value("state").toString() == "gray", "stale snapshot still colors the node");
    require(staleMap.value("timingText").toString() == QStringLiteral("未运行"),
            "stale snapshot still reports the previous total timing");
    require(staleMap.value("timingOperatorText").toString() == QStringLiteral("未运行"),
            "stale snapshot still reports the previous operator timing");
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

    // 1. Prepare plugin and YAML definitions
    QDir(dirPath).mkpath("tools-package/bin");
    const QString dllSrc = QCoreApplication::applicationDirPath() + "/test_plugin_module.dll";
    const QString dllDst = dirPath + "/tools-package/bin/test_plugin_module.dll";
    if (QFile::exists(dllSrc))
    {
        QFile::copy(dllSrc, dllDst);
    }

    QFile indexFile(dirPath + "/tools-package/module_index.yaml");
    if (indexFile.open(QIODevice::WriteOnly))
    {
        indexFile.write(
            "schema_version: 2\n"
            "variant: cpu\n"
            "modules:\n"
            "  test_module:\n"
            "    library: bin/test_plugin_module.dll\n"
            "tools:\n"
            "  test.plugin.counter:\n"
            "    module: test_module\n"
            "    definition: assets/tools/builtin/Plugin/Basic/counter.yaml\n"
            "  test.plugin.sleep:\n"
            "    module: test_module\n"
            "    definition: assets/tools/builtin/Plugin/Basic/sleep.yaml\n"
            "  test.plugin.fail:\n"
            "    module: test_module\n"
            "    definition: assets/tools/builtin/Plugin/Basic/fail.yaml\n");
        indexFile.close();
    }

    QDir(dirPath).mkpath("tools-package/assets/tools/builtin/Plugin/Basic");
    QFile sleepYaml(dirPath + "/tools-package/assets/tools/builtin/Plugin/Basic/sleep.yaml");
    if (sleepYaml.open(QIODevice::WriteOnly))
    {
        sleepYaml.write(
            "schema_version: 1\n"
            "tool_id: test.plugin.sleep\n"
            "display_name: Plugin Sleep\n"
            "version: 1.0.0\n"
            "kind: cpp_class\n"
            "entry: {registry_name: test.plugin.sleep}\n"
            "init_params: []\n"
            "inputs: []\n"
            "outputs:\n"
            "  - {id: y, name: Y, type: Int}\n");
        sleepYaml.close();
    }

    QFile counterYaml(dirPath + "/tools-package/assets/tools/builtin/Plugin/Basic/counter.yaml");
    if (counterYaml.open(QIODevice::WriteOnly))
    {
        counterYaml.write(
            "schema_version: 1\n"
            "tool_id: test.plugin.counter\n"
            "display_name: Plugin Counter\n"
            "version: 1.0.0\n"
            "kind: cpp_class\n"
            "entry: {registry_name: test.plugin.counter}\n"
            "init_params: []\n"
            "inputs:\n"
            "  - {id: step, name: Step, type: Int, default: 1}\n"
            "outputs:\n"
            "  - {id: count, name: Count, type: Int}\n");
        counterYaml.close();
    }

    QFile failYaml(dirPath + "/tools-package/assets/tools/builtin/Plugin/Basic/fail.yaml");
    if (failYaml.open(QIODevice::WriteOnly))
    {
        failYaml.write(
            "schema_version: 1\n"
            "tool_id: test.plugin.fail\n"
            "display_name: Fail Init Tool\n"
            "version: 1.0.0\n"
            "kind: cpp_class\n"
            "entry: {registry_name: test.plugin.fail_init}\n"
            "init_params: []\n"
            "inputs: []\n"
            "outputs: []\n");
        failYaml.close();
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
    app.save(dirPath + "/test.vrt");
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
    app.addNode("test.plugin.counter", 200, 100);
    app.save(dirPath + "/test.vrt");
    require(app.dirty() == false, "project dirty after save with nodes");

    const auto nodesBeforeRun = app.nodes();
    require(!nodesBeforeRun.isEmpty(), "nodes empty after adding nodes. diag: " + app.diagnostic().toStdString());
    require(nodesBeforeRun.first().toMap().value("state").toString() == "gray", "initial node state is not gray");

    // Start continuous loop
    app.toggleLoop();
    for (int i = 0; i < 50 && app.runState() != "RunningLoop"; ++i)
    {
        QCoreApplication::processEvents();
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
    require(app.runState() == "RunningLoop", "app failed to enter RunningLoop state");

    // Observe active running node state is "blue"
    bool observedBlue = false;
    for (int i = 0; i < 50; ++i)
    {
        QCoreApplication::processEvents();
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
        for (const auto &n : app.nodes())
        {
            if (n.toMap().value("state").toString() == "blue")
            {
                observedBlue = true;
                break;
            }
        }
        if (observedBlue)
            break;
    }
    require(observedBlue, "Running node did not exhibit 'blue' active running state in RunningLoop");
    std::cout << "[Evidence HS5-03] Node state 'blue' observed during RunningLoop execution.\n";

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
    require(nodesAfterRun.first().toMap().value("state").toString() == "green",
            "node state after execution is not green");

    // Add failing node and verify red error state
    app.addNode("test.plugin.fail", 300, 100);
    app.runOnce();
    for (int i = 0; i < 50 && app.busy(); ++i)
    {
        QCoreApplication::processEvents();
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
    const auto nodesWithFail = app.nodes();
    require(nodesWithFail.size() == 3, "expected 3 nodes");
    require(nodesWithFail.at(2).toMap().value("state").toString() == "red", "failing node state is not red");

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

        // 2. Placing YAML into old host directories without package does NOT populate tools
        QDir(dirPath).mkpath("assets/tools/builtin/flow/func");
        QFile legacyYaml(dirPath + "/assets/tools/builtin/flow/func/platform_func.yaml");
        if (legacyYaml.open(QIODevice::WriteOnly))
        {
            legacyYaml.write(
                "schema_version: 1\n"
                "tool_id: test.legacy.func\n"
                "display_name: 遗留函数\n"
                "version: 1.0.0\n"
                "kind: cpp_function\n"
                "entry:\n"
                "  registry_name: test.plugin.function\n"
                "init_params: []\n"
                "inputs: []\n"
                "outputs: []\n");
            legacyYaml.close();
        }
        QDir(dirPath).mkpath("tools/custom/user/py");
        QFile legacyPy(dirPath + "/tools/custom/user/py/legacy.py");
        if (legacyPy.open(QIODevice::WriteOnly))
        {
            legacyPy.write("def run(i): return {'val': 1}\n");
            legacyPy.close();
        }
        QFile legacyPyYaml(dirPath + "/tools/custom/user/py/legacy.yaml");
        if (legacyPyYaml.open(QIODevice::WriteOnly))
        {
            legacyPyYaml.write(
                "schema_version: 1\n"
                "tool_id: test.legacy.python\n"
                "display_name: 遗留脚本\n"
                "version: 1.0.0\n"
                "kind: python_function\n"
                "entry:\n"
                "  file: legacy.py\n"
                "  symbol: run\n"
                "init_params: []\n"
                "inputs: []\n"
                "outputs: []\n");
            legacyPyYaml.close();
        }
        app.rescanTools();
        for (int i = 0; i < 50 && app.busy(); ++i)
        {
            QCoreApplication::processEvents();
            std::this_thread::sleep_for(std::chrono::milliseconds(10));
        }
        require(app.toolDefinitions().isEmpty(),
                "putting YAML into old host directories must not populate tools without package");
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
    QDir(dirPath).mkpath("tools-package/assets/tools/builtin/pkg/counter");
    QFile rootCounterYaml(dirPath + "/tools-package/assets/tools/builtin/pkg/counter/counter.yaml");
    if (rootCounterYaml.open(QIODevice::WriteOnly))
    {
        rootCounterYaml.write(
            "schema_version: 1\n"
            "tool_id: test.pkg.counter\n"
            "display_name: 计数器插件\n"
            "version: 1.0.0\n"
            "kind: cpp_class\n"
            "entry:\n"
            "  registry_name: test.plugin.counter\n"
            "init_params:\n"
            "- id: initial\n"
            "  name: 初始值\n"
            "  type: Int\n"
            "  default: 10\n"
            "inputs: []\n"
            "outputs:\n"
            "- id: count\n"
            "  name: 计数值\n"
            "  type: Int\n");
        rootCounterYaml.close();
    }

    // Package builtin C++ function
    QDir(dirPath).mkpath("tools-package/assets/tools/builtin/pkg/function");
    QFile rootFunctionYaml(dirPath + "/tools-package/assets/tools/builtin/pkg/function/function.yaml");
    if (rootFunctionYaml.open(QIODevice::WriteOnly))
    {
        rootFunctionYaml.write(
            "schema_version: 1\n"
            "tool_id: test.pkg.function\n"
            "display_name: 函数插件\n"
            "version: 1.0.0\n"
            "kind: cpp_function\n"
            "entry:\n"
            "  registry_name: test.plugin.function\n"
            "init_params: []\n"
            "inputs:\n"
            "- id: x\n"
            "  name: 输入\n"
            "  type: Int\n"
            "  default: 5\n"
            "outputs:\n"
            "- id: y\n"
            "  name: 输出\n"
            "  type: Int\n");
        rootFunctionYaml.close();
    }

    // Package Python tool
    QDir(dirPath).mkpath("tools-package/assets/tools/custom/user/py");
    QFile pyScriptA(dirPath + "/tools-package/assets/tools/custom/user/py/user_node_a.py");
    if (pyScriptA.open(QIODevice::WriteOnly))
    {
        pyScriptA.write(
            "def run(inputs):\n"
            "    return {'__status_code': 0, '__status_message': 'ok', 'result': 111, 'image': []}\n");
        pyScriptA.close();
    }
    QFile root3YamlA(dirPath + "/tools-package/assets/tools/custom/user/py/user_node_a.yaml");
    if (root3YamlA.open(QIODevice::WriteOnly))
    {
        root3YamlA.write(
            "schema_version: 1\n"
            "tool_id: test.user.pythonA\n"
            "display_name: 用户脚本A\n"
            "version: 1.0.0\n"
            "kind: python_function\n"
            "entry:\n"
            "  file: user_node_a.py\n"
            "  symbol: run\n"
            "init_params: []\n"
            "inputs: []\n"
            "outputs:\n"
            "- id: result\n"
            "  name: 结果\n"
            "  type: Int\n"
            "- id: image\n"
            "  name: 图像\n"
            "  type: ImageBatch\n");
        root3YamlA.close();
    }

    // Platform receiver tool with host_binding
    QDir(dirPath).mkpath("tools-package/assets/tools/builtin/flow/recv");
    QFile recvYaml(dirPath + "/tools-package/assets/tools/builtin/flow/recv/receiver.yaml");
    if (recvYaml.open(QIODevice::WriteOnly))
    {
        recvYaml.write(
            "schema_version: 1\n"
            "tool_id: test.custom.receiver\n"
            "display_name: 动态接收器\n"
            "version: 1.0.0\n"
            "kind: cpp_class\n"
            "entry:\n"
            "  registry_name: test.custom.receiver\n"
            "init_params:\n"
            "- id: source_flow_name\n"
            "  name: 源流程\n"
            "  type: String\n"
            "  default: ''\n"
            "- id: source_node_name\n"
            "  name: 源节点\n"
            "  type: String\n"
            "  default: ''\n"
            "- id: source_port_id\n"
            "  name: 源端口\n"
            "  type: String\n"
            "  default: ''\n"
            "- id: timeout_ms\n"
            "  name: 超时时间(ms)\n"
            "  type: Int\n"
            "  default: 500\n"
            "inputs: []\n"
            "outputs:\n"
            "- id: value\n"
            "  name: 接收值\n"
              "  type_from: {flow: source_flow_name, node: source_node_name, port: source_port_id}\n"
            "- id: source_status_code\n"
            "  name: 源状态码\n"
            "  type: Int\n"
            "- id: source_status_message\n"
            "  name: 源状态消息\n"
            "  type: String\n");
        recvYaml.close();
    }

    // Unindexed YAML inside package
    QDir(dirPath).mkpath("tools-package/assets/tools/builtin/unindexed");
    QFile unindexedYaml(dirPath + "/tools-package/assets/tools/builtin/unindexed/unindexed.yaml");
    if (unindexedYaml.open(QIODevice::WriteOnly))
    {
        unindexedYaml.write(
            "schema_version: 1\n"
            "tool_id: test.unindexed.tool\n"
            "display_name: 未索引工具\n"
            "version: 1.0.0\n"
            "kind: cpp_class\n"
            "entry:\n"
            "  registry_name: test.unindexed.tool\n"
            "init_params: []\n"
            "inputs: []\n"
            "outputs: []\n");
        unindexedYaml.close();
    }

    // module_index.yaml
    QFile indexFile(dirPath + "/tools-package/module_index.yaml");
    if (indexFile.open(QIODevice::WriteOnly))
    {
        indexFile.write(
            "schema_version: 2\n"
            "variant: cpu\n"
            "modules:\n"
            "  test_module:\n"
            "    library: bin/test_plugin_module.dll\n"
            "tools:\n"
            "  test.pkg.counter:\n"
            "    module: test_module\n"
            "    definition: assets/tools/builtin/pkg/counter/counter.yaml\n"
            "  test.pkg.function:\n"
            "    module: test_module\n"
            "    definition: assets/tools/builtin/pkg/function/function.yaml\n"
            "  test.user.pythonA:\n"
            "    definition: assets/tools/custom/user/py/user_node_a.yaml\n"
            "  test.custom.receiver:\n"
            "    host_binding: visionrt.receiver\n"
            "    definition: assets/tools/builtin/flow/recv/receiver.yaml\n");
        indexFile.close();
    }

    const QString projectFile = dirPath + "/test_project.vrt";
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
            if (id == "test.pkg.counter")
            {
                hasCounter = true;
                require(avail, "counter tool not available");
            }
            if (id == "test.pkg.function")
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

        app.addNode("test.pkg.function", 100, 100);
        app.addNode("test.pkg.counter", 200, 100);
        app.addNode("test.user.pythonA", 300, 100);

        QString funcNodeTitle;
        QString pythonNodeTitle;
        for (const auto &n : app.nodes())
        {
            const auto m = n.toMap();
            if (m.value("toolId").toString() == "test.pkg.function")
            {
                funcNodeTitle = m.value("title").toString();
            }
            if (m.value("toolId").toString() == "test.user.pythonA")
                pythonNodeTitle = m.value("title").toString();
        }
        require(!funcNodeTitle.isEmpty(), "missing function node title");

        // Add Flow 2 for cross-flow receiver
        app.addFlow("flow_2");
        app.setFlowSelected("flow_2", true);
        app.addNode("test.custom.receiver", 100, 100);

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
        auto choices = [&](const QString &parameter)
        {
            for (const auto &item : app.parameters())
            {
                const auto row = item.toMap();
                if (row.value("id").toString() == parameter) return row.value("options").toList();
            }
            return QVariantList{};
        };
        require(choices("source_flow_name").contains(flow1Name), "source flow choices are missing");
        app.setParameter("source_flow_name", true, "\"" + flow1Name + "\"");
        require(!pythonNodeTitle.isEmpty(), "missing Python image source");
        app.setParameter("source_node_name", true, "\"" + pythonNodeTitle + "\"");
        app.setParameter("source_port_id", true, "\"image\"");
        require(app.imageOutputs().size() == 1
                    && app.imageOutputs().first().toMap().value("id").toString() == "value",
                "source-derived image output is missing from the viewport selector");
        require(choices("source_node_name").contains(funcNodeTitle), "source node choices are missing");
        app.setParameter("source_node_name", true, "\"" + funcNodeTitle + "\"");
        require(choices("source_port_id").contains(QStringLiteral("y")), "source output choices are missing");
        app.setParameter("source_port_id", true, "\"y\"");
        require(receiverOutputs->get(2).value("type").toString() == typeText({Type::Int}), "receiver output not resolved after source selection");
        app.setParameter("timeout_ms", true, "1000");

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

        app.addNode("test.pkg.function", 200, 100);
        const auto relationTarget = app.selectedNode();
        require(app.bindInput(app.portContext("x"), recvNodeId, "value"), "cannot bind source-derived receiver output");
        require(app.wires().size() == 1, "valid receiver relation is missing");
        const auto relationPorts = app.wires().front().toMap().value("ports").toList();
        require(relationPorts.size() == 1 && relationPorts.front().toMap().value("output").toString() == "value"
                    && relationPorts.front().toMap().value("input").toString() == "x",
                "grouped wire discarded the source and target port identity");
        app.selectNode(recvNodeId);
        app.setParameter("source_node_name", true, "\"" + pythonNodeTitle + "\"");
        app.setParameter("source_port_id", true, "\"image\"");
        require(app.wires().isEmpty(), "incompatible or unresolved receiver binding is still drawn as an active wire");
        require(outModel->get(2).value("count").toInt() == 0,
                "invalid receiver relation is still counted as an active output reference");
        app.setParameter("source_node_name", true, "\"" + funcNodeTitle + "\"");
        app.setParameter("source_port_id", true, "\"y\"");
        require(app.wires().size() == 1 && outModel->get(2).value("count").toInt() == 1,
                "restoring the source type did not restore the same input relation");
        app.deleteNodes({relationTarget});
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
        QFile::remove(dirPath + "/tools-package/assets/tools/builtin/flow/recv/receiver.yaml");
        QFile renamedYaml(dirPath + "/tools-package/assets/tools/builtin/flow/recv/renamed.yaml");
        if (renamedYaml.open(QIODevice::WriteOnly))
        {
            renamedYaml.write(
                "schema_version: 1\n"
                "tool_id: test.custom.renamed_recv\n"
                "display_name: 改名接收器\n"
                "version: 1.0.0\n"
                "kind: cpp_class\n"
                "entry:\n"
                "  registry_name: test.custom.renamed_recv\n"
                "init_params:\n"
                "- id: source_flow_name\n"
                "  name: 源流程\n"
                "  type: String\n"
                "  default: ''\n"
                "- id: source_node_name\n"
                "  name: 源节点\n"
                "  type: String\n"
                "  default: ''\n"
                "- id: source_port_id\n"
                "  name: 源端口\n"
                "  type: String\n"
                "  default: ''\n"
                "- id: timeout_ms\n"
                "  name: 超时时间(ms)\n"
                "  type: Int\n"
                "  default: 500\n"
                "inputs: []\n"
                "outputs:\n"
                "- id: value\n"
                "  name: 接收值\n"
                "  type_from: {flow: source_flow_name, node: source_node_name, port: source_port_id}\n"
                "- id: source_status_code\n"
                "  name: 源状态码\n"
                "  type: Int\n"
                "- id: source_status_message\n"
                "  name: 源状态消息\n"
                "  type: String\n");
            renamedYaml.close();
        }

        // Update module_index.yaml with renamed tool
        if (indexFile.open(QIODevice::WriteOnly))
        {
            indexFile.write(
                "schema_version: 2\n"
                "variant: cpu\n"
                "modules:\n"
                "  test_module:\n"
                "    library: bin/test_plugin_module.dll\n"
                "tools:\n"
                "  test.pkg.counter:\n"
                "    module: test_module\n"
                "    definition: assets/tools/builtin/pkg/counter/counter.yaml\n"
                "  test.pkg.function:\n"
                "    module: test_module\n"
                "    definition: assets/tools/builtin/pkg/function/function.yaml\n"
                "  test.user.pythonA:\n"
                "    definition: assets/tools/custom/user/py/user_node_a.yaml\n"
                "  test.custom.renamed_recv:\n"
                "    host_binding: visionrt.receiver\n"
                "    definition: assets/tools/builtin/flow/recv/renamed.yaml\n");
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
        QFile brokenIndex(dirPath + "/tools-package/module_index.yaml");
        if (brokenIndex.open(QIODevice::WriteOnly))
        {
            brokenIndex.write("unclosed_yaml: [invalid");
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
            if (d.contains("tools-package") || d.contains("module_index.yaml"))
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
    if (argc >= 2 && (std::string(argv[1]) == "--roi-child" || std::string(argv[1]) == "--output-child"
                      || std::string(argv[1]) == "--parameters-child" || std::string(argv[1]) == "--paths-child"
                      || std::string(argv[1]) == "--canvas-child" || std::string(argv[1]) == "--startup-child"
                      || std::string(argv[1]) == "--catalog-child"
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
