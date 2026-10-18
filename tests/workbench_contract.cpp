#include "ui/ToolTreeModel.h"
#include "ui/OutputModel.h"
#include "ui/AppController.h"
#include "ui/Theme.h"
#include "ui/ImageViewport.h"
#include <QGuiApplication>
#include <QProcess>
#include <QTemporaryDir>
#include <QQmlApplicationEngine>
#include <QQmlContext>
#include <QQuickWindow>
#include <QQuickItem>
#include <QQuickStyle>
#include <QImage>
#include <QUrl>
#include <filesystem>
#include <iostream>
#include <stdexcept>

using namespace visionrt;
namespace {
void require(bool value, const char* message) { if (!value) throw std::runtime_error(message); }
void require(bool value, const std::string& message) { if (!value) throw std::runtime_error(message); }
void treeContract() {
    ToolRegistry registry;
    ToolDefinition tool;
    tool.id = "custom.precise";
    tool.displayName = "精确整数";
    tool.version = "1.0";
    tool.kind = ToolKind::PythonFunction;
    tool.script = "precise.py";
    tool.symbol = "execute";
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

void mainQmlContract(double scale) {
    QQuickStyle::setStyle("Basic");
    QTemporaryDir directory;
    const auto dirPath = directory.path();

    // 1. Prepare plugin and YAML definitions
    QDir(dirPath).mkpath("tools-package/bin");
    const QString dllSrc = QCoreApplication::applicationDirPath() + "/test_plugin_module.dll";
    const QString dllDst = dirPath + "/tools-package/bin/test_plugin_module.dll";
    if (QFile::exists(dllSrc)) {
        QFile::copy(dllSrc, dllDst);
    }

    QFile indexFile(dirPath + "/tools-package/module_index.yaml");
    if (indexFile.open(QIODevice::WriteOnly)) {
        indexFile.write(
            "variant: cpu\n"
            "modules:\n"
            "  test_module:\n"
            "    library: bin/test_plugin_module.dll\n"
            "tools:\n"
            "  test.plugin.counter:\n"
            "    module: test_module\n"
            "    definition: tools/counter.yaml\n"
            "  test.plugin.sleep:\n"
            "    module: test_module\n"
            "    definition: tools/sleep.yaml\n"
            "  test.plugin.fail_init:\n"
            "    module: test_module\n"
            "    definition: tools/fail_init.yaml\n"
        );
        indexFile.close();
    }

    QDir(dirPath).mkpath("tools-package/assets/tools/builtin/Plugin/Basic");
    QFile sleepYaml(dirPath + "/tools-package/assets/tools/builtin/Plugin/Basic/sleep.yaml");
    if (sleepYaml.open(QIODevice::WriteOnly)) {
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
            "  - {id: y, name: Y, type: Int}\n"
        );
        sleepYaml.close();
    }

    QFile counterYaml(dirPath + "/tools-package/assets/tools/builtin/Plugin/Basic/counter.yaml");
    if (counterYaml.open(QIODevice::WriteOnly)) {
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
            "  - {id: count, name: Count, type: Int}\n"
        );
        counterYaml.close();
    }

    QFile failYaml(dirPath + "/tools-package/assets/tools/builtin/Plugin/Basic/fail.yaml");
    if (failYaml.open(QIODevice::WriteOnly)) {
        failYaml.write(
            "schema_version: 1\n"
            "tool_id: test.plugin.fail\n"
            "display_name: Fail Init Tool\n"
            "version: 1.0.0\n"
            "kind: cpp_class\n"
            "entry: {registry_name: test.plugin.fail_init}\n"
            "init_params: []\n"
            "inputs: []\n"
            "outputs: []\n"
        );
        failYaml.close();
    }

    AppController app(1, dirPath);
    for (int i = 0; i < 50 && app.busy(); ++i) {
        QCoreApplication::processEvents();
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }

    qmlRegisterType<visionrt::ImageViewport>("VisionRT.View", 1, 0, "ImageViewport");
    auto* theme = visionrt::Theme::instance();
    qmlRegisterSingletonInstance<visionrt::Theme>("VisionRT", 1, 0, "Theme", theme);

