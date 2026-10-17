#include "ui/ToolTreeModel.h"
#include "ui/OutputModel.h"
#include "ui/AppController.h"
#include "ui/Theme.h"
#include "ui/ImageViewport.h"
#include <QGuiApplication>
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

void mainQmlContract() {
    QQuickStyle::setStyle("Basic");
    QTemporaryDir directory;
    AppController app(1, directory.path());
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
    engine.addImportPath("F:/Projects/VisionRT/build/bin/qml");
    engine.addImportPath("F:/Projects/VisionRT/build/qml");
    engine.addImportPath("F:/Projects/VisionRT/src/ui/qml");

    engine.rootContext()->setContextProperty("app", &app);
    engine.rootContext()->setContextProperty("Theme", theme);
    engine.rootContext()->setContextProperty("demoMenu", false);
    engine.rootContext()->setContextProperty("demoSelect", false);

    const auto qmlPath = QStringLiteral("F:/Projects/VisionRT/src/ui/qml/Main.qml");
    engine.load(QUrl::fromLocalFile(qmlPath));

    const auto roots = engine.rootObjects();
    require(!roots.isEmpty(), "Main.qml failed to instantiate");
    auto* window = qobject_cast<QQuickWindow*>(roots.first());
    require(window != nullptr, "Main.qml root is not QQuickWindow");

    // 1. Min logic workspace bounds (960x540)
    window->setWidth(960);
    window->setHeight(540);
    window->show();
    for (int i = 0; i < 20; ++i) QCoreApplication::processEvents();

    require(window->width() >= 960, "window width violates 960 min constraint");
    require(window->height() >= 540, "window height violates 540 min constraint");

    // 2. Initial state verification
    require(window->property("leftExpanded").toBool() == true, "drawer initially not expanded");
    app.save(directory.path() + "/test.vrt");
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

    // 3. Drawer Collapse (toggleLeft)
    QMetaObject::invokeMethod(window, "toggleLeft");
    for (int i = 0; i < 20; ++i) QCoreApplication::processEvents();

    require(window->property("leftExpanded").toBool() == false, "drawer did not collapse");
    require(leftHost->width() == 28, "collapsed leftHost width != 28");
    require(leftHandle->implicitWidth() == 0, "collapsed leftHandle implicitWidth != 0");
    require(leftHandleMask->width() == 0, "collapsed leftHandleMask width != 0");
    require(!leftHandle->isVisible(), "collapsed leftHandle should be invisible");
    require(!leftHandle->isEnabled(), "collapsed leftHandle should be disabled");
    require(!leftContent->isVisible(), "collapsed leftContent should be hidden");

    auto* drawerToggle = window->findChild<QQuickItem*>("drawerToggle");
    require(drawerToggle != nullptr && drawerToggle->isVisible(), "drawerToggle missing or invisible");
    require(window->activeFocusItem() == drawerToggle, "focus did not switch to drawerToggle upon collapse");

    // 4. Invariant: running or browsing/folding does not dirty project or mutate graph
    require(app.dirty() == false, "collapsing drawer dirtied the project");

    // 5. Drawer Expand (toggleLeft)
    QMetaObject::invokeMethod(window, "toggleLeft");
    for (int i = 0; i < 20; ++i) QCoreApplication::processEvents();

    require(window->property("leftExpanded").toBool() == true, "drawer did not expand");
    require(leftHost->width() >= 260, "expanded leftHost width < 260");
    require(leftHandle->implicitWidth() == 1, "expanded leftHandle implicitWidth != 1");
    require(leftHandleMask->width() == 7, "expanded leftHandleMask width != 7");
    require(leftHandle->isVisible(), "expanded leftHandle should be visible");
    require(leftHandle->isEnabled(), "expanded leftHandle should be enabled");
    require(leftContent->isVisible(), "expanded leftContent not visible");

    auto* drawerCollapseButton = window->findChild<QQuickItem*>("drawerCollapseButton");
    require(drawerCollapseButton != nullptr && drawerCollapseButton->isVisible(), "drawerCollapseButton missing or invisible");
    require(window->activeFocusItem() == drawerCollapseButton, "focus did not switch to drawerCollapseButton upon expand");

    // 6. Independent ToolLibrary collapse & expand
    require(window->property("libraryExpanded").toBool() == true, "library initially not expanded");
    window->setProperty("libraryExpanded", false);
    for (int i = 0; i < 10; ++i) QCoreApplication::processEvents();
    require(window->property("libraryExpanded").toBool() == false, "library failed to collapse");

    window->setProperty("libraryExpanded", true);
    for (int i = 0; i < 10; ++i) QCoreApplication::processEvents();
    require(window->property("libraryExpanded").toBool() == true, "library failed to re-expand");

    // 7. Focus search (Ctrl+F)
    QMetaObject::invokeMethod(window, "focusSearch");
    for (int i = 0; i < 10; ++i) QCoreApplication::processEvents();
    require(window->property("leftExpanded").toBool() == true, "focusSearch did not ensure leftExpanded");

    // 8. Screenshot capture evidence
    QImage frame = window->grabWindow();
    require(!frame.isNull(), "grabWindow failed");
    require(frame.width() >= 960 && frame.height() >= 540, "frame dimensions smaller than logical workspace");
    std::filesystem::create_directories("build/evidence");
    frame.save("build/evidence/main_window_verified.png");
}
}
int main(int argc, char** argv) {
    QGuiApplication qt(argc, argv);
    try {
        treeContract(); outputContract(); pendingContract(); nodeDiagnosticsContract();
        mainQmlContract();
        std::cout << "workbench contract passed\n";
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n'; return 1;
    }
}
