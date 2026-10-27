#include "ui/AppController.h"
#include "WorkbenchFixtures.h"
#include "common/Version.h"
#include "ui/Theme.h"
#include "ui/OutputModel.h"
#include "ui/ImageViewport.h"
#include <quickui/Color.h>

#include <QClipboard>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QGuiApplication>
#include <QImage>
#include <QJSValue>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QMetaObject>
#include <QQmlContext>
#include <QQmlEngine>
#include <QQuickItem>
#include <QQuickWindow>
#include <QPointer>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QTest>
#include <memory>
#include <iostream>
#include <stdexcept>


namespace {
QQuickItem *visibleText(QQuickItem *item, const QString &text)
{
    if (item->isVisible() && item->property("text").toString() == text)
        return item;
    for (auto *child : item->childItems())
        if (auto *found = visibleText(child, text))
            return found;
    return nullptr;
}

// Output cells are pooled delegates that carry the row/column they currently represent. Looking a
// cell up by identity instead of by its visible text keeps this contract independent of formatting.
QQuickItem *outputCell(QQuickItem *item, const QString &portId, int column)
{
    const auto options = item->property("options").toMap();
    if (item->isVisible() && options.value("portId").toString() == portId
        && options.value("column").toInt() == column && options.contains("connectable"))
        return item;
    for (auto *child : item->childItems())
        if (auto *found = outputCell(child, portId, column))
            return found;
    return nullptr;
}

QQuickItem *waitForCell(QQuickItem *panel, const QString &portId, int column, int timeout = 3000)
{
    QPointer<QQuickItem> found;
    QTest::qWaitFor([&] {
        found = outputCell(panel, portId, column);
        return !found.isNull();
    }, timeout);
    return found.data();
}

void rightClick(QQuickWindow &window, QQuickItem *item)
{
    QTest::mouseClick(&window, Qt::RightButton, Qt::NoModifier,
                      item->mapToScene(QPointF(item->width() / 2, item->height() / 2)).toPoint());
}

void leftClick(QQuickWindow &window, QObject *object)
{
    auto *item = qobject_cast<QQuickItem *>(object);
    if (!item)
        throw std::runtime_error("menu action is not a visual item");
    QTest::mouseClick(&window, Qt::LeftButton, Qt::NoModifier,
                      item->mapToScene(QPointF(item->width() / 2, item->height() / 2)).toPoint());
}

QObject *findByProperty(QObject *root, const char *name, const QString &value)
{
    for (auto *child : root->findChildren<QObject *>())
        if (child->property(name).toString() == value)
            return child;
    return nullptr;
}

QObject *findByTextPrefix(QObject *root, const QString &prefix)
{
    for (auto *child : root->findChildren<QObject *>())
        if (child->property("text").toString().startsWith(prefix))
            return child;
    return nullptr;
}

bool hasWire(visionrt::AppController &app, const QString &source, const QString &target)
{
    for (const auto &wire : app.wires())
    {
        const auto entry = wire.toMap();
        if (entry.value("source").toString() == source && entry.value("target").toString() == target)
            return true;
    }
    return false;
}
}

void aboutDialogContract();