    QQmlApplicationEngine engine;
    const auto appDir = QCoreApplication::applicationDirPath();
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

    if (QFile::exists(qmlPath)) {
        engine.load(QUrl::fromLocalFile(qmlPath));
    } else {
        engine.loadFromModule("VisionRT", "Main");
    }

    const auto roots = engine.rootObjects();
    require(!roots.isEmpty(), "Main.qml failed to instantiate");
    auto* window = qobject_cast<QQuickWindow*>(roots.first());
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

    auto* leftHandle = window->findChild<QQuickItem*>("leftHandle");
    auto* leftHandleMask = window->findChild<QQuickItem*>("leftHandleMask");
    auto* leftHost = window->findChild<QQuickItem*>("leftHost");
    auto* leftContent = window->findChild<QQuickItem*>("leftContent");
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

    auto* drawerToggle = window->findChild<QQuickItem*>("drawerToggle");
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
    auto* flowSearch = window->findChild<QQuickItem*>("flowSearch");
    require(flowSearch != nullptr, "missing flowSearch input field");
    require(flowSearch->hasActiveFocus(), "flowSearch input does not have active focus after Ctrl+F");

    // 5. Tab key: visible focus target
    QKeyEvent tabPress(QEvent::KeyPress, Qt::Key_Tab, Qt::NoModifier);
    QKeyEvent tabRelease(QEvent::KeyRelease, Qt::Key_Tab, Qt::NoModifier);
    QCoreApplication::sendEvent(window, &tabPress);
    QCoreApplication::sendEvent(window, &tabRelease);
    for (int i = 0; i < 10; ++i) QCoreApplication::processEvents();

