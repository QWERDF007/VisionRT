#include "ui/AppController.h"
#include "ui/Theme.h"
#include "ui/OutputModel.h"
#include "ui/ImageViewport.h"
#include <quickui/Color.h>

#include <QQmlComponent>
#include <QDir>
#include <QFile>
#include <QImage>
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
}

void popupSessionsContract()
{
    const auto require = [](bool condition, const char *message)
    {
        if (!condition)
            throw std::runtime_error(message);
    };
    visionrt::AppController app(1, QStringLiteral(VISIONRT_BINARY_DIR));
    require(QTest::qWaitFor([&] { return !app.busy(); }, 5000), "popup catalog did not become ready");
    app.requestOpen(QStringLiteral(VISIONRT_SOURCE_DIR "/examples/pipeline_cpu_python.vrt.json"));
    app.resolveUnsaved(QStringLiteral("discard"));
    require(QTest::qWaitFor([&] { return !app.busy() && app.nodes().size() == 5; }, 5000),
            "popup contract could not open the real project");
    app.selectNode(QStringLiteral("1"));
    QQmlEngine engine;
    quickui::QuiColor::getInstance()->setThemeMode(quickui::QuiColor::Light);
    engine.addImportPath(QStringLiteral(VISIONRT_BINARY_DIR "/qml"));
    engine.rootContext()->setContextProperty("app", &app);
    engine.rootContext()->setContextProperty("Theme", visionrt::Theme::instance());
    QQuickWindow window;
    window.resize(760, 560);
    window.show();
    require(QTest::qWaitForWindowExposed(&window), "popup window was not exposed");
    QQmlComponent component(&engine, QUrl::fromLocalFile(QStringLiteral(VISIONRT_SOURCE_DIR "/src/ui/qml/BindingPopup.qml")));
    std::unique_ptr<QObject> popup(component.create());
    if (!popup)
        throw std::runtime_error(component.errorString().toStdString());
    require(popup->setProperty("parent", QVariant::fromValue(window.contentItem())), "popup parent was not assigned");
    const auto openOutput = [&]
    {
        require(QMetaObject::invokeMethod(popup.get(), "openOutput", Q_ARG(QVariant, app.portContext(QStringLiteral("images"), true)),
                                         Q_ARG(QVariant, QStringLiteral("图像批次"))), "cannot open output association selector");
        require(QTest::qWaitFor([&] { return popup->property("opened").toBool(); }, 1000),
                "output association selector did not open");
    };
    openOutput();
    QQuickWindow otherWindow;
    otherWindow.resize(180, 100);
    otherWindow.show();
    otherWindow.requestActivate();
    require(QTest::qWaitForWindowActive(&otherWindow, 1000), "cannot activate another real Qt window");
    require(QTest::qWaitFor([&] { return !popup->property("visible").toBool(); }, 1000),
            "output association selector survives deactivating its window");
    otherWindow.close();
    window.requestActivate();
    require(QTest::qWaitForWindowActive(&window, 1000), "cannot reactivate the popup window");
    openOutput();
    app.selectNode(QStringLiteral("3"));
    require(QTest::qWaitFor([&] { return !popup->property("visible").toBool(); }, 1000),
            "output association selector survives switching away from its source node");

    const auto oldInput = app.portContext(QStringLiteral("images"));
    require(!oldInput.isEmpty(), "crop input has no stable context");
    app.selectNode(QStringLiteral("5"));
    const auto unchangedWires = app.wires();
    QSignalSpy changes(&app, &visionrt::AppController::parametersChanged);
    require(!app.bindInput(oldInput, QStringLiteral("4"), QStringLiteral("images"))
                && !app.unbindInput(oldInput) && app.wires() == unchangedWires && changes.isEmpty(),
            "an obsolete input context writes to the newly selected node");

    app.addNode(QStringLiteral("visionrt.image.crop"), 8, 8);
    const auto target = app.selectedNode();
    require(app.nodes().size() == 6 && !target.isEmpty(), "cannot create the output association target");
    app.selectNode(QStringLiteral("1"));
    const auto oldOutput = app.portContext(QStringLiteral("images"), true);
    app.selectNode(QStringLiteral("4"));
    require(!app.bindOutputTo(oldOutput, target, QStringLiteral("images")),
            "an obsolete output context connects the newly selected source");
    app.selectNode(QStringLiteral("1"));
    require(app.bindOutputTo(app.portContext(QStringLiteral("images"), true), target, QStringLiteral("images")),
            "a current output context cannot connect a compatible input");
    bool connected = false;
    for (const auto &wire : app.wires())
    {
        const auto entry = wire.toMap();
        if (entry.value("source").toString() == "1" && entry.value("target").toString() == target)
            connected = true;
    }
    require(connected, "output association did not preserve the captured source identity");
    openOutput();
    const auto sourceContext = app.portContext(QStringLiteral("images"), true);
    const auto beforeRejectedBinding = app.wires();
    QSignalSpy rejectedProjectChanges(&app, &visionrt::AppController::projectChanged);
    QSignalSpy rejectedParameterChanges(&app, &visionrt::AppController::parametersChanged);
    require(!app.bindOutputTo(sourceContext, target, QStringLiteral("images"))
                && !app.bindOutputTo(sourceContext, target, QStringLiteral("rect"))
                && app.wires() == beforeRejectedBinding && rejectedProjectChanges.isEmpty()
                && rejectedParameterChanges.isEmpty() && popup->property("visible").toBool()
                && popup->property("context").toMap() == sourceContext,
            "a rejected output connection changed bindings or dismissed its current selector");
    app.renameNode(target, QStringLiteral("输出断开目标"));
    require(QTest::qWaitFor([&]
    {
        if (!popup->property("visible").toBool())
            return true;
        for (const auto &entry : popup->property("candidates").toList())
            if (entry.toMap().value("node").toString() == target
                && entry.toMap().value("title").toString() == QStringLiteral("输出断开目标-") + target)
                return true;
        return false;
    }, 1000), "output target candidates retained an obsolete node title");
    if (!popup->property("visible").toBool())
        openOutput();
    const auto connectedTargets = app.relationTargets(app.portContext(QStringLiteral("images"), true));
    bool connectedTargetListed = false;
    for (const auto &entry : connectedTargets)
    {
        const auto relation = entry.toMap();
        if (relation.value("node").toString() == target && relation.value("port").toString() == "images")
            connectedTargetListed = relation.value("connected").toBool();
    }
    require(connectedTargetListed, "output association selector omits its existing connected input");
    QPointer<QQuickItem> disconnect;
    require(QTest::qWaitFor([&]
    {
        QList<QQuickItem *> pending{window.contentItem()};
        while (!pending.isEmpty())
        {
            auto *item = pending.takeLast();
            if (item->objectName() == QStringLiteral("disconnectOutput_") + target + "_images"
                && item->isVisible() && item->isEnabled())
            {
                disconnect = item;
                return true;
            }
            pending.append(item->childItems());
        }
        return false;
    }, 1000),
            "connected output target has no visible disconnect action");
    const auto beforeDisconnect = app.wires();
    QVariantList retainedWires;
    for (const auto &wire : beforeDisconnect)
        if (wire.toMap().value("target").toString() != target)
            retainedWires.push_back(wire);
    const auto imageReferences = [&]
    {
        auto *model = app.outputModel();
        for (int row = 0; row < model->rowCount(); ++row)
            if (model->data(model->index(row, 0), visionrt::OutputModel::PortIdRole).toString() == "images")
                return model->data(model->index(row, 0), visionrt::OutputModel::CountRole).toInt();
        return -1;
    };
    if (imageReferences() != 4)
        throw std::runtime_error("connected output reference count: " + std::to_string(imageReferences())
                                 + "; active relations: " + std::to_string(beforeDisconnect.size()));
    QDir().mkpath(QStringLiteral(VISIONRT_BINARY_DIR "/evidence"));
    require(window.grabWindow().save(QStringLiteral(VISIONRT_BINARY_DIR "/evidence/output_connections.png")),
            "cannot capture the connected output targets");
    QTest::mouseClick(&window, Qt::LeftButton, Qt::NoModifier,
                      disconnect->mapToScene(QPointF(disconnect->width() / 2, disconnect->height() / 2)).toPoint());
    require(QTest::qWaitFor([&] { return !popup->property("visible").toBool(); }, 1000)
                && app.wires().size() == beforeDisconnect.size() - 1,
            "disconnecting one output target left the selector open or removed the wrong number of relations");
    require(app.wires() == retainedWires && imageReferences() == 3,
            "output disconnect changed an unrelated relation or left a stale reference count");
    app.selectNode(target);
    bool disconnected = false;
    for (const auto &entry : app.parameters())
        if (entry.toMap().value("id").toString() == "images")
            disconnected = entry.toMap().value("sourceNode").toString().isEmpty();
    require(disconnected, "output disconnect did not clear the exact target input");
    app.selectNode(QStringLiteral("1"));
    require(app.bindOutputTo(app.portContext(QStringLiteral("images"), true), target, QStringLiteral("images")),
            "output target could not reconnect after explicit disconnection");
    const auto replacedSourceContext = app.portContext(QStringLiteral("images"), true);
    app.selectNode(target);
    require(!app.unbindOutputFrom(replacedSourceContext, target, QStringLiteral("images")),
            "an obsolete source context disconnected an input after changing selection");
    require(app.bindInput(app.portContext(QStringLiteral("images")), QStringLiteral("4"), QStringLiteral("images")),
            "cannot replace the target input with a different compatible source");
    app.selectNode(QStringLiteral("1"));
    const auto replacementWires = app.wires();
    QSignalSpy replacementChanges(&app, &visionrt::AppController::projectChanged);
    require(!app.unbindOutputFrom(app.portContext(QStringLiteral("images"), true), target, QStringLiteral("images"))
                && app.wires() == replacementWires && replacementChanges.isEmpty(),
            "disconnecting the former output removed a replacement connection");
    for (const auto &entry : app.relationTargets(app.portContext(QStringLiteral("images"), true)))
        require(entry.toMap().value("node").toString() != target
                    || entry.toMap().value("port").toString() != "images",
                "the former source still lists an input occupied by a different output");

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
    auto previousIdentity = previousProject;
    auto currentIdentity = app.portContext(QStringLiteral("images"), true);
    previousIdentity.remove("project");
    currentIdentity.remove("project");
    require(previousIdentity == currentIdentity && !app.portContextCurrent(previousProject),
            "reopening the same project leaves old popup contexts authorized");
    const auto reopenedWires = app.wires();
    require(!app.unbindOutputFrom(previousProject, QStringLiteral("3"), QStringLiteral("images"))
                && app.wires() == reopenedWires,
            "an obsolete project context disconnected the reopened project's matching input");
    QSignalSpy textReady(&app, &visionrt::AppController::outputTextReady);
    app.requestOutputText(previousProject, false);
    QTest::qWait(60);
    require(textReady.isEmpty(), "obsolete output context reads a new project's value");
    app.requestOutputText(app.portContext(QStringLiteral("__status_message"), true), false);
    app.selectNode(QStringLiteral("3"));
    QTest::qWait(150);
    require(textReady.isEmpty(), "late output text opens on a different selected node");
    app.selectNode(QStringLiteral("1"));

    QQmlComponent panelComponent(&engine, QUrl::fromLocalFile(QStringLiteral(VISIONRT_SOURCE_DIR "/src/ui/qml/OutputPanel.qml")));
    std::unique_ptr<QObject> panelOwner(panelComponent.create());
    if (!panelOwner)
        throw std::runtime_error(panelComponent.errorString().toStdString());
    auto *panel = qobject_cast<QQuickItem *>(panelOwner.get());
    require(panel != nullptr, "output panel is not a visual item");
    panel->setParentItem(window.contentItem());
    panel->setSize(QSizeF(760, 560));
    auto *menu = panel->findChild<QObject *>(QStringLiteral("outputMenu"));
    auto *selector = panel->findChild<QObject *>(QStringLiteral("bindingPopup"));
    require(menu && selector, "output panel has no stable menu or association selector");
    const auto openMenu = [&]
    {
        QQuickItem *label = nullptr;
        require(QTest::qWaitFor([&] { label = visibleText(panel, QStringLiteral("图像批次")); return label != nullptr; }, 1000),
                "real output table has no image output cell");
        QTest::mouseClick(&window, Qt::RightButton, Qt::NoModifier,
                          label->mapToScene(QPointF(label->width() / 2, label->height() / 2)).toPoint());
        require(QTest::qWaitFor([&] { return menu->property("opened").toBool(); }, 1000), "output context menu did not open");
        require(menu->property("parent").value<QQuickItem *>() == panel,
                "output menu is parented to an ephemeral table cell");
    };
    openMenu();
    auto *model = qobject_cast<visionrt::OutputModel *>(app.outputModel());
    require(model != nullptr, "output model is unavailable");
    QSignalSpy reset(model, &QAbstractItemModel::modelReset);
    QTest::qWait(150);
    require(reset.isEmpty() && menu->property("opened").toBool(), "idle snapshots reset or close the active output menu");
    model->clear();
    require(QTest::qWaitFor([&] { return !menu->property("visible").toBool(); }, 1000),
            "real output model reset leaves a detached context menu");
    app.selectNode(QStringLiteral("3"));
    app.selectNode(QStringLiteral("1"));
    openMenu();
    auto *associate = visibleText(window.contentItem(), QStringLiteral("关联已有节点…"));
    require(associate != nullptr, "association action is missing");
    QSignalSpy closed(menu, SIGNAL(closed()));
    QTest::mouseClick(&window, Qt::LeftButton, Qt::NoModifier,
                      associate->mapToScene(QPointF(associate->width() / 2, associate->height() / 2)).toPoint());
    require(QTest::qWaitFor([&] { return selector->property("opened").toBool(); }, 1000),
            "output menu did not hand off to the association selector");
    require(closed.count() == 1 && !menu->property("visible").toBool(), "selector opened before the source menu actually closed");
    require(QDir(QStringLiteral(VISIONRT_BINARY_DIR)).mkpath("evidence"), "cannot create popup evidence directory");
    require(window.grabWindow().save(QStringLiteral(VISIONRT_BINARY_DIR "/evidence/workbench_popup_binding.png")),
            "cannot capture the real output association selector");
    app.selectNode(QStringLiteral("3"));
    require(QTest::qWaitFor([&] { return !selector->property("visible").toBool(); }, 1000),
            "panel association selector survives a target change");

    app.selectNode(QStringLiteral("1"));
    QTemporaryDir directory;
    require(directory.isValid(), "popup image directory is invalid");
    const auto imagePath = directory.filePath(QStringLiteral("弹层预览.png"));
    QImage image(16, 12, QImage::Format_RGB888);
    image.fill(QColor(30, 90, 180));
    require(image.save(imagePath), "cannot write the real popup image fixture");
    require(app.setParameter(QStringLiteral("mode"), false, QStringLiteral("file"))
                && app.setParameterPath(QStringLiteral("path"), false, QUrl::fromLocalFile(imagePath)),
            "cannot select the real image for result publication");
    const auto beforeRun = app.portContext(QStringLiteral("images"), true);
    openMenu();
    app.runNode();
    require(QTest::qWaitFor([&] { return !app.busy() && model->capture(QStringLiteral("images"))
            && model->capture(QStringLiteral("images"))->publication > 0; }, 5000),
            "real image execution did not publish a new result");
    require(!app.portContextCurrent(beforeRun), "new publication keeps an obsolete output context authorized");
    require(QTest::qWaitFor([&] { return !menu->property("visible").toBool(); }, 1000),
            "new publication leaves its old output menu visible");
    const auto captured = model->capture(QStringLiteral("images"));
    const auto *batch = captured ? std::get_if<visionrt::ImageBatch>(&captured->value) : nullptr;
    if (!batch || batch->images().size() != 1)
        throw std::runtime_error("real image publication has no image batch: "
            + visionrt::OutputModel::fullText(*model->capture(QStringLiteral("__status_message"))).toStdString());
    app.requestOutputText(app.portContext(QStringLiteral("__status_code"), true), false);
    require(QTest::qWaitFor([&] { return textReady.count() == 1; }, 1000)
                && textReady.last().front().toString() == QStringLiteral("0"),
            "current output context cannot display the real execution status");

    visionrt::ImageViewport viewport(window.contentItem());
    viewport.setWidth(300);
    viewport.setHeight(280);
    app.attachViewport(&viewport);
    app.selectNode(QStringLiteral("3"));
    const auto beforeRoi = app.portContext(QStringLiteral("images"));
    openOutput();
    require(app.editRoi(QStringLiteral("rect"), false), "cannot enter the real crop ROI editor");
    require(QTest::qWaitFor([&] { return !popup->property("visible").toBool(); }, 1000),
            "output association selector remains open when ROI editing starts");
    require(!app.portContextCurrent(beforeRoi) && !app.unbindInput(beforeRoi),
            "a pre-ROI input operation is still authorized in the ROI session");
    app.endRoiPreview();
    require(!app.portContextCurrent(beforeRoi), "ending ROI editing revived a canceled binding operation");

    QQmlComponent inputComponent(&engine, QUrl::fromLocalFile(QStringLiteral(VISIONRT_SOURCE_DIR "/src/ui/qml/ParameterEditor.qml")));
    QVariantMap imageParameter;
    for (const auto &entry : app.parameters())
        if (entry.toMap().value("id").toString() == QStringLiteral("images")) imageParameter = entry.toMap();
    std::unique_ptr<QObject> inputOwner(inputComponent.createWithInitialProperties({{"parameter", imageParameter}}));
    if (!inputOwner) throw std::runtime_error(inputComponent.errorString().toStdString());
    auto *inputPanel = qobject_cast<QQuickItem *>(inputOwner.get());
    require(inputPanel != nullptr, "input binding row is not a visual item");
    inputPanel->setParentItem(window.contentItem());
    inputPanel->setWidth(740);
    auto *inputCombo = inputPanel->findChild<QObject *>(QStringLiteral("parameterUpstream"));
    require(inputCombo != nullptr, "input binding group was not instantiated");
    auto *inputPopup = inputCombo->property("popup").value<QObject *>();
    require(inputPopup && QMetaObject::invokeMethod(inputPopup, "open"), "cannot open the input binding group");
    require(QTest::qWaitFor([&] { return inputPopup->property("opened").toBool(); }, 1000),
            "input binding group did not open");
    const auto inputContext = app.portContext(QStringLiteral("images"));
    const auto inputWires = app.wires();
    const auto inputValue = inputCombo->property("savedValue");
    QSignalSpy rejectedInputChanges(&app, &visionrt::AppController::parametersChanged);
    require(!app.bindInput(inputContext, QStringLiteral("4"), QStringLiteral("images"))
                && app.wires() == inputWires && rejectedInputChanges.isEmpty()
                && inputCombo->property("savedValue") == inputValue && inputPopup->property("visible").toBool(),
            "rejecting a cyclic upstream connection changed the selected value or dismissed the input group");
    otherWindow.show();
    otherWindow.requestActivate();
    require(QTest::qWaitForWindowActive(&otherWindow, 1000), "cannot deactivate the input binding window");
    require(QTest::qWaitFor([&] { return !inputPopup->property("visible").toBool(); }, 1000),
            "input binding group survives deactivating its window");
    otherWindow.close();
    window.requestActivate();
    require(QTest::qWaitForWindowActive(&window, 1000), "cannot reactivate the input binding window");
    require(QMetaObject::invokeMethod(inputPopup, "open"), "cannot reopen the input binding group");
    require(QTest::qWaitFor([&] { return inputPopup->property("opened").toBool(); }, 1000),
            "reopened input binding group did not open");
    app.selectNode(QStringLiteral("4"));
    require(QTest::qWaitFor([&] { return !inputPopup->property("visible").toBool(); }, 1000)
                && !app.unbindInput(inputContext) && app.wires() == inputWires,
            "switching the input target left its selector open or authorized an obsolete disconnection");
    app.selectNode(QStringLiteral("3"));
    require(QMetaObject::invokeMethod(inputPopup, "open"), "cannot restore the crop input binding group");
    require(QTest::qWaitFor([&] { return inputPopup->property("opened").toBool(); }, 1000),
            "crop input binding group did not reopen");
    require(app.editRoi(QStringLiteral("rect"), false), "cannot start ROI editing with an open input selector");
    require(QTest::qWaitFor([&] { return !inputPopup->property("visible").toBool(); }, 1000),
            "input binding group remains open when ROI editing starts");
    app.endRoiPreview();
    window.hide();
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
    require(QDir().mkpath(package + "assets"), "cannot create the many-output package");
    const auto write = [&](const QString &path, const QByteArray &bytes)
    {
        QFile file(package + path);
        require(file.open(QIODevice::WriteOnly) && file.write(bytes) == bytes.size(), "cannot write the many-output fixture");
    };
    write("module_index.yaml", "schema_version: 2\nvariant: test\ntools:\n  test.outputs:\n    definition: assets/outputs.yaml\n");
    write("assets/outputs.py",
          "def run(inputs):\n"
          "    values = {'value_' + str(index): index for index in range(64)}\n"
          "    values.update(__status_code=0, __status_message='')\n"
          "    return values\n");
    QByteArray definition = "schema_version: 1\ntool_id: test.outputs\ndisplay_name: Many outputs\nversion: 1.0.0\n"
                            "kind: python_function\nentry: {file: outputs.py, symbol: run}\ninit_params: []\ninputs: []\noutputs:\n";
    for (int number = 0; number < 64; ++number)
        definition += "  - {id: value_" + QByteArray::number(number) + ", name: Value "
                    + QByteArray::number(number).rightJustified(2, '0') + ", type: Int}\n";
    write("assets/outputs.yaml", definition);
    visionrt::AppController manyApp(1, manyDirectory.path());
    require(QTest::qWaitFor([&] { return !manyApp.busy(); }, 5000), "many-output catalog did not become ready");
    manyApp.addNode("test.outputs", 0, 0);
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
    manyEngine.rootContext()->setContextProperty("Theme", visionrt::Theme::instance());
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
    auto *manyMenu = manyPanel->findChild<QObject *>("outputMenu");
    auto *manyTable = manyPanel->findChild<QObject *>("outputTable");
    require(manyMenu && manyTable, "many-output menu or table is unavailable");
    QTest::mouseClick(&manyWindow, Qt::RightButton, Qt::NoModifier,
                      firstLabel->mapToScene(QPointF(firstLabel->width() / 2, firstLabel->height() / 2)).toPoint());
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
    QSignalSpy reusedText(&manyApp, &visionrt::AppController::outputTextReady);
    auto *viewAction = visibleText(manyWindow.contentItem(), QStringLiteral("查看数据"));
    require(viewAction != nullptr, "reused-cell menu has no view action");
    QTest::mouseClick(&manyWindow, Qt::LeftButton, Qt::NoModifier,
                      viewAction->mapToScene(QPointF(viewAction->width() / 2, viewAction->height() / 2)).toPoint());
    require(QTest::qWaitFor([&] { return reusedText.count() == 1; }, 1000)
                && reusedText.front().front().toString() == "0" && !manyMenu->property("visible").toBool(),
            "reused-cell menu read another row instead of its captured output value");
    std::cout << "[Output cell reuse] A real pooled cell changed ports while its stable menu retained the original source and value.\n";
}