void popupSessionsContract()
{
    const auto require = [](bool condition, const char *message)
    {
        if (!condition)
            throw std::runtime_error(message);
    };
    visionrt::testing::WorkbenchInstallation installation;
    visionrt::AppController                  app(1, installation.directory());
    require(QTest::qWaitFor([&] { return !app.busy(); }, 20000), "popup catalog did not become ready");
    app.requestOpen(QStringLiteral(VISIONRT_SOURCE_DIR "/examples/pipeline_cpu_python.vrt.json"));
    app.resolveUnsaved(QStringLiteral("discard"));
    require(QTest::qWaitFor([&] { return !app.busy() && app.nodes().size() == 5; }, 5000),
            "popup contract could not open the real project");
    app.selectNode(QStringLiteral("1"));
    QQmlEngine engine;
    quickui::QuiColor::getInstance()->setThemeMode(quickui::QuiColor::Light);
    engine.addImportPath(QStringLiteral(VISIONRT_BINARY_DIR "/qml"));
    engine.rootContext()->setContextProperty("app", &app);
    QQuickWindow window;
    window.resize(900, 600);
    window.show();
    require(QTest::qWaitForWindowExposed(&window), "popup window was not exposed");
    QQmlComponent panelComponent(&engine, QUrl::fromLocalFile(QStringLiteral(VISIONRT_SOURCE_DIR "/src/ui/qml/OutputPanel.qml")));
    std::unique_ptr<QObject> panelOwner(panelComponent.create());
    if (!panelOwner)
        throw std::runtime_error(panelComponent.errorString().toStdString());
    auto *panel = qobject_cast<QQuickItem *>(panelOwner.get());
    require(panel != nullptr, "output panel is not a visual item");
    panel->setParentItem(window.contentItem());
    panel->setSize(QSizeF(900, 600));
    auto *menu     = panel->findChild<QObject *>(QStringLiteral("outputMenu"));
    auto *nameMenu = panel->findChild<QObject *>(QStringLiteral("outputNameMenu"));
    require(menu && nameMenu, "output panel has no stable copy or connection menu");
    require(panel->findChild<QObject *>(QStringLiteral("bindingPopup")) == nullptr,
            "the removed association popup is still instantiated");
    require(panel->findChild<QObject *>(QStringLiteral("outputDataDialog")) == nullptr,
            "the removed data viewer is still instantiated");

    // A name without any legal, unoccupied downstream input opens nothing at all: no placeholder row
    // and no connection menu that cannot do anything.
    // `paths` is an Array output and no downstream input accepts an Array, so this name has no
    // legal target at all.
    auto *pathsName = waitForCell(panel, QStringLiteral("paths"), 0);
    require(pathsName != nullptr, "paths name cell is missing");
    rightClick(window, pathsName);
    QTest::qWait(200);
    require(!nameMenu->property("opened").toBool() && !menu->property("opened").toBool(),
            "a name cell without a legal downstream input opened a menu");

    // The data column has exactly one action. Before execution the value does not exist, so copy is
    // visible but disabled instead of silently disappearing.
    auto *statusData = waitForCell(panel, QStringLiteral("__status_code"), 2);
    require(statusData != nullptr, "status code data cell is missing");
    rightClick(window, statusData);
    require(QTest::qWaitFor([&] { return menu->property("opened").toBool(); }, 1000),
            "data column did not open its menu");
    auto *copyAction = menu->findChild<QObject *>(QStringLiteral("outputCopyAction"));
    require(copyAction != nullptr && !copyAction->property("enabled").toBool(),
            "copy of an unexecuted output is not disabled");
    const auto dataActions = menu->property("count").toInt();
    require(dataActions == 1, "the data column still offers actions other than copy");
    QMetaObject::invokeMethod(menu, "close", Qt::DirectConnection);
    require(QTest::qWaitFor([&] { return !menu->property("visible").toBool(); }, 1000),
            "data menu did not close");

    // Run the real tool so copy works on a published value.
    QTemporaryDir directory;
    require(directory.isValid(), "popup image directory is invalid");
    const auto imagePath = directory.filePath(QStringLiteral("弹层预览.png"));
    QImage image(16, 12, QImage::Format_RGB888);
    image.fill(QColor(30, 90, 180));
    require(image.save(imagePath), "cannot write the real popup image fixture");
    require(app.setParameter(QStringLiteral("count"), false, QStringLiteral("1"))
                && app.setParameterPath(QStringLiteral("path"), false, QUrl::fromLocalFile(imagePath)),
            "cannot select the real image for result publication");
    auto *model = qobject_cast<visionrt::OutputModel *>(app.outputModel());
    require(model != nullptr, "output model is unavailable");
    app.runNode();
    require(QTest::qWaitFor([&] { return !app.busy() && model->capture(QStringLiteral("images"))
            && model->capture(QStringLiteral("images"))->publication > 0; }, 5000),
            "real image execution did not publish a new result");
    auto *imageData = waitForCell(panel, QStringLiteral("images"), 2);
    require(imageData != nullptr, "image data cell is missing after publication");
    const auto fullText = visionrt::OutputModel::fullText(*model->capture(QStringLiteral("images")));
    QGuiApplication::clipboard()->clear();
    QSignalSpy textReady(&app, &visionrt::AppController::outputTextReady);
    rightClick(window, imageData);
    require(QTest::qWaitFor([&] { return menu->property("opened").toBool(); }, 1000),
            "image data menu did not open");
    copyAction = menu->findChild<QObject *>(QStringLiteral("outputCopyAction"));
    require(copyAction != nullptr && copyAction->property("enabled").toBool(),
            "copy of a published value is disabled");
    leftClick(window, copyAction);
    require(QTest::qWaitFor([&] { return textReady.count() == 1; }, 5000),
            "copy never completed its bounded background readback");
    require(QGuiApplication::clipboard()->text() == fullText,
            "copy did not place the complete value on the clipboard");
    require(!menu->property("visible").toBool(), "copy left its context menu open");

    // The name column is the connection menu: it lists the free downstream inputs grouped by node,
    // and the entry it creates is the same relation the graph then contains.
    app.addNode(QStringLiteral("visionrt.image.crop"), 8, 8);
    require(QTest::qWaitFor([&] { return !app.busy(); }, 5000), "node instance synchronization did not finish");
    const auto target = app.selectedNode();
    require(app.nodes().size() == 6 && !target.isEmpty(), "cannot create the connection target");
    app.selectNode(QStringLiteral("1"));
    auto *imageName = waitForCell(panel, QStringLiteral("images"), 0);
    require(imageName != nullptr, "image name cell is missing");
    rightClick(window, imageName);
    require(QTest::qWaitFor([&] { return nameMenu->property("opened").toBool(); }, 1000),
            "name column did not open the connection menu");
    auto *targetMenu = findByProperty(nameMenu, "targetNode", target);
    require(targetMenu != nullptr, "connection menu does not group the compatible node");
    auto *imageInput = findByTextPrefix(targetMenu, QStringLiteral("图像"));
    require(imageInput != nullptr && imageInput->property("enabled").toBool(),
            "connection menu has no enabled image input for the free target");
    require(QMetaObject::invokeMethod(imageInput, "triggered", Qt::DirectConnection), "cannot trigger the connection menu entry");
    require(QTest::qWaitFor([&] { return hasWire(app, QStringLiteral("1"), target); }, 1000),
            "the connection menu entry did not create the relation");
    require(QTest::qWaitFor([&] { return !nameMenu->property("visible").toBool(); }, 1000),
            "the connection menu stayed open after committing a relation");
    rightClick(window, waitForCell(panel, QStringLiteral("images"), 0));
    require(QTest::qWaitFor([&] { return nameMenu->property("opened").toBool(); }, 1000),
            "connection menu did not reopen for the connected output");
    targetMenu = findByProperty(nameMenu, "targetNode", target);
    require(targetMenu != nullptr, "connected target disappeared from the connection menu");
    auto *connected = findByTextPrefix(targetMenu, QStringLiteral("✓"));
    require(connected != nullptr && connected->property("enabled").toBool(),
            "connected input is not offered for explicit disconnection");
    require(QMetaObject::invokeMethod(connected, "triggered", Qt::DirectConnection), "cannot trigger the connected entry");
    require(QTest::qWaitFor([&] { return !hasWire(app, QStringLiteral("1"), target); }, 1000),
            "triggering the connected entry did not disconnect it");

    // An output context stays bound to its node even when the same port id exists elsewhere.
    const auto oldOutput = app.portContext(QStringLiteral("images"), true);
    app.selectNode(QStringLiteral("4"));
    require(!app.bindOutputTo(oldOutput, target, QStringLiteral("images")),
            "an obsolete output context connects the newly selected source");
    app.selectNode(QStringLiteral("1"));
    const auto current = app.portContext(QStringLiteral("images"), true);
    require(app.bindOutputTo(current, target, QStringLiteral("images")),
            "a current output context cannot connect a compatible input");
    const auto occupied = app.portContext(QStringLiteral("images"), true);
    require(!app.bindOutputTo(occupied, target, QStringLiteral("images")),
            "output connection replaced an occupied input");
    require(app.diagnostic() == QStringLiteral("输入端口已关联上游：%1.images").arg(target),
            "occupied input rejection lost its precise reason or stable target identity");
    require(!app.bindOutputTo(occupied, target, QStringLiteral("rect")),
            "output connection accepted an incompatible input");
    require(app.diagnostic().contains(QStringLiteral("类型不兼容")) && app.diagnostic().contains(target + ".rect"),
            "incompatible input rejection lost its precise reason or stable target identity");
    require(!app.bindOutputTo(occupied, QStringLiteral("not-a-node"), QStringLiteral("images"))
                && app.diagnostic() == QStringLiteral("节点标识 必须是无前导零的无符号十进制文本：not-a-node"),
            "invalid output target lost its original node identifier or concrete Chinese reason");

    // Reopening a project invalidates every captured context without touching the new graph.
    const auto openProject = [&]
    {
        app.requestOpen(QStringLiteral(VISIONRT_SOURCE_DIR "/examples/pipeline_cpu_python.vrt.json"));
        app.resolveUnsaved(QStringLiteral("discard"));
        require(QTest::qWaitFor([&] { return !app.busy() && app.nodes().size() == 5; }, 5000),
                "cannot reopen popup project");
        app.selectNode(QStringLiteral("1"));
    };
    openProject();
    const auto previousProject = app.portContext(QStringLiteral("images"), true);
    openProject();
    require(!app.portContextCurrent(previousProject),
            "reopening the same project leaves old output contexts authorized");
    const auto reopenedWires = app.wires();
    require(!app.unbindOutputFrom(previousProject, QStringLiteral("3"), QStringLiteral("images"))
                && app.wires() == reopenedWires,
            "an obsolete project context disconnected the reopened project's matching input");
    QSignalSpy projectText(&app, &visionrt::AppController::outputTextReady);
    app.requestOutputText(previousProject, true, false);
    QTest::qWait(60);
    require(projectText.isEmpty(), "obsolete output context reads a new project's value");
    app.requestOutputText(app.portContext(QStringLiteral("__status_message"), true), true, false);
    app.selectNode(QStringLiteral("3"));
    QTest::qWait(150);
    require(projectText.isEmpty(), "late output copy completes on a different selected node");
    app.selectNode(QStringLiteral("1"));

    // A model reset closes the open menu instead of leaving it bound to a destroyed row.
    rightClick(window, waitForCell(panel, QStringLiteral("__status_code"), 2));
    require(QTest::qWaitFor([&] { return menu->property("opened").toBool(); }, 1000),
            "status data menu did not open for the reset case");
    model->clear();
    require(QTest::qWaitFor([&] { return !menu->property("visible").toBool(); }, 1000),
            "real output model reset leaves a detached context menu");

    visionrt::ImageViewport viewport(window.contentItem());
    viewport.setWidth(300);
    viewport.setHeight(280);
    app.attachViewport(&viewport);
    app.selectNode(QStringLiteral("3"));
    app.endRoiPreview();
    window.hide();

    aboutDialogContract();
}

