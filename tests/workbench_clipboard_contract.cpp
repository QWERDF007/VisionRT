#include "WorkbenchFixtures.h"
#include "ui/AppController.h"
#include "ui/Theme.h"
#include <quickui/Color.h>

#include <QGuiApplication>
#include <QQmlComponent>
#include <QQmlContext>
#include <QQmlEngine>
#include <QQuickItem>
#include <QQuickWindow>
#include <QSGRendererInterface>
#include <QSet>
#include <QTest>
#include <cmath>
#include <iostream>
#include <memory>
#include <stdexcept>

using namespace visionrt;

namespace {
void require(bool condition, const char *message)
{
    if (!condition)
        throw std::runtime_error(message);
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

QQuickItem *findObjectItem(QObject *root, const QString &name)
{
    if (!root)
        return nullptr;
    if (auto *item = root->findChild<QQuickItem *>(name))
        return item;
    if (auto *content = root->property("contentItem").value<QQuickItem *>())
        if (auto *item = findItem(content, name))
            return item;
    for (auto *item : root->findChildren<QQuickItem *>())
        if (item->objectName() == name)
            return item;
    return nullptr;
}

QQuickItem *findText(QObject *root, const QString &text)
{
    if (!root)
        return nullptr;
    for (auto *item : root->findChildren<QQuickItem *>())
        if (item->isVisible() && item->property("text").toString() == text)
            return item;
    return nullptr;
}

void clickItem(QQuickWindow &window, QQuickItem *item, Qt::KeyboardModifiers modifiers = Qt::NoModifier)
{
    require(item && item->isVisible(), "clipboard fixture target is not visible");
    auto *target = item->window() ? item->window() : &window;
    QTest::mouseClick(target, Qt::LeftButton, modifiers, item->mapToScene(item->boundingRect().center()).toPoint());
}

void clickAt(QQuickWindow &window, QQuickItem *surface, const QPointF &point, Qt::MouseButton button = Qt::LeftButton)
{
    QTest::mouseClick(&window, button, Qt::NoModifier, surface->mapToScene(point).toPoint());
}
}

int main(int argc, char **argv)
try
{
    QGuiApplication application(argc, argv);
    QQuickWindow::setGraphicsApi(QSGRendererInterface::Software);
    testing::WorkbenchInstallation installation;
    AppController                  app(1, installation.directory());
    require(QTest::qWaitFor([&] { return !app.busy(); }, 5000), "clipboard catalog did not become ready");
    QQmlEngine engine;
    quickui::QuiColor::getInstance()->setThemeMode(quickui::QuiColor::Light);
    engine.addImportPath(QStringLiteral(VISIONRT_BINARY_DIR "/qml"));
    engine.rootContext()->setContextProperty("app", &app);
    engine.rootContext()->setContextProperty("Theme", Theme::instance());
    QQmlComponent component(&engine,
                            QUrl::fromLocalFile(QStringLiteral(VISIONRT_SOURCE_DIR "/src/ui/qml/FlowCanvas.qml")));
    std::unique_ptr<QObject> owner(component.create());
    if (!owner)
        throw std::runtime_error(component.errorString().toStdString());
    auto *canvas = qobject_cast<QQuickItem *>(owner.get());
    require(canvas, "clipboard canvas root is not an item");
    QQuickWindow window;
    window.resize(1200, 800);
    canvas->setParentItem(window.contentItem());
    canvas->setSize(QSizeF(1200, 800));
    window.show();
    require(QTest::qWaitForWindowExposed(&window), "clipboard window was not exposed");
    window.requestActivate();
    require(QTest::qWaitForWindowActive(&window), "clipboard window was not activated");
    const auto idle = [&]
    {
        require(QTest::qWaitFor([&] { return !app.busy(); }, 5000), "clipboard operation did not finish");
    };
    app.requestOpen(QStringLiteral(VISIONRT_SOURCE_DIR "/examples/pipeline_cpu_python.vrt.json"));
    if (!app.busy() && app.nodes().isEmpty())
        app.resolveUnsaved("discard");
    require(QTest::qWaitFor([&] { return !app.busy() && app.nodes().size() == 5; }, 5000),
            "clipboard example project did not open");
    auto *surface = findItem(canvas, "flowSurface");
    require(surface, "clipboard surface is missing");
    const auto wires = app.wires();
    require(!wires.isEmpty(), "clipboard example has no wire to copy");
    const auto wire   = wires.front().toMap();
    const auto source = wire.value("source").toString();
    const auto target = wire.value("target").toString();
    auto *sourceItem  = findItem(canvas, "flowNode_" + source);
    auto *targetItem  = findItem(canvas, "flowNode_" + target);
    require(sourceItem && targetItem, "clipboard node delegates are missing");
    require(QTest::qWaitFor([&] { return sourceItem->width() > 0 && targetItem->width() > 0; }, 2000),
            "clipboard node delegates did not lay out");
    clickItem(window, sourceItem);
    clickItem(window, targetItem, Qt::ControlModifier);
    surface->forceActiveFocus();
    QTest::qWait(20);
    QTest::keyClick(&window, Qt::Key_C, Qt::ControlModifier);
    QTest::qWait(20);
    require(app.canPaste(), "Ctrl+C did not copy the selected nodes");
    const auto beforeNodes = app.nodes().size();
    const auto beforeWires = app.wires().size();
    QSet<QString> originalIds;
    for (const auto &value : app.nodes()) originalIds.insert(value.toMap().value("id").toString());
    app.deleteNodes({source, target});
    idle();
    require(app.nodes().size() == beforeNodes - 2, "clipboard source deletion did not remove both nodes");
    const auto afterDeleteWires = app.wires().size();
    require(afterDeleteWires < beforeWires, "clipboard source deletion left the internal wire");
    const auto hasInternalWire = [&](const QSet<QString> &ids)
    {
        for (const auto &value : app.wires())
        {
            const auto row = value.toMap();
            if (ids.contains(row.value("source").toString()) && ids.contains(row.value("target").toString()))
                return true;
        }
        return false;
    };
    const QPointF blank(surface->width() / 2, surface->height() * 0.85);
    clickAt(window, surface, blank, Qt::RightButton);
    auto *menu = canvas->findChild<QObject *>("flowBlankMenu");
    require(menu && QTest::qWaitFor([&] { return menu->property("visible").toBool(); }, 2000),
            "blank right click did not open the clipboard menu");
    auto *paste = findObjectItem(menu, "flowBlankPaste");
    if (!paste)
        paste = findText(menu, "粘贴");
    require(paste && paste->isEnabled(), "blank menu paste action is missing or disabled");
    QTest::qWait(200);
    auto *popupWindow = paste->window() ? paste->window() : &window;
    const auto point = paste->mapToScene(paste->boundingRect().center()).toPoint();
    QTest::mousePress(popupWindow, Qt::LeftButton, Qt::NoModifier, point);
    QTest::qWait(30);
    QTest::mouseRelease(popupWindow, Qt::LeftButton, Qt::NoModifier, point);
    require(QTest::qWaitFor([&] { return !menu->property("visible").toBool(); }, 2000),
            "blank menu did not close after the paste action");
    idle();
    require(app.nodes().size() == beforeNodes, "blank menu paste did not restore both nodes");
    require(app.wires().size() == afterDeleteWires + 1, "blank menu paste did not recreate the internal wire");
    const auto gridX = std::lround((blank.x() - canvas->property("panX").toDouble()) / canvas->property("zoom").toDouble()
                                   / canvas->property("cellWidth").toDouble());
    const auto gridY = std::lround((blank.y() - canvas->property("panY").toDouble()) / canvas->property("zoom").toDouble()
                                   / canvas->property("cellHeight").toDouble());
    QSet<QString> pastedIds;
    for (const auto &value : app.nodes())
    {
        const auto row = value.toMap();
        if (originalIds.contains(row.value("id").toString()))
            continue;
        pastedIds.insert(row.value("id").toString());
        require(std::abs(row.value("x").toInt() - gridX) <= 2 && std::abs(row.value("y").toInt() - gridY) <= 2,
                "blank menu paste did not land at the requested grid point");
    }
    require(pastedIds.size() == 2 && hasInternalWire(pastedIds), "pasted nodes lost their internal wire");
    surface->forceActiveFocus();
    QTest::keyClick(&window, Qt::Key_V, Qt::ControlModifier);
    idle();
    require(app.nodes().size() == beforeNodes + 2, "Ctrl+V did not paste the copied nodes at the viewport center");
    require(app.wires().size() == afterDeleteWires + 2, "Ctrl+V did not recreate the internal wire");
    const auto afterKeys = app.nodes().size();
    QTest::mousePress(&window, Qt::RightButton, Qt::NoModifier, surface->mapToScene(blank).toPoint());
    QTest::mouseMove(&window, surface->mapToScene(blank + QPointF(120, 0)).toPoint());
    QTest::mouseRelease(&window, Qt::RightButton, Qt::NoModifier,
                        surface->mapToScene(blank + QPointF(120, 0)).toPoint());
    QTest::qWait(100);
    require(!menu->property("visible").toBool(), "right drag opened the blank clipboard menu");
    require(app.nodes().size() == afterKeys, "right drag changed the project");
    std::cout << "WORKBENCH_CLIPBOARD_PASS copied=2 internal_wire=1 menu=1 ctrl_v=1 right_drag=0\n";
    window.hide();
    return 0;
}
catch (const std::exception &error)
{
    std::cerr << error.what() << '\n';
    return 1;
}
