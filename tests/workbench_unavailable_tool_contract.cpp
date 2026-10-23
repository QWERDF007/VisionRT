#include "WorkbenchFixtures.h"
#include "ui/AppController.h"
#include "ui/Theme.h"
#include "ui/ToolTreeModel.h"

#include <QAbstractItemModel>
#include <QGuiApplication>
#include <QQmlComponent>
#include <QQmlContext>
#include <QQmlEngine>
#include <QQuickItem>
#include <QQuickWindow>
#include <QSGRendererInterface>
#include <QTest>
#include <algorithm>
#include <cassert>
#include <filesystem>
#include <functional>
#include <iostream>
#include <stdexcept>
#include <quickui/Color.h>

using namespace visionrt;

namespace {
void require(bool condition, const char *message)
{
    if (!condition)
        throw std::runtime_error(message);
}

QVariantMap findDefinition(const QVariantList &definitions, const QString &id)
{
    for (const auto &item : definitions)
    {
        const auto map = item.toMap();
        if (map.value("id").toString() == id)
            return map;
    }
    return {};
}

QModelIndex findToolIndex(QAbstractItemModel *model, const QString &id, const QModelIndex &parent = {})
{
    for (int row = 0; row < model->rowCount(parent); ++row)
    {
        const auto index = model->index(row, 0, parent);
        if (model->data(index, ToolTreeModel::ToolIdRole).toString() == id)
            return index;
        if (const auto nested = findToolIndex(model, id, index); nested.isValid())
            return nested;
    }
    return {};
}

QQuickItem *findItem(QQuickItem *parent, const QString &name)
{
    for (auto *child : parent->childItems())
    {
        if (child->objectName() == name)
            return child;
        if (auto *found = findItem(child, name))
            return found;
    }
    return nullptr;
}

void collectDelegates(QQuickItem *parent, QList<QQuickItem *> &result)
{
    for (auto *child : parent->childItems())
    {
        if (QString(child->metaObject()->className()).contains("ToolTreeDelegate"))
            result.push_back(child);
        collectDelegates(child, result);
    }
}

QQuickItem *delegateWithTool(QQuickItem *tree, const QString &toolId)
{
    QList<QQuickItem *> delegates;
    collectDelegates(tree, delegates);
    for (auto *delegate : delegates)
        if (delegate->property("toolId").toString() == toolId)
            return delegate;
    return nullptr;
}

QList<QQuickItem *> categories(QQuickItem *tree)
{
    QList<QQuickItem *> result;
    QList<QQuickItem *> delegates;
    collectDelegates(tree, delegates);
    for (auto *delegate : delegates)
        if (delegate->property("hasChildren").toBool() && delegate->property("depth").toInt() == 0)
            result.push_back(delegate);
    return result;
}

void clickItem(QQuickWindow &window, QQuickItem *item)
{
    require(item && item->isVisible(), "tool tree click target is not visible");
    QTest::mouseClick(&window, Qt::LeftButton, Qt::NoModifier,
                      item->mapToScene(item->boundingRect().center()).toPoint());
}
}