void aboutDialogContract()
{
    const auto require = [](bool condition, const char *message)
    {
        if (!condition)
            throw std::runtime_error(message);
    };

    visionrt::testing::WorkbenchInstallation installation;
    visionrt::AppController app(1, installation.directory());
    require(QTest::qWaitFor([&] { return !app.busy(); }, 10000), "catalog did not become ready");

    // 1. Check aboutText structure
    const QString about = app.aboutText();
    require(!about.isEmpty(), "aboutText is empty");
    require(about.contains("VisionRT"), "aboutText missing VisionRT header");
    require(about.contains("VisionTools"), "aboutText missing VisionTools header");
    require(about.contains("VisionRTContracts"), "aboutText missing VisionRTContracts header");
    require(about.contains("运行环境"), "aboutText missing 运行环境 header");
    require(about.contains("Qt"), "aboutText missing Qt line");
    require(about.contains("Python"), "aboutText missing Python line");
    require(about.contains("Copyright © VisionRT contributors"), "aboutText missing Copyright");

    // Check 40-char commit SHA for VisionRT and Contracts
    const auto contracts = visionrt::GetContractsVersion();
    require(!contracts.commit.empty() && contracts.commit.length() == 40, "Contracts commit is not 40 chars");
    require(!contracts.version.empty(), "Contracts version is empty");
    require(about.contains(QString::fromStdString(contracts.commit)), "aboutText does not contain Contracts commit");

    const std::string hostCommit = visionrt::GetCommitHashString();
    require(!hostCommit.empty() && hostCommit.length() == 40, "VisionRT commit is not 40 chars");
    require(about.contains(QString::fromStdString(hostCommit)), "aboutText does not contain VisionRT commit");

    // 2. Check copyAboutText
    QGuiApplication::clipboard()->clear();
    app.copyAboutText();
    require(QGuiApplication::clipboard()->text() == about, "copyAboutText did not match aboutText exactly");

    // 3. Test uninstalled tools package
    {
        QTemporaryDir emptyDir;
        visionrt::AppController emptyApp(1, emptyDir.path());
        require(QTest::qWaitFor([&] { return !emptyApp.busy(); }, 5000), "emptyApp did not settle");
        const QString emptyAbout = emptyApp.aboutText();
        require(emptyAbout.contains(QStringLiteral("VisionTools\n状态          未安装")), "uninstalled package did not show '状态          未安装'");
    }

    // 4. Test missing build_info.json
    {
        QTemporaryDir missingInfoDir;
        QDir root(missingInfoDir.path());
        root.mkpath("tools-package");
        QFile file(root.filePath("tools-package/module_index.json"));
        require(file.open(QIODevice::WriteOnly), "cannot create module_index.json fixture");
        file.write("{\"schema_version\":1,\"tools\":[],\"modules\":[],\"runtime_files\":[]}");
        file.close();

        visionrt::AppController missingApp(1, missingInfoDir.path());
        require(QTest::qWaitFor([&] { return !missingApp.busy(); }, 5000), "missingApp did not settle");
        const QString missingAbout = missingApp.aboutText();
        require(missingAbout.contains(QStringLiteral("VisionTools\n状态          构建信息不可用")), "missing build_info did not show '状态          构建信息不可用'");
    }

    // 5. QML AboutVisionRTDialog UI verification
    QQmlEngine engine;
    quickui::QuiColor::getInstance()->setThemeMode(quickui::QuiColor::Light);
    engine.addImportPath(QStringLiteral(VISIONRT_BINARY_DIR "/qml"));
    engine.rootContext()->setContextProperty("app", &app);

    QQuickWindow window;
    window.resize(800, 600);
    window.show();
    require(QTest::qWaitForWindowExposed(&window), "dialog window not exposed");

    QQmlComponent dialogComp(&engine);
    dialogComp.setData(
        "import QtQuick\nimport quickui\nimport \".\"\n"
        "Item {\n"
        "    id: testRoot\n"
        "    anchors.fill: parent\n"
        "    AboutVisionRTDialog { id: about; objectName: \"aboutDialog\" }\n"
        "}\n",
        QUrl::fromLocalFile(QStringLiteral(VISIONRT_SOURCE_DIR "/src/ui/qml/AboutTestRoot.qml")));
    std::unique_ptr<QObject> rootOwner(dialogComp.create());
    if (!rootOwner)
        throw std::runtime_error(dialogComp.errorString().toStdString());

    auto *rootItem = qobject_cast<QQuickItem *>(rootOwner.get());
    rootItem->setParentItem(window.contentItem());

    auto *dialog = rootItem->findChild<QObject *>("aboutDialog");
    require(dialog != nullptr, "aboutDialog not found in test root");

    QMetaObject::invokeMethod(dialog, "open");
    require(QTest::qWaitFor([&] { return dialog->property("visible").toBool(); }, 2000), "About dialog did not open");

    // Verify TextArea content
    auto *textArea = dialog->findChild<QObject *>("aboutTextArea");
    require(textArea != nullptr, "aboutTextArea is missing in dialog");
    require(textArea->property("text").toString() == about, "aboutTextArea text does not match app.aboutText");
    require(textArea->property("readOnly").toBool(), "aboutTextArea is not readOnly");
    require(textArea->property("selectByMouse").toBool(), "aboutTextArea does not have selectByMouse");

    // Verify context menu exists
    auto *aboutMenu = dialog->findChild<QObject *>("aboutMenu");
    require(aboutMenu != nullptr, "aboutMenu is missing in AboutVisionRTDialog");

    // Verify '复制全部' neutral button copies without closing
    QGuiApplication::clipboard()->clear();
    const auto neutralListener = dialog->property("onNeutralClickListener").value<QJSValue>();
    require(neutralListener.isCallable(), "onNeutralClickListener is not callable");
    neutralListener.call();
    require(QGuiApplication::clipboard()->text() == about, "neutral button did not copy aboutText");
    require(dialog->property("visible").toBool(), "neutral button closed the About dialog");

    // Verify '关闭' closes dialog
    QMetaObject::invokeMethod(dialog, "close");
    require(QTest::qWaitFor([&] { return !dialog->property("visible").toBool(); }, 2000), "close did not close About dialog");

    std::cout << "[Evidence About UI] AboutVisionRTDialog verified: 3-component versions, 40-char SHAs, clipboard copy, uninstalled state, missing metadata state, and QML dialog actions.\n";
}