    auto* activeFocus = window->activeFocusItem();
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
    for (int i = 0; i < 50 && app.runState() != "RunningLoop"; ++i) {
        QCoreApplication::processEvents();
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
    require(app.runState() == "RunningLoop", "app failed to enter RunningLoop state");

    // Observe active running node state is "blue"
    bool observedBlue = false;
    for (int i = 0; i < 50; ++i) {
        QCoreApplication::processEvents();
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
        for (const auto& n : app.nodes()) {
            if (n.toMap().value("state").toString() == "blue") {
                observedBlue = true;
                break;
            }
        }
        if (observedBlue) break;
    }
    require(observedBlue, "Running node did not exhibit 'blue' active running state in RunningLoop");
    std::cout << "[Evidence HS5-03] Node state 'blue' observed during RunningLoop execution.\n";

    // Drawer toggle while running: does not dirty project or mutate instance identities
    const auto* hostIdentityBefore = leftHost;
    const auto* contentIdentityBefore = leftContent;
    QMetaObject::invokeMethod(window, "toggleLeft");
    for (int i = 0; i < 15; ++i) QCoreApplication::processEvents();
    require(app.dirty() == false, "toggling drawer during active execution dirtied project");

    QMetaObject::invokeMethod(window, "toggleLeft");
    for (int i = 0; i < 15; ++i) QCoreApplication::processEvents();
    require(app.dirty() == false, "re-expanding drawer during active execution dirtied project");
    require(window->findChild<QQuickItem*>("leftHost") == hostIdentityBefore, "leftHost identity changed across toggle");
    require(window->findChild<QQuickItem*>("leftContent") == contentIdentityBefore, "leftContent identity changed across toggle");

    // Stop loop and wait for Idle
    app.toggleLoop();
    for (int i = 0; i < 50 && app.busy(); ++i) {
        QCoreApplication::processEvents();
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
    require(app.runState() == "Idle", "app failed to return to Idle after stopping loop");

    // Node state after successful run is green
    const auto nodesAfterRun = app.nodes();
    require(!nodesAfterRun.isEmpty(), "nodes empty after running loop");
    require(nodesAfterRun.first().toMap().value("state").toString() == "green", "node state after execution is not green");

    // Add failing node and verify red error state
    app.addNode("test.plugin.fail", 300, 100);
    app.runOnce();
    for (int i = 0; i < 50 && app.busy(); ++i) {
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
        QMouseEvent pressEv(QEvent::MouseButtonPress, roiViewport.imageToItem({20, 20}), Qt::LeftButton, Qt::LeftButton, Qt::NoModifier);
        QCoreApplication::sendEvent(&roiViewport, &pressEv);
        QMouseEvent moveEv(QEvent::MouseMove, roiViewport.imageToItem({60, 60}), Qt::NoButton, Qt::LeftButton, Qt::NoModifier);
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
    for (const auto& n : nodesWithFail) {
        if (n.toMap().value("toolId").toString() == "test.plugin.counter") {
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
    for (const auto& paramVar : app.parameters()) {
        const auto pMap = paramVar.toMap();
        if (pMap.value("id").toString() == "step") {
            paramPreserved = (pMap.value("literal").toString() == "99" || pMap.value("valueText").toString() == "99");
        }
    }
    require(paramPreserved, "modified parameter draft was not preserved across drawer toggle");

    // 8. Screenshot capture evidence for this scale
    QImage frame = window->grabWindow();
    require(!frame.isNull(), "grabWindow failed");
    const int expectedPhysicalWidth = static_cast<int>(std::round(960.0 * dpr));
    const int expectedPhysicalHeight = static_cast<int>(std::round(540.0 * dpr));
    require(std::abs(frame.width() - expectedPhysicalWidth) <= 2, "Physical frame width does not match logical * DPR");
    require(std::abs(frame.height() - expectedPhysicalHeight) <= 2, "Physical frame height does not match logical * DPR");

    const std::filesystem::path evidenceDir = "build/evidence";
    std::filesystem::create_directories(evidenceDir);
    char scaleBuf[16];
    std::snprintf(scaleBuf, sizeof(scaleBuf), "%.1f", scale);
    const auto screenshotName = "workbench_dpi_" + std::string(scaleBuf) + ".png";
    const auto screenshotPath = evidenceDir / screenshotName;
    const bool saved = frame.save(QString::fromStdString(screenshotPath.string()));
    require(saved, "Failed to save screenshot evidence to " + screenshotPath.string());
    require(std::filesystem::exists(screenshotPath) && std::filesystem::file_size(screenshotPath) > 0,
            "Saved screenshot file does not exist or is empty");

    std::cout << "[Evidence HS5-03] Child process scale=" << scaleBuf
              << ", actual DPR=" << dpr
              << ", logical size=(" << window->width() << ", " << window->height() << ")"
              << ", physical size=(" << frame.width() << ", " << frame.height() << ")"
              << ", screenshot=" << screenshotPath.string() << "\n";
}

void multiRootCoexistenceAndRefreshContract() {
    std::cout << "\n[MULTI-ROOT & REFRESH CONTRACT START]\n";
    QTemporaryDir directory;
    const auto dirPath = directory.path();

    QDir(dirPath).mkpath("tools-package/bin");
    const QString dllSrc = QCoreApplication::applicationDirPath() + "/test_plugin_module.dll";
    const QString dllDst = dirPath + "/tools-package/bin/test_plugin_module.dll";
    if (QFile::exists(dllSrc)) {
        QFile::copy(dllSrc, dllDst);
    }

    QFile indexFile(dirPath + "/tools-package/module_index.yaml");
    if (indexFile.open(QIODevice::WriteOnly)) {
        indexFile.write(
            "variant: cpu\n"
            "modules:\n"
            "  test_module:\n"
            "    library: bin/test_plugin_module.dll\n"
            "tools:\n"
            "  test.plugin.counter:\n"
            "    module: test_module\n"
            "    definition: tools/counter.yaml\n"
            "  test.plugin.function:\n"
            "    module: test_module\n"
            "    definition: tools/function.yaml\n"
        );
        indexFile.close();
    }

    // Root 1: Platform builtin (cpp_function)
    QDir(dirPath).mkpath("assets/tools/builtin/flow/func");
    QFile root1Yaml(dirPath + "/assets/tools/builtin/flow/func/platform_func.yaml");
    if (root1Yaml.open(QIODevice::WriteOnly)) {
        root1Yaml.write(
            "schema_version: 1\n"
            "tool_id: test.platform.func\n"
            "display_name: 平台函数\n"
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
            "  type: Int\n"
        );
        root1Yaml.close();
    }

    // Root 2: Package builtin (cpp_class)
    QDir(dirPath).mkpath("tools-package/assets/tools/builtin/pkg/counter");
    QFile root2Yaml(dirPath + "/tools-package/assets/tools/builtin/pkg/counter/counter.yaml");
    if (root2Yaml.open(QIODevice::WriteOnly)) {
        root2Yaml.write(
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
            "  type: Int\n"
        );
        root2Yaml.close();
    }

    // Root 3: Custom Python tool A (valid)
    QDir(dirPath).mkpath("tools/custom/user/py");
    QFile pyScriptA(dirPath + "/tools/custom/user/py/user_node_a.py");
    if (pyScriptA.open(QIODevice::WriteOnly)) {
        pyScriptA.write(
            "def run(inputs):\n"
            "    return {'__status_code': 0, '__status_message': 'ok', 'result': 111}\n"
        );
        pyScriptA.close();
    }
    QFile root3YamlA(dirPath + "/tools/custom/user/py/user_node_a.yaml");
    if (root3YamlA.open(QIODevice::WriteOnly)) {
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
        );
        root3YamlA.close();
    }

    // Root 3: Custom Python tool B (valid)
    QFile pyScriptB(dirPath + "/tools/custom/user/py/user_node_b.py");
    if (pyScriptB.open(QIODevice::WriteOnly)) {
        pyScriptB.write(
            "def run(inputs):\n"
            "    return {'__status_code': 0, '__status_message': 'ok', 'result': 222}\n"
        );
        pyScriptB.close();
    }
    QFile root3YamlB(dirPath + "/tools/custom/user/py/user_node_b.yaml");
    if (root3YamlB.open(QIODevice::WriteOnly)) {
        root3YamlB.write(
            "schema_version: 1\n"
            "tool_id: test.user.pythonB\n"
            "display_name: 用户脚本B\n"
            "version: 1.0.0\n"
            "kind: python_function\n"
            "entry:\n"
            "  file: user_node_b.py\n"
            "  symbol: run\n"
            "init_params: []\n"
            "inputs: []\n"
            "outputs:\n"
            "- id: result\n"
            "  name: 结果\n"
            "  type: Int\n"
        );
        root3YamlB.close();
    }

    // Root 3: Custom Python tool C (broken syntax script)
    QFile pyScriptC(dirPath + "/tools/custom/user/py/broken_node.py");
    if (pyScriptC.open(QIODevice::WriteOnly)) {
        pyScriptC.write("def invalid_syntax(\n  bad syntax !!\n");
        pyScriptC.close();
    }
    QFile root3YamlC(dirPath + "/tools/custom/user/py/broken_node.yaml");
    if (root3YamlC.open(QIODevice::WriteOnly)) {
        root3YamlC.write(
            "schema_version: 1\n"
            "tool_id: test.user.broken\n"
            "display_name: 损坏脚本\n"
            "version: 1.0.0\n"
            "kind: python_function\n"
            "entry:\n"
            "  file: broken_node.py\n"
            "  symbol: invalid_syntax\n"
            "init_params: []\n"
            "inputs: []\n"
            "outputs:\n"
            "- id: result\n"
            "  name: 结果\n"
            "  type: Int\n"
        );
        root3YamlC.close();
    }

    // Duplicate YAML
    QFile dupYaml(dirPath + "/tools/custom/user/py/duplicate.yaml");
    if (dupYaml.open(QIODevice::WriteOnly)) {
        dupYaml.write(
            "schema_version: 1\n"
            "tool_id: test.platform.func\n"
            "display_name: 重复平台函数\n"
            "version: 1.0.0\n"
            "kind: cpp_function\n"
            "entry:\n"
            "  registry_name: test.plugin.function\n"
            "init_params: []\n"
            "inputs: []\n"
            "outputs: []\n"
        );
        dupYaml.close();
    }

    const QString projectFile = dirPath + "/test_project.vrt";
    {
        AppController app(1, dirPath);
        for (int i = 0; i < 50 && app.busy(); ++i) {
            QCoreApplication::processEvents();
            std::this_thread::sleep_for(std::chrono::milliseconds(10));
        }

        const auto diag = app.diagnostic();
        require(diag.contains("Duplicate tool_id test.platform.func"), "duplicate diagnostic missing");
        std::cout << "[Evidence HS3-01] Duplicate diagnosis detected: " << diag.toStdString() << "\n";

        const auto defs = app.toolDefinitions();
        bool hasPlatform = false, hasPkg = false, hasPyA = false, hasPyB = false, hasBroken = false;
        for (const auto& item : defs) {
            const auto map = item.toMap();
            const auto id = map.value("id").toString();
            const bool avail = map.value("available").toBool();
            if (id == "test.platform.func") { hasPlatform = true; require(avail, "platform tool not available"); }
            if (id == "test.pkg.counter") { hasPkg = true; require(avail, "package tool not available"); }
            if (id == "test.user.pythonA") { hasPyA = true; require(avail, "pythonA tool not available"); }
            if (id == "test.user.pythonB") { hasPyB = true; require(avail, "pythonB tool not available"); }
            if (id == "test.user.broken") { hasBroken = true; require(!avail, "broken python script must not be available"); }
        }
        require(hasPlatform && hasPkg && hasPyA && hasPyB && hasBroken, "definitions missing expected tools");
        std::cout << "[Evidence HS3-01] Coexistence verified: platform, pkg, pythonA, pythonB available; broken script isolated.\n";

        // Add nodes
        app.addNode("test.platform.func", 100, 100);
        app.addNode("test.pkg.counter", 200, 100);
        app.addNode("test.user.pythonA", 300, 100);
        app.addNode("test.user.pythonB", 400, 100);

        const auto nodesBefore = app.nodes();
        require(nodesBefore.size() == 4, "expected 4 nodes added");

        app.runOnce();
        for (int i = 0; i < 100 && app.busy(); ++i) {
            QCoreApplication::processEvents();
            std::this_thread::sleep_for(std::chrono::milliseconds(10));
        }

        auto* outModel = qobject_cast<OutputModel*>(app.outputModel());
        require(outModel != nullptr, "outputModel is null");

        // Check node statuses and output values
        for (const auto& n : app.nodes()) {
            const auto m = n.toMap();
            const auto id = m.value("id").toString();
            const auto toolId = m.value("toolId").toString();
            const auto state = m.value("state").toString();
            require(state == "green", "node execution failed: " + toolId.toStdString());
            app.selectNode(id);
            const auto preview = outModel->get(2).value("preview").toString();
            std::cout << "[Evidence HS3-01] Node " << id.toStdString() << " (" << toolId.toStdString() << ") state=" << state.toStdString() << ", result=" << preview.toStdString() << "\n";
            if (toolId == "test.platform.func") require(preview == "10", "unexpected platform.func output: " + preview.toStdString());
            if (toolId == "test.pkg.counter") require(preview == "11", "unexpected pkg.counter output: " + preview.toStdString());
            if (toolId == "test.user.pythonA") require(preview == "111", "unexpected pythonA output: " + preview.toStdString());
            if (toolId == "test.user.pythonB") require(preview == "222", "unexpected pythonB output: " + preview.toStdString());
        }

        // Save project
        require(app.save(projectFile), "save project failed");
        std::cout << "[Evidence HS3-01] Project saved successfully to: " << projectFile.toStdString() << "\n";

        // Reopen and verify equality
        app.requestOpen(projectFile);
        for (int i = 0; i < 50 && app.busy(); ++i) {
            QCoreApplication::processEvents();
            std::this_thread::sleep_for(std::chrono::milliseconds(10));
        }
        const auto reopenedNodes = app.nodes();
        require(reopenedNodes.size() == 4, "reopened project node count differs");
        app.runOnce();
        for (int i = 0; i < 100 && app.busy(); ++i) {
            QCoreApplication::processEvents();
            std::this_thread::sleep_for(std::chrono::milliseconds(10));
        }
        for (const auto& n : app.nodes()) {
            const auto m = n.toMap();
            const auto id = m.value("id").toString();
            const auto toolId = m.value("toolId").toString();
            require(m.value("state").toString() == "green", "reopened node run failed");
            app.selectNode(id);
            const auto preview = outModel->get(2).value("preview").toString();
            if (toolId == "test.platform.func") require(preview == "10", "reopened platform.func output mismatch");
            if (toolId == "test.user.pythonA") require(preview == "111", "reopened pythonA output mismatch");
            if (toolId == "test.user.pythonB") require(preview == "222", "reopened pythonB output mismatch");
        }
        std::cout << "[Evidence HS3-01] Save/reopen numerical equality confirmed.\n";

        // Delete Python A YAML and rescan
        std::cout << "[Evidence HS3-01] Deleting Python A and executing rescanTools()...\n";
        QFile::remove(dirPath + "/tools/custom/user/py/user_node_a.yaml");
        app.rescanTools();
        for (int i = 0; i < 100 && app.busy(); ++i) {
            QCoreApplication::processEvents();
            std::this_thread::sleep_for(std::chrono::milliseconds(10));
        }
        std::cout << "[Evidence HS3-01] rescan completed. Busy=" << app.busy() << ", runState=" << app.runState().toStdString() << "\n";

        const auto defsAfterDelete = app.toolDefinitions();
        bool foundDeletedA = false, foundB = false, bAvailable = false;
        for (const auto& item : defsAfterDelete) {
            const auto map = item.toMap();
            const auto id = map.value("id").toString();
            if (id == "test.user.pythonA") foundDeletedA = true;
            if (id == "test.user.pythonB") {
                foundB = true;
                bAvailable = map.value("available").toBool();
            }
        }
        require(!foundDeletedA, "deleted Python A YAML still in toolDefinitions");
        require(foundB && bAvailable, "Python B lost or unavailable after rescan");
        std::cout << "[Evidence HS3-01] Tool B remains Available after Tool A deleted and rescan.\n";

        // Verify existing node for Python A is marked defMissing
        bool nodeADefMissing = false;
        for (const auto& n : app.nodes()) {
            const auto map = n.toMap();
            if (map.value("toolId").toString() == "test.user.pythonA") {
                nodeADefMissing = map.value("defMissing").toBool();
            }
        }
        require(nodeADefMissing, "node for deleted tool definition is not marked defMissing");

        // Add a NEW node with Python B and run it
        app.addNode("test.user.pythonB", 500, 100);
        const auto nodesAfterAddB = app.nodes();
        require(nodesAfterAddB.size() == 5, "failed to add new Python B node");
        const auto newBNodeId = nodesAfterAddB.last().toMap().value("id").toString();

        // Run the new Python B node via runNode()
        app.selectNode(newBNodeId);
        app.runNode();
        for (int i = 0; i < 100 && app.busy(); ++i) {
            QCoreApplication::processEvents();
            std::this_thread::sleep_for(std::chrono::milliseconds(10));
        }
        const auto newBPreview = outModel->get(2).value("preview").toString();
        require(newBPreview == "222", "new Python B node failed to run after rescan: " + newBPreview.toStdString());
        std::cout << "[Evidence HS3-01] New Python B node ran successfully via runNode with result=" << newBPreview.toStdString() << "\n";

        // Clean up the defMissing node and run the entire flow once
        QString nodeAId;
        for (const auto& n : app.nodes()) {
            const auto map = n.toMap();
            if (map.value("toolId").toString() == "test.user.pythonA") {
                nodeAId = map.value("id").toString();
                break;
            }
        }
        require(!nodeAId.isEmpty(), "missing node A");
        app.deleteNodes({nodeAId});
        app.runOnce();
        for (int i = 0; i < 100 && app.busy(); ++i) {
            QCoreApplication::processEvents();
            std::this_thread::sleep_for(std::chrono::milliseconds(10));
        }
        app.selectNode(newBNodeId);
        require(outModel->get(2).value("preview").toString() == "222", "runOnce after deleting missing node failed");
        std::cout << "[Evidence HS3-01] Entire flow ran successfully after removing missing node, new B result=222\n";
    }

    // Verify: Start without custom directory, create directory afterwards, then refresh/rescan discovers it
    QTemporaryDir emptyDir;
    AppController lateApp(1, emptyDir.path());
    for (int i = 0; i < 50 && lateApp.busy(); ++i) {
        QCoreApplication::processEvents();
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
    require(lateApp.toolDefinitions().isEmpty(), "lateApp expected empty toolDefinitions initially");

    // Create custom directory and tool dynamically after startup
    QDir(emptyDir.path()).mkpath("tools/custom/late_cat/late_tool");
    QFile latePy(emptyDir.path() + "/tools/custom/late_cat/late_tool/late.py");
    if (latePy.open(QIODevice::WriteOnly)) {
        latePy.write("def run(inputs):\n    return {'__status_code': 0, '__status_message': '', 'val': 888}\n");
        latePy.close();
    }
    QFile lateYaml(emptyDir.path() + "/tools/custom/late_cat/late_tool/late.yaml");
    if (lateYaml.open(QIODevice::WriteOnly)) {
        lateYaml.write(
            "schema_version: 1\n"
            "tool_id: test.late.python\n"
            "display_name: 启动后动态脚本\n"
            "version: 1.0.0\n"
            "kind: python_function\n"
            "entry:\n"
            "  file: late.py\n"
            "  symbol: run\n"
            "init_params: []\n"
            "inputs: []\n"
            "outputs:\n"
            "- id: val\n"
            "  name: 输出\n"
            "  type: Int\n"
        );
        lateYaml.close();
    }

    lateApp.refreshPython();
    for (int i = 0; i < 100 && lateApp.busy(); ++i) {
        QCoreApplication::processEvents();
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
    std::cout << "[Evidence HS3-01] lateApp busy=" << lateApp.busy() << ", diagnostic: " << lateApp.diagnostic().toStdString() << "\n";
    bool foundLate = false;
    for (const auto& item : lateApp.toolDefinitions()) {
        const auto map = item.toMap();
        if (map.value("id").toString() == "test.late.python") {
            foundLate = true;
            std::cout << "[Evidence HS3-01] Late tool available=" << map.value("available").toBool()
                      << ", message=" << map.value("message").toString().toStdString() << "\n";
            require(map.value("available").toBool(), "dynamically discovered tool is not available: " + map.value("message").toString().toStdString());
        }
    }
    require(foundLate, "dynamic custom directory created after startup not discovered by refresh");
    lateApp.addNode("test.late.python", 100, 100);
    lateApp.runOnce();
    for (int i = 0; i < 50 && lateApp.busy(); ++i) {
        QCoreApplication::processEvents();
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
    auto* lateOutModel = qobject_cast<OutputModel*>(lateApp.outputModel());
    lateApp.selectNode(lateApp.nodes().first().toMap().value("id").toString());
    const auto latePreview = lateOutModel->get(2).value("preview").toString();
    require(latePreview == "888", "dynamically discovered tool failed execution: " + latePreview.toStdString());
    std::cout << "[Evidence HS3-01] Post-startup custom directory discovery verified, execution result=" << latePreview.toStdString() << "\n";
    std::cout << "[MULTI-ROOT & REFRESH CONTRACT PASSED]\n";
}

void shortWindowContract() {
    std::cout << "\n[SHORT WINDOW CONTRACT START]\n";
    QTemporaryDir directory;
    const auto dirPath = directory.path();
    AppController app(1, dirPath);
    for (int i = 0; i < 50 && app.busy(); ++i) {
        QCoreApplication::processEvents();
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }

    qmlRegisterType<visionrt::ImageViewport>("VisionRT.View", 1, 0, "ImageViewport");
    auto* theme = visionrt::Theme::instance();
    qmlRegisterSingletonInstance<visionrt::Theme>("VisionRT", 1, 0, "Theme", theme);

    QQmlApplicationEngine engine;
    const auto appDir = QCoreApplication::applicationDirPath();
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

    if (QFile::exists(qmlPath)) {
        engine.load(QUrl::fromLocalFile(qmlPath));
    } else {
        engine.loadFromModule("VisionRT", "Main");
    }

    const auto roots = engine.rootObjects();
    require(!roots.isEmpty(), "Main.qml failed to instantiate in shortWindowContract");
    auto* window = qobject_cast<QQuickWindow*>(roots.first());
    require(window != nullptr, "Main.qml root is not QQuickWindow");

    window->setFlags(Qt::FramelessWindowHint | Qt::Window);
    window->resize(800, 480);
    window->show();
    for (int i = 0; i < 20; ++i) QCoreApplication::processEvents();

    auto* leftHost = window->findChild<QQuickItem*>("leftHost");
    auto* leftContent = window->findChild<QQuickItem*>("leftContent");
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
    const bool saved = frame.save(QString::fromStdString(screenshotPath.string()));
    require(saved, "Failed to save short window screenshot");
    require(std::filesystem::exists(screenshotPath) && std::filesystem::file_size(screenshotPath) > 0,
            "Saved short window screenshot file does not exist or is empty");

    std::cout << "[Evidence HS5-03] Short window contract verified at ("
              << window->width() << "x" << window->height() << "), screenshot=" << screenshotPath.string() << "\n";
    std::cout << "[SHORT WINDOW CONTRACT PASSED]\n";
}
}
int main(int argc, char** argv) {
    if (argc >= 2 && std::string(argv[1]) == "--short-window-child") {
        QGuiApplication qt(argc, argv);
        try {
            shortWindowContract();
            return 0;
        } catch (const std::exception& error) {
            std::cerr << "[Short Window Error] " << error.what() << '\n';
            return 1;
        }
    }
    if (argc >= 3 && std::string(argv[1]) == "--dpi-child") {
        const double scale = std::stod(argv[2]);
        qputenv("QT_SCALE_FACTOR", QByteArray::number(scale, 'f', 1));
        QGuiApplication qt(argc, argv);
        try {
            mainQmlContract(scale);
            return 0;
        } catch (const std::exception& error) {
            std::cerr << "[DPI Child Error for scale=" << scale << "] " << error.what() << '\n';
            return 1;
        }
    }

    QGuiApplication qt(argc, argv);
    try {
        treeContract(); outputContract(); pendingContract(); nodeDiagnosticsContract();
        multiRootCoexistenceAndRefreshContract();

        const QString appPath = QCoreApplication::applicationFilePath();

        // 1. Run short window contract in child process
        {
            QProcess shortProc;
            shortProc.start(appPath, {"--short-window-child"});
            const bool shortFinished = shortProc.waitForFinished(45000);
            require(shortFinished, "Short window child process timed out");
            std::cout << shortProc.readAllStandardOutput().toStdString();
            if (shortProc.exitCode() != 0) {
                std::cerr << shortProc.readAllStandardError().toStdString();
                throw std::runtime_error("Short window child process failed");
            }
            const auto shortScreenshot = std::filesystem::path("build/evidence/workbench_short_window.png");
            require(std::filesystem::exists(shortScreenshot) && std::filesystem::file_size(shortScreenshot) > 0,
                    "Short window screenshot missing");
        }

        // 2. Run 3 DPI scale child processes (1.0, 1.5, 2.0)
        const double scales[] = {1.0, 1.5, 2.0};
        for (double scale : scales) {
            QProcess proc;
            proc.start(appPath, {"--dpi-child", QString::number(scale, 'f', 1)});
            const bool finished = proc.waitForFinished(45000);
            require(finished, "DPI child process timed out for scale " + std::to_string(scale));
            std::cout << proc.readAllStandardOutput().toStdString();
            if (proc.exitCode() != 0) {
                std::cerr << proc.readAllStandardError().toStdString();
                throw std::runtime_error("DPI child process failed for scale " + std::to_string(scale));
            }
            char scaleBuf[16];
            std::snprintf(scaleBuf, sizeof(scaleBuf), "%.1f", scale);
            const auto screenshot = std::filesystem::path("build/evidence") / ("workbench_dpi_" + std::string(scaleBuf) + ".png");
            require(std::filesystem::exists(screenshot) && std::filesystem::file_size(screenshot) > 0,
                    "Screenshot missing for scale " + std::string(scaleBuf));
        }

        std::cout << "workbench contract passed\n";
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