int main(int argc, char **argv)
try
{
    QGuiApplication application(argc, argv);
    testing::WorkbenchInstallation installation;
    const auto package = std::filesystem::path(installation.directory().toStdWString()) / "tools-package";
    const auto script  = package / "python/custom/examples/roi_geometry/tool.py";
    assert(std::filesystem::is_regular_file(script));

    AppController controller(1, installation.directory());
    require(QTest::qWaitFor([&] { return !controller.busy() && !controller.toolDefinitions().isEmpty(); }, 5000),
            "tool catalog did not become ready");
    controller.addNode(QStringLiteral("custom.python.roi"), 0, 0);
    require(QTest::qWaitFor([&] { return !controller.busy() && controller.nodes().size() == 1; }, 5000),
            "available Python tool was not added before removal");
    const auto nodeId = controller.nodes().first().toMap().value("id").toString();
    controller.copyNodes(QVariantList{nodeId});
    require(controller.canPaste(), "copied node was not retained");

    assert(std::filesystem::remove(script));
    controller.refreshPython();
    require(QTest::qWaitFor(
                [&]
                {
                    const auto current = findDefinition(controller.toolDefinitions(), QStringLiteral("custom.python.roi"));
                    return !controller.busy() && !current.isEmpty() && current.value("available").toBool() == false;
                },
                5000),
            "missing-script Python declaration was not retained as unavailable");
    const auto roi = findDefinition(controller.toolDefinitions(), QStringLiteral("custom.python.roi"));
    require(roi.value("available").toBool() == false, "missing-script Python tool was reported available");
    const auto index = findToolIndex(controller.toolModel(), QStringLiteral("custom.python.roi"));
    require(index.isValid(), "missing-script Python declaration disappeared from the tool tree");
    require(controller.toolModel()->data(index, ToolTreeModel::AvailableRole).toBool() == false,
            "tool tree did not mark the missing-script Python tool unavailable");
    require((controller.toolModel()->flags(index) & Qt::ItemIsDragEnabled) == 0,
            "unavailable tool remained draggable in the tool model");
    require(controller.toolModel()->data(index, ToolTreeModel::StatusRole).toString().contains(
                QStringLiteral("Python 脚本缺失")),
            "tool tree did not retain the unavailable diagnostic");
    const auto diagnostics = controller.diagnostics();
    require(std::any_of(diagnostics.begin(), diagnostics.end(),
                        [](const QString &message) { return message.contains(QStringLiteral("Python 脚本缺失")); }),
            "missing-script diagnosis was not retained");
    require(controller.nodes().size() == 1, "refresh removed the existing node");

    controller.pasteNodes(0, 0);
    QCoreApplication::processEvents();
    require(controller.nodes().size() == 1, "unavailable clipboard node was pasted into the flow");

    controller.addNode(QStringLiteral("custom.python.roi"), 0, 0);
    QCoreApplication::processEvents();
    require(controller.nodes().size() == 1, "unavailable tool was added to the flow");

    controller.addNode(QStringLiteral("visionrt.image.read"), 0, 0);
    require(QTest::qWaitFor([&] { return !controller.busy() && controller.nodes().size() == 2; }, 5000),
            "available tool was not added");

    controller.filterTools(QStringLiteral("ROI几何"));
    QQuickWindow::setGraphicsApi(QSGRendererInterface::Software);
    QQmlEngine engine;
    quickui::QuiColor::getInstance()->setThemeMode(quickui::QuiColor::Light);
    engine.addImportPath(QStringLiteral(VISIONRT_BINARY_DIR "/qml"));
    engine.rootContext()->setContextProperty("app", &controller);
    engine.rootContext()->setContextProperty("Theme", Theme::instance());
    QQmlComponent component(&engine,
                            QUrl::fromLocalFile(QStringLiteral(VISIONRT_SOURCE_DIR "/src/ui/qml/ToolLibrary.qml")));
    std::unique_ptr<QObject> owner(component.create());
    if (!owner)
        throw std::runtime_error(component.errorString().toStdString());
    auto *library = qobject_cast<QQuickItem *>(owner.get());
    require(library, "tool library root is not an item");
    library->setProperty("expanded", true);
    QQuickWindow window;
    window.resize(360, 640);
    library->setParentItem(window.contentItem());
    library->setSize(QSizeF(360, 640));
    window.show();
    require(QTest::qWaitForWindowExposed(&window), "tool library window was not exposed");
    window.requestActivate();
    require(QTest::qWaitForWindowActive(&window), "tool library window was not activated");
    auto *tree = findItem(library, "toolTree");
    require(tree, "tool tree is missing");
    require(QTest::qWaitFor([&] { return !categories(tree).isEmpty(); }, 3000),
            "filtered tool tree category did not lay out");
    QMetaObject::invokeMethod(tree, "allExpand");
    for (int pass = 0; pass < 3; ++pass)
    {
        QList<QQuickItem *> delegates;
        collectDelegates(tree, delegates);
        for (auto *delegate : delegates)
            if (delegate->property("hasChildren").toBool() && !delegate->property("expanded").toBool())
                clickItem(window, delegate);
        QCoreApplication::processEvents();
        QTest::qWait(50);
    }
    require(QTest::qWaitFor([&] { return delegateWithTool(tree, QStringLiteral("custom.python.roi")) != nullptr; }, 3000),
            "unavailable tool delegate was not laid out");
    auto *delegate = delegateWithTool(tree, QStringLiteral("custom.python.roi"));
    require(delegate->property("available").toBool() == false, "delegate lost unavailable state");
    require(delegate->property("status").toString().contains(QStringLiteral("Python 脚本缺失")),
            "delegate lost the unavailable diagnostic");
    require(library->property("selectedTool").toString().isEmpty(), "unavailable tool was selected before click");
    clickItem(window, delegate);
    QCoreApplication::processEvents();
    require(library->property("selectedTool").toString().isEmpty(), "unavailable tool was chosen by click");
    std::cout << "Unavailable Python declarations stay diagnostic-only; available tools remain addable\n";
    return 0;
}
catch (const std::exception &error)
{
    std::cerr << error.what() << '\n';
    return 1;
}