void outputCellReuseContract()
{
    const auto require = [](bool condition, const char *message)
    {
        if (!condition) throw std::runtime_error(message);
    };
    QTemporaryDir manyDirectory;
    require(manyDirectory.isValid(), "many-output fixture directory is invalid");
    const auto package = manyDirectory.path() + "/tools-package/";
    require(QDir().mkpath(package + "tools/custom"), "cannot create the many-output package");
    const auto write = [&](const QString &path, const QByteArray &bytes)
    {
        QFile file(package + path);
        require(QDir().mkpath(QFileInfo(file).absolutePath()), "cannot create fixture parent directory");
        require(file.open(QIODevice::WriteOnly) && file.write(bytes) == bytes.size(), "cannot write the many-output fixture");
    };
    write("module_index.json", R"json({
  "schema_version": 1,
  "variant": "test",
  "modules": [],
  "tools": [
    {"tool_id": "test.outputs", "definition": "tools/custom/outputs.json"}
  ],
  "runtime_files": []
})json");
    write("python/custom/outputs/outputs.py",
          "def run(inputs):\n"
          "    values = {'value_' + str(index): index for index in range(64)}\n"
          "    values.update(__status_code=0, __status_message='')\n"
          "    return values\n");
    QJsonObject definition{
        {"schema_version",                                                                              4},
        {       "tool_id",                                                                 "test.outputs"},
        {  "display_name",                                                                 "Many outputs"},
        {       "version",                                                                        "1.0.0"},
        {          "kind",                                                              "python_function"},
        {         "entry", QJsonObject{{"package", "outputs"}, {"file", "outputs.py"}, {"symbol", "run"}}},
        {   "init_params",                                                                   QJsonArray{}},
        {        "inputs",                                                                   QJsonArray{}}
    };
    QJsonArray ports;
    for (int number = 0; number < 64; ++number)
        ports.append(QJsonObject{{"id", "value_" + QString::number(number)},
            {"name", "Value " + QString::number(number).rightJustified(2, '0')}, {"type", "Int"}});
    definition["outputs"] = ports;
    write("tools/custom/outputs.json", QJsonDocument(definition).toJson());
    visionrt::AppController manyApp(1, manyDirectory.path());
    require(QTest::qWaitFor([&] { return !manyApp.busy(); }, 5000), "many-output catalog did not become ready");
    manyApp.addNode("test.outputs", 0, 0);
    require(QTest::qWaitFor([&] { return !manyApp.busy(); }, 5000), "node instance synchronization did not finish");
    require(!manyApp.selectedNode().isEmpty(), "many-output tool was not discovered");
    manyApp.runNode();
    require(QTest::qWaitFor([&] { return !manyApp.busy(); }, 5000), "many-output tool did not finish");
    auto *manyModel = qobject_cast<visionrt::OutputModel *>(manyApp.outputModel());
    require(manyModel && manyModel->rowCount() >= 64, "many-output fixture did not expose its declared ports");
    const auto manyStatus = manyModel->capture("__status_code");
    if (!manyStatus || visionrt::OutputModel::fullText(*manyStatus) != "0")
        throw std::runtime_error(("many-output execution failed: "
            + visionrt::OutputModel::fullText(*manyModel->capture("__status_message"))).toStdString());
    const auto firstValue = manyModel->capture("value_0");
    const auto *firstInteger = firstValue ? std::get_if<std::int64_t>(&firstValue->value) : nullptr;
    require(firstInteger && *firstInteger == 0,
            "many-output fixture did not publish its real Python values");
    QQmlEngine manyEngine;
    manyEngine.addImportPath(QStringLiteral(VISIONRT_BINARY_DIR "/qml"));
    manyEngine.rootContext()->setContextProperty("app", &manyApp);
    visionrt::registerStyleModule();
    QQmlComponent manyComponent(&manyEngine, QUrl::fromLocalFile(QStringLiteral(VISIONRT_SOURCE_DIR "/src/ui/qml/OutputPanel.qml")));
    std::unique_ptr<QObject> manyOwner(manyComponent.create());
    if (!manyOwner) throw std::runtime_error(manyComponent.errorString().toStdString());
    auto *manyPanel = qobject_cast<QQuickItem *>(manyOwner.get());
    require(manyPanel != nullptr, "many-output panel is not a visual item");
    QQuickWindow manyWindow;
    manyWindow.resize(760, 250);
    manyPanel->setParentItem(manyWindow.contentItem());
    manyPanel->setSize(QSizeF(760, 250));
    manyWindow.show();
    manyWindow.requestActivate();
    require(QTest::qWaitForWindowActive(&manyWindow), "many-output window did not activate");
    QPointer<QQuickItem> firstLabel;
    require(QTest::qWaitFor([&] {
        firstLabel = visibleText(manyPanel, "Value 00");
        return !firstLabel.isNull();
    }, 1000), "many-output table has no first value cell");
    QPointer<QQuickItem> sourceCell = firstLabel;
    while (sourceCell && !sourceCell->property("currentTableView").isValid())
        sourceCell = sourceCell->parentItem();
    require(sourceCell != nullptr, "source output label has no TableView delegate");
    const auto sourcePort = sourceCell->property("options").toMap().value("portId").toString();
    require(sourcePort == "value_0", "source delegate does not identify the clicked output port");
    QQmlComponent monitorComponent(&manyEngine);
    monitorComponent.setData(
        "import QtQuick\nQtObject { id: monitor; property Item cell; property int pooledCount: 0; property int reusedCount: 0; "
        "property Connections listener: Connections { target: monitor.cell.TableView; "
        "function onPooled() { monitor.pooledCount++ } function onReused() { monitor.reusedCount++ } } }",
        QUrl::fromLocalFile(QStringLiteral(VISIONRT_SOURCE_DIR "/tests/OutputCellReuse.qml")));
    require(QTest::qWaitFor([&] { return monitorComponent.status() != QQmlComponent::Loading; }, 1000),
            "output cell reuse monitor did not finish loading");
    std::unique_ptr<QObject> monitor(monitorComponent.createWithInitialProperties({{"cell", QVariant::fromValue(sourceCell.data())}}));
    if (!monitor) throw std::runtime_error(monitorComponent.errorString().toStdString());
    auto *manyMenu  = manyPanel->findChild<QObject *>("outputMenu");
    auto *manyTable = manyPanel->findChild<QObject *>("outputTable");
    require(manyMenu && manyTable, "many-output menu or table is unavailable");
    auto *valueData = waitForCell(manyPanel, "value_0", 2);
    require(valueData != nullptr, "many-output data cell is missing");
    rightClick(manyWindow, valueData);
    require(QTest::qWaitFor([&] { return manyMenu->property("opened").toBool(); }, 1000),
            "many-output source menu did not open");
    const auto sourceContext = manyPanel->property("selectedContext").toMap();
    require(sourceContext == manyApp.portContext("value_0", true), "source menu did not capture the clicked port");
    const double lastOffset = manyTable->property("contentHeight").toDouble() - manyTable->property("viewportHeight").toDouble();
    require(lastOffset > 500, "many-output table is not large enough to exercise pooling");
    require(manyTable->setProperty("contentY", lastOffset), "cannot scroll the real output table");
    require(QTest::qWaitFor([&] {
        return sourceCell && sourceCell->property("options").toMap().value("portId").toString() != sourcePort
            && monitor->property("pooledCount").toInt() > 0 && monitor->property("reusedCount").toInt() > 0;
    }, 1000), "the source cell was not demonstrably reused for another output port");
    require(manyMenu->property("opened").toBool()
                && manyMenu->property("parent").value<QQuickItem *>() == manyPanel
                && manyPanel->property("selectedContext").toMap() == sourceContext,
            "cell reuse detached the menu or rebound it to the recycled row");
    QGuiApplication::clipboard()->clear();
    QSignalSpy copiedText(&manyApp, &visionrt::AppController::outputTextReady);
    auto *copyAction = manyMenu->findChild<QObject *>("outputCopyAction");
    require(copyAction != nullptr && copyAction->property("enabled").toBool(),
            "reused-cell menu has no enabled copy action");
    leftClick(manyWindow, copyAction);
    require(QTest::qWaitFor([&] { return copiedText.count() == 1; }, 3000)
                && QGuiApplication::clipboard()->text() == QStringLiteral("0")
                && !manyMenu->property("visible").toBool(),
            "reused-cell copy read another row instead of its captured output value");
    std::cout << "[Output cell reuse] A real pooled cell changed ports while its stable menu retained the original source and value.\n";
}
