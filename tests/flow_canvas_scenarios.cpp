#include "ui/UiState.h"
#include "ui/AppController.h"
#include "ui/NodeListModel.h"
#include "WorkbenchFixtures.h"
#include "ui/UiTypes.h"
#include <quickui/Color.h>

#include <QAbstractItemModelTester>
#include <QQmlComponent>
#include <QQmlContext>
#include <QQmlEngine>
#include <QImage>
#include <QJSValue>
#include <QLineF>
#include <QQuickItem>
#include <QQuickWindow>
#include <QSGRendererInterface>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QTest>
#include <algorithm>
#include <cmath>
#include <filesystem>
#include <limits>
#include <iostream>
#include <memory>
#include <stdexcept>

namespace {
// test.plugin.counter declares 步长 as input 1 and 计数 as output 2.
constexpr visionrt::PortId kCounterStepInput   = 1;
constexpr visionrt::PortId kCounterCountOutput = 2;
} // namespace


void flowCanvasContract()
{
    const auto require = [](bool condition, const char *message)
    {
        if (!condition)
            throw std::runtime_error(message);
    };
    visionrt::testing::WorkbenchInstallation installation;
    visionrt::AppController                  app(1, installation.directory());
    require(QTest::qWaitFor([&] { return !app.busy(); }, 5000), "canvas catalog did not become ready");
    QQmlEngine engine;
    quickui::QuiColor::getInstance()->setThemeMode(quickui::QuiColor::Light);
    engine.addImportPath(QStringLiteral(VISIONRT_BINARY_DIR "/qml"));
    engine.rootContext()->setContextProperty("app", &app);
    QQmlComponent component(&engine, QUrl::fromLocalFile(QStringLiteral(VISIONRT_SOURCE_DIR "/src/ui/qml/FlowCanvas.qml")));
    std::unique_ptr<QObject> owner(component.create());
    if (!owner)
        throw std::runtime_error(component.errorString().toStdString());
    auto *canvas = qobject_cast<QQuickItem *>(owner.get());
    require(canvas != nullptr, "canvas root is not an item");
    QQuickWindow window;
    window.resize(960, 640);
    canvas->setParentItem(window.contentItem());
    canvas->setSize(QSizeF(960, 640));
    window.show();
    require(QTest::qWaitForWindowExposed(&window), "canvas window was not exposed");
    auto findItem = [](auto &&self, QQuickItem *parent, const QString &name) -> QQuickItem *
    {
        for (auto *child : parent->childItems())
        {
            if (child->objectName() == name)
                return child;
            if (auto *found = self(self, child, name))
                return found;
        }
        return nullptr;
    };
    auto *surface = findItem(findItem, canvas, QStringLiteral("flowSurface"));
    require(surface != nullptr, "canvas surface is missing");
    app.requestOpen(QStringLiteral(VISIONRT_SOURCE_DIR "/examples/04_扩展示例/01_Python图像统计/Python图像统计.vrt.json"));
    app.resolveUnsaved(visionrt::UnsavedDecision::Discard);
    if (!QTest::qWaitFor([&] { return !app.busy() && app.nodes().size() == 5; }, 5000))
        throw std::runtime_error("canvas did not open the real example project: " + app.diagnostic().toStdString()
                                 + "; nodes=" + std::to_string(app.nodes().size()));
    bool readable = true;
    const auto allVisible = [&]
    {
        for (const auto &value : app.nodes())
        {
            const auto id = value.toMap().value("id").toString();
            auto *item = findItem(findItem, canvas, QStringLiteral("flowNode_") + id);
            if (!item || !item->isVisible())
                return false;
            const auto bounds = item->mapRectToItem(surface, item->boundingRect());
            if (bounds.width() <= 0 || bounds.height() <= 0 || (readable && bounds.width() < 70)
                || !surface->boundingRect().adjusted(1, 1, -1, -1).contains(bounds))
                return false;
        }
        return true;
    };
    require(QTest::qWaitFor(allVisible, 1000), "opening the real example leaves nodes outside the visible canvas or unreadable");

    // The canvas consumes the node model, so the model protocol is verified here instead of in a
    // separate harness. Qt's tester checks the row/role notification protocol.
    auto *nodeModel = qobject_cast<QAbstractListModel *>(app.nodeListModel());
    require(nodeModel != nullptr, "node list model is missing");
    QAbstractItemModelTester modelTester(nodeModel, QAbstractItemModelTester::FailureReportingMode::QtTest);
    require(nodeModel->rowCount() == static_cast<int>(app.nodes().size()),
            "node model row count disagrees with the nodes view");
    require(nodeModel->data(nodeModel->index(0), visionrt::NodeListModel::NodeIdRole).toString()
                == app.nodes().first().toMap().value("id").toString(),
            "node model does not expose the node id as its first role");
    QSignalSpy rowChanged(nodeModel, &QAbstractItemModel::dataChanged);
    QVariantList ids;
    for (const auto &value : app.nodes())
        ids.push_back(value.toMap().value("id"));
    require(app.moveNodes({QStringLiteral("2")}, -2, 0), "cannot place routing obstacle between bound nodes");
    require(!rowChanged.isEmpty(), "moving a node did not report a model change");
    const auto movedRoles = rowChanged.last().at(2).value<QList<int>>();
    require(movedRoles.contains(visionrt::NodeListModel::PosXRole) || movedRoles.contains(visionrt::NodeListModel::PosYRole),
            "moving a node did not report the position roles");
    QTest::qWait(50);
    QQuickItem *bypass = nullptr;
    QList<QQuickItem *> pending{surface};
    while (!pending.isEmpty())
    {
        auto *candidate = pending.takeLast();
        if (candidate->property("sourceX").isValid() && candidate->property("targetX").isValid()
            && candidate->property("sourceX").toDouble() == 80
            && candidate->property("sourceY").toDouble() == 60
            && candidate->property("targetX").toDouble() == 80
            && candidate->property("targetY").toDouble() == 160)
            bypass = candidate;
        pending.append(candidate->childItems());
    }
    require(bypass != nullptr, "reader-to-crop wire is missing from the real canvas");
    const auto readRoute = [](QQuickItem *wire)
    {
        const auto value = wire->property("route");
        return value.metaType() == QMetaType::fromType<QJSValue>()
                   ? value.value<QJSValue>().toVariant().toList() : value.toList();
    };
    const auto route = readRoute(bypass);
    require(route.size() >= 4, "canvas wire has no obstacle-avoiding path");
    // Wires leaving the same source port share their first pixels, so the longest segment is not
    // necessarily the wire's own: the topmost wire receives the click. Take the stretch with the
    // most clearance from every other wire instead.
    const auto clearance = [&](const QPointF &point)
    {
        double nearest = std::numeric_limits<double>::max();
        QList<QQuickItem *> items{canvas};
        while (!items.isEmpty())
        {
            auto *item = items.takeLast();
            if (item != bypass && item->objectName().startsWith(QStringLiteral("flowWire_")))
            {
                const auto points = readRoute(item);
                for (qsizetype index = 1; index < points.size(); ++index)
                {
                    const QPointF start  = points[index - 1].toPointF();
                    const QPointF finish = points[index].toPointF();
                    const QPointF span   = finish - start;
                    const double  length = span.x() * span.x() + span.y() * span.y();
                    const double  t      = length <= 0 ? 0.0
                                                       : std::clamp(((point.x() - start.x()) * span.x()
                                                                     + (point.y() - start.y()) * span.y())
                                                                        / length,
                                                                    0.0, 1.0);
                    nearest = std::min(nearest, QLineF(point, start + span * t).length());
                }
            }
            items.append(item->childItems());
        }
        return nearest;
    };
    QPointF selectionPoint;
    double  bestClearance = -1;
    for (qsizetype index = 1; index < route.size(); ++index)
    {
        const auto start  = route[index - 1].toPointF();
        const auto finish = route[index].toPointF();
        require(start.x() == finish.x() || start.y() == finish.y(), "canvas route contains a diagonal segment");
        if (start.x() == finish.x() && start.x() > 0 && start.x() < 160)
            require(std::max(start.y(), finish.y()) <= 80 || std::min(start.y(), finish.y()) >= 140,
                    "reader-to-crop wire crosses the intervening projection node");
        if (start.y() == finish.y() && start.y() > 80 && start.y() < 140)
            require(std::max(start.x(), finish.x()) <= 0 || std::min(start.x(), finish.x()) >= 160,
                    "reader-to-crop wire crosses the intervening projection node");
        const auto middle   = (start + finish) / 2;
        const auto distance = clearance(middle);
        if (distance > bestClearance)
        {
            bestClearance  = distance;
            selectionPoint = middle;
        }
    }
    require(bestClearance > 10, "no part of the reader-to-crop wire is clear of every other wire");
    QTest::mouseClick(&window, Qt::LeftButton, Qt::NoModifier, bypass->mapToScene(selectionPoint).toPoint());
    require(QTest::qWaitFor([&] { return canvas->property("selectedWire").toString() == QStringLiteral("1:3"); }, 500),
            "clicking a routed wire does not select its actual source/target relationship");
    auto *details = findItem(findItem, canvas, QStringLiteral("wireRelations"));
    require(details && details->isVisible(), "selected wire does not show its port relationships");
    require(window.grabWindow().save(QStringLiteral(VISIONRT_BINARY_DIR "/evidence/workbench_wire_routing.png")),
            "cannot save selected obstacle-avoiding wire evidence");
    app.selectNode(QStringLiteral("1"));
    require(!details->isVisible() && canvas->property("selectedWire").toString().isEmpty(),
            "selecting a node leaves stale wire relationship details visible");
    app.selectNode(QStringLiteral("3"));
    auto *obstacle = findItem(findItem, canvas, QStringLiteral("flowNode_2"));
    require(obstacle != nullptr, "routing obstacle delegate disappeared");
    const auto center = obstacle->mapToScene(obstacle->boundingRect().center()).toPoint();
    const auto finish = center + QPoint(qRound(180 * canvas->property("zoom").toDouble()), 0);
    QTest::mousePress(&window, Qt::LeftButton, Qt::NoModifier, center);
    QSignalSpy routingChanges(canvas, SIGNAL(routedWiresChanged()));
    require(routingChanges.isValid(), "canvas routing publication signal is missing");
    for (int offset = 1; offset <= 4; ++offset)
    {
        const auto dragPoint
            = surface->mapFromScene(center + QPointF(180 * canvas->property("zoom").toDouble() * offset, 0));
        require(QMetaObject::invokeMethod(canvas, "updateGesture", Q_ARG(QVariant, dragPoint)),
                "canvas drag update is missing");
    }
    std::cout << "[Routing burst] publications before event-loop return: " << routingChanges.size() << std::endl;
    require(routingChanges.isEmpty(), "drag geometry was routed synchronously instead of coalescing pending updates");
    require(QTest::qWaitFor([&] { return routingChanges.size() == 1; }, 1000),
            "pending drag updates did not publish exactly one current route");
    QTest::mouseMove(&window, finish, 30);
    QTest::qWait(30);
    auto *movingWire = findItem(findItem, canvas, QStringLiteral("flowWire_1_3"));
    require(movingWire != nullptr && readRoute(movingWire) != route,
            "dragging an intervening node does not reroute the existing reader-to-crop wire");
    QTest::mouseRelease(&window, Qt::LeftButton, Qt::NoModifier, finish);
    QTest::qWait(30);
    const auto afterDrag = app.nodes()[1].toMap();
    require(afterDrag.value("id").toString() == QStringLiteral("2") && afterDrag.value("x").toInt() == 1
                && afterDrag.value("y").toInt() == 1,
            "routing obstacle drag did not commit its actual grid position");
    auto *zoomedWire = findItem(findItem, canvas, QStringLiteral("flowWire_1_3"));
    require(zoomedWire != nullptr, "dragging an obstacle disconnected an unrelated input");
    const auto unscaledRoute = readRoute(zoomedWire);
    const auto originalZoom = canvas->property("zoom").toDouble();
    canvas->setProperty("zoom", originalZoom * 1.1);
    QTest::qWait(30);
    zoomedWire = findItem(findItem, canvas, QStringLiteral("flowWire_1_3"));
    require(zoomedWire != nullptr && readRoute(zoomedWire) == unscaledRoute,
            "canvas zoom changes the world-coordinate wire path");
    canvas->setProperty("zoom", originalZoom);
    require(app.moveNodes({QStringLiteral("2")}, -1, 0), "cannot undo routing obstacle drag");
    require(app.moveNodes({QStringLiteral("2")}, 2, 0), "cannot restore routing obstacle");
    readable = false;
    require(app.moveNodes({ids.front()}, -1000, -1000), "cannot spread canvas fixture");
    require(QMetaObject::invokeMethod(canvas, "fit"), "canvas fit action is missing");
    require(QTest::qWaitFor(allVisible, 1000), "fit clips nodes in a widely spaced flow");
    require(app.moveNodes({ids.front()}, 1000, 1000), "cannot restore canvas fixture spacing");
    for (const auto offset : {QPoint(1000, 1000), QPoint(-2000, -2000)})
    {
        require(app.moveNodes(ids, offset.x(), offset.y()), "cannot translate canvas fixture");
        require(QMetaObject::invokeMethod(canvas, "fit"), "canvas fit action is missing");
        require(QTest::qWaitFor(allVisible, 1000), "fit depends on the sign or offset of node coordinates");
    }
    require(window.grabWindow().save(QStringLiteral(VISIONRT_BINARY_DIR "/evidence/workbench_project_fit.png")),
            "cannot save fitted canvas evidence");
    const auto currentRoutes = [&]
    {
        const auto value = canvas->property("routedWires");
        return value.metaType() == QMetaType::fromType<QJSValue>() ? value.value<QJSValue>().toVariant().toList()
                                                                   : value.toList();
    };
    const auto originalFlow = app.currentFlow();
    app.addFlow(QStringLiteral("路由切换"));
    require(QTest::qWaitFor([&] { return !app.busy(); }, 5000), "alternate flow did not become ready");
    const auto alternateFlow = app.currentFlow();
    require(alternateFlow != originalFlow, "alternate routing flow was not created");
    app.selectFlow(originalFlow);
    require(QTest::qWaitFor([&] { return currentRoutes().size() == app.wires().size() && !currentRoutes().isEmpty(); },
                            1000),
            "original flow did not restore its routes");
    obstacle = findItem(findItem, canvas, QStringLiteral("flowNode_2"));
    require(obstacle != nullptr, "flow-switch drag fixture is missing");
    const auto switchingStart = obstacle->mapToScene(obstacle->boundingRect().center()).toPoint();
    QTest::mousePress(&window, Qt::LeftButton, Qt::NoModifier, switchingStart);
    require(QMetaObject::invokeMethod(
                canvas, "updateGesture",
                Q_ARG(QVariant,
                      surface->mapFromScene(switchingStart + QPointF(180 * canvas->property("zoom").toDouble(), 0)))),
            "flow switch drag update is missing");
    require(canvas->property("dragX").toDouble() != 0,
            "flow switch did not interrupt a pending node drag");
    app.selectFlow(alternateFlow);
    QTest::mouseRelease(&window, Qt::LeftButton, Qt::NoModifier, switchingStart);
    require(QTest::qWaitFor([&] { return currentRoutes().isEmpty(); }, 1000),
            "queued geometry from the old flow was published into the empty flow");
    app.selectFlow(originalFlow);
    require(QTest::qWaitFor([&] { return !currentRoutes().isEmpty(); }, 1000), "returning flow lost its routes");
    obstacle = findItem(findItem, canvas, QStringLiteral("flowNode_2"));
    require(obstacle != nullptr, "delete-during-drag fixture is missing");
    const auto deletionStart = obstacle->mapToScene(obstacle->boundingRect().center()).toPoint();
    QTest::mousePress(&window, Qt::LeftButton, Qt::NoModifier, deletionStart);
    require(QMetaObject::invokeMethod(
                canvas, "updateGesture",
                Q_ARG(QVariant,
                      surface->mapFromScene(deletionStart + QPointF(180 * canvas->property("zoom").toDouble(), 0)))),
            "delete drag update is missing");
    require(canvas->property("dragX").toDouble() != 0,
            "deletion did not interrupt a pending node drag");
    app.deleteNodes({QStringLiteral("2")});
    QTest::mouseRelease(&window, Qt::LeftButton, Qt::NoModifier, deletionStart);
    require(QTest::qWaitFor(
                [&]
                {
                    if (app.busy() || app.nodes().size() != 4 || currentRoutes().size() != app.wires().size())
                        return false;
                    for (const auto &entry : currentRoutes())
                        if (entry.toMap().value("source").toString() == "2"
                            || entry.toMap().value("target").toString() == "2")
                            return false;
                    return true;
                },
                5000),
            "queued geometry restored a deleted node's routes");
    QTest::qWait(50);
    require(currentRoutes().size() == app.wires().size(), "obsolete route publication overwrote the current graph");
    std::cout << "[Canvas Contract Passed] Fit, coalesced drag, final endpoints, flow switch and pending deletion "
                 "verified.\n";
}
