#include "ui/AppController.h"
#include "ui/Theme.h"
#include <quickui/Color.h>

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
#include <cmath>
#include <filesystem>
#include <iostream>
#include <memory>
#include <stdexcept>

void flowCanvasContract()
{
    const auto require = [](bool condition, const char *message)
    {
        if (!condition)
            throw std::runtime_error(message);
    };
    visionrt::AppController app(1, QStringLiteral(VISIONRT_BINARY_DIR));
    require(QTest::qWaitFor([&] { return !app.busy(); }, 5000), "canvas catalog did not become ready");
    QQmlEngine engine;
    quickui::QuiColor::getInstance()->setThemeMode(quickui::QuiColor::Light);
    engine.addImportPath(QStringLiteral(VISIONRT_BINARY_DIR "/qml"));
    engine.rootContext()->setContextProperty("app", &app);
    engine.rootContext()->setContextProperty("Theme", visionrt::Theme::instance());
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
    app.requestOpen(QStringLiteral(VISIONRT_SOURCE_DIR "/examples/pipeline_cpu_python.vrt"));
    app.resolveUnsaved(QStringLiteral("discard"));
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
    QVariantList ids;
    for (const auto &value : app.nodes())
        ids.push_back(value.toMap().value("id"));
    require(app.moveNodes({QStringLiteral("2")}, -2, 1), "cannot place routing obstacle between bound nodes");
    QTest::qWait(50);
    QQuickItem *bypass = nullptr;
    QList<QQuickItem *> pending{surface};
    while (!pending.isEmpty())
    {
        auto *candidate = pending.takeLast();
        if (candidate->property("sourceX").isValid() && candidate->property("targetX").isValid()
            && candidate->property("sourceX").toDouble() == 78
            && candidate->property("sourceY").toDouble() == 52
            && candidate->property("targetX").toDouble() == 78
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
    QPointF selectionPoint;
    double longestSegment = 0;
    for (qsizetype index = 1; index < route.size(); ++index)
    {
        const auto start = route[index - 1].toPointF();
        const auto finish = route[index].toPointF();
        require(start.x() == finish.x() || start.y() == finish.y(), "canvas route contains a diagonal segment");
        if (start.x() == finish.x() && start.x() > 0 && start.x() < 156)
            require(std::max(start.y(), finish.y()) <= 80 || std::min(start.y(), finish.y()) >= 132,
                    "reader-to-crop wire crosses the intervening projection node");
        if (start.y() == finish.y() && start.y() > 80 && start.y() < 132)
            require(std::max(start.x(), finish.x()) <= 0 || std::min(start.x(), finish.x()) >= 156,
                    "reader-to-crop wire crosses the intervening projection node");
        const auto length = QLineF(start, finish).length();
        if (length > longestSegment)
        {
            longestSegment = length;
            selectionPoint = (start + finish) / 2;
        }
    }
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
    require(bypass->property("lineColor").value<QColor>() == QColor("#1677ff"),
            "selected source does not highlight its outgoing relationship");
    app.selectNode(QStringLiteral("3"));
    require(bypass->property("lineColor").value<QColor>() == QColor("#f08800"),
            "selected target does not distinguish its upstream relationship");
    auto *obstacle = findItem(findItem, canvas, QStringLiteral("flowNode_2"));
    require(obstacle != nullptr, "routing obstacle delegate disappeared");
    const auto center = obstacle->mapToScene(obstacle->boundingRect().center()).toPoint();
    const auto finish = center + QPoint(qRound(180 * canvas->property("zoom").toDouble()), 0);
    QTest::mousePress(&window, Qt::LeftButton, Qt::NoModifier, center);
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
    require(app.moveNodes({QStringLiteral("2")}, 2, -1), "cannot restore routing obstacle");
    readable = false;
    require(app.moveNodes({ids.front()}, 1000, 1000), "cannot spread canvas fixture");
    require(QMetaObject::invokeMethod(canvas, "fit"), "canvas fit action is missing");
    require(QTest::qWaitFor(allVisible, 1000), "fit clips nodes in a widely spaced flow");
    require(app.moveNodes({ids.front()}, -1000, -1000), "cannot restore canvas fixture spacing");
    for (const auto offset : {QPoint(1000, 1000), QPoint(-2000, -2000)})
    {
        require(app.moveNodes(ids, offset.x(), offset.y()), "cannot translate canvas fixture");
        require(QMetaObject::invokeMethod(canvas, "fit"), "canvas fit action is missing");
        require(QTest::qWaitFor(allVisible, 1000), "fit depends on the sign or offset of node coordinates");
    }
    require(window.grabWindow().save(QStringLiteral(VISIONRT_BINARY_DIR "/evidence/workbench_project_fit.png")),
            "cannot save fitted canvas evidence");
    std::cout << "[Canvas Contract Passed] Loaded example and translated bounds remain visible after fitting.\n";
}

void hoverCardContract(visionrt::AppController &app, QQuickWindow *window, double scale)
{
    const auto require = [](bool condition, const std::string &message)
    {
        if (!condition)
            throw std::runtime_error(message);
    };
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
    auto *canvas  = findItem(findItem, window->contentItem(), QStringLiteral("flowCanvas"));
    auto *surface = findItem(findItem, window->contentItem(), QStringLiteral("flowSurface"));
    auto *card    = findItem(findItem, window->contentItem(), QStringLiteral("nodeHoverCard"));
    require(canvas != nullptr && surface != nullptr && card != nullptr, "hover card items are missing");
    require(!card->isEnabled(), "hover card must not accept input or focus");
    require(canvas->property("hoverCardVisible").toBool() == false, "hover card is visible without hover");

    auto nodeMap = [&](const QString &id)
    {
        for (const auto &item : app.nodes())
            if (item.toMap().value("id").toString() == id)
                return item.toMap();
        return QVariantMap{};
    };
    auto nodeItem = [&](const QString &id)
    { return findItem(findItem, window->contentItem(), QStringLiteral("flowNode_") + id); };
    auto nodeCenter = [&](QQuickItem *item)
    { return item->mapToScene(QPointF(item->width() / 2, item->height() / 2)).toPoint(); };
    auto texts = [&]
    {
        QStringList values;
        auto        collect = [&](auto &&self, QQuickItem *parent) -> void
        {
            for (auto *item : parent->childItems())
            {
                const QVariant value = item->property("text");
                if (value.isValid() && value.canConvert<QString>())
                    values << value.toString();
                self(self, item);
            }
        };
        collect(collect, card);
        return values;
    };
    const auto hoverNode = [&](QQuickItem *target)
    {
        window->requestActivate();
        require(QTest::qWaitForWindowActive(window), "hover test window did not become active");
        QTest::mouseMove(window, surface->mapToScene(QPointF(-2, -2)).toPoint());
        QTest::qWait(20);
        QTest::mouseMove(window, nodeCenter(target));
        require(QTest::qWaitFor([&] { return canvas->property("hoverCardVisible").toBool(); }, 1500),
                "hover card did not appear after reentering the node");
    };

    const auto first = app.nodes().first().toMap();
    const auto id    = first.value("id").toString();
    app.selectNode(id);
    QMetaObject::invokeMethod(canvas, "centerNode", Q_ARG(QVariant, id));
    QTest::qWait(50);
    auto      *item  = nodeItem(id);
    require(item != nullptr, "flow node delegate is missing: id=" + id.toStdString());
    const QPoint center = nodeCenter(item);

    window->requestActivate();
    require(QTest::qWaitForWindowActive(window), "initial hover test window did not become active");
    QTest::mouseMove(window, center);
    QTest::qWait(700);
    require(canvas->property("hoverCardVisible").toBool(), "hover card did not appear after 500 ms");
    require(card->isVisible(), "hover card item is not visible");
    require(!card->hasActiveFocus(), "hover card took keyboard focus");
    const QStringList content = texts();
    require(content.size() == 7, "hover card contains more than the tool name and three timing rows");
    require(content.contains(first.value("title").toString()), "hover card lost the tool name");
    require(content.contains(QStringLiteral("算子")) && content.contains(QStringLiteral("预处理"))
                && content.contains(QStringLiteral("后处理")),
            "hover card lost a timing row");
    require(content.contains(first.value("timingOperatorText").toString()),
            "hover card operator time does not match the structured node data");
    require(content.contains(first.value("timingPreprocessText").toString()),
            "hover card preprocess time does not match the structured node data");
    require(content.contains(first.value("timingPostprocessText").toString()),
            "hover card postprocess time does not match the structured node data");

    int renderedTexts = 0;
    auto checkRendering = [&](auto &&self, QQuickItem *parent) -> void
    {
        for (auto *child : parent->childItems())
        {
            const QVariant renderType = child->property("renderType");
            if (renderType.isValid())
            {
                ++renderedTexts;
                require(renderType.toInt() == 0, std::string("node text is not using QtRendering: ")
                                                     + child->metaObject()->className() + " renderType="
                                                     + std::to_string(renderType.toInt()) + " text="
                                                     + child->property("text").toString().toStdString());
            }
            self(self, child);
        }
    };
    checkRendering(checkRendering, item);
    require(renderedTexts >= 4, "node text rendering was not inspected");

    std::filesystem::create_directories("build/evidence");
    require(window->grabWindow().save(QStringLiteral("build/evidence/workbench_hover_card.png")),
            "failed to save hover card evidence");

    app.toggleLoop();
    const bool runningVisible = QTest::qWaitFor([&]
    {
        return nodeMap(id).value("state").toString() == QStringLiteral("blue")
            && nodeMap(id).value("timingText").toString() == QStringLiteral("运行中")
            && texts().contains(QStringLiteral("运行中"));
    }, 3000);
    app.toggleLoop();
    for (int attempt = 0; app.busy() && attempt < 100; ++attempt)
        QTest::qWait(10);
    require(runningVisible, "visible hover card did not show the real running measurement state");
    require(!app.busy(), "hovered node execution did not finish");
    require(canvas->property("hoverCardVisible").toBool(), "execution model refresh closed the visible hover card");
    require(texts().contains(nodeMap(id).value("timingOperatorText").toString()),
            "visible hover card did not follow the current execution snapshot");
    require(!texts().contains(QStringLiteral("运行中")), "completed hover card retained the running measurement state");
    item = nodeItem(id);
    require(item != nullptr, "hovered node disappeared after execution");

    QTest::mouseMove(window, surface->mapToScene(QPointF(surface->width() - 4, 4)).toPoint());
    QTest::qWait(100);
    require(!canvas->property("hoverCardVisible").toBool(), "hover card survived leaving the node");

    QTest::mouseMove(window, center);
    QTest::qWait(700);
    require(canvas->property("hoverCardVisible").toBool(), "hover card did not reappear");
    QTest::mousePress(window, Qt::LeftButton, Qt::NoModifier, center);
    QTest::qWait(50);
    require(!canvas->property("hoverCardVisible").toBool(), "hover card survived a pointer press");
    QTest::mouseRelease(window, Qt::LeftButton, Qt::NoModifier, center);
    QTest::qWait(50);

    hoverNode(item);
    QTest::mouseClick(window, Qt::RightButton, Qt::NoModifier, nodeCenter(item));
    QTest::qWait(600);
    require(!canvas->property("hoverCardVisible").toBool(), "hover card survived opening the context menu");
    QTest::keyClick(window, Qt::Key_Escape);
    QTest::qWait(50);

    const double savedZoom = canvas->property("zoom").toDouble();
    const double savedPanX = canvas->property("panX").toDouble();
    const double savedPanY = canvas->property("panY").toDouble();
    canvas->setProperty("zoom", 1.0);
    const double rightEdge = surface->width() - item->width() - 6;
    const double bottomEdge = surface->height() - item->height() - 6;
    for (const auto &corner : {QPointF(6, 6), QPointF(rightEdge, 6), QPointF(6, bottomEdge), QPointF(rightEdge, bottomEdge)})
    {
        QTest::mouseMove(window, surface->mapToScene(QPointF(2, 2)).toPoint());
        QTest::qWait(30);
        canvas->setProperty("panX", corner.x() - item->x());
        canvas->setProperty("panY", corner.y() - item->y());
        QTest::qWait(50);
        QTest::mouseMove(window, nodeCenter(item));
        QTest::qWait(550);
        require(canvas->property("hoverCardVisible").toBool(), "hover card did not appear near a canvas corner");
        require(card->x() >= 3 && card->y() >= 3, "hover card left the top/left canvas edge");
        require(card->x() + card->width() <= surface->width() - 3, "hover card left the right canvas edge");
        require(card->y() + card->height() <= surface->height() - 3, "hover card left the bottom canvas edge");
    }

    const double singleLineHeight = card->height();
    const double cardWidth        = card->width();
    QTest::mouseMove(window, surface->mapToScene(QPointF(surface->width() - 4, 4)).toPoint());
    QTest::qWait(50);
    const QString longName = QStringLiteral("超长工具名称用于验证悬停卡片换行与可见性边界");
    app.renameNode(id, longName);
    QTest::qWait(50);
    item = nodeItem(id);
    require(item != nullptr, "node delegate is missing after renaming");
    QTest::mouseMove(window, nodeCenter(item));
    QTest::qWait(700);
    require(canvas->property("hoverCardVisible").toBool(), "hover card did not appear for a long name");
    require(qFuzzyCompare(card->width(), cardWidth), "long name changed the hover card width");
    require(card->height() > singleLineHeight, "long name did not wrap inside the hover card");
    require(texts().contains(longName + "-" + id), "hover card lost the long tool name");

    app.deleteNodes(QVariantList{id});
    QTest::qWait(100);
    require(!canvas->property("hoverCardVisible").toBool(), "hover card survived deleting the node");

    const auto remaining    = app.nodes().first().toMap();
    const auto remainingId  = remaining.value("id").toString();
    const auto flow         = app.currentFlow();
    QMetaObject::invokeMethod(canvas, "centerNode", Q_ARG(QVariant, remainingId));
    QTest::qWait(50);
    auto      *other        = nodeItem(remainingId);
    require(other != nullptr, "remaining node delegate is missing");
    hoverNode(other);
    app.addFlow(QStringLiteral("悬停流程"));
    QTest::qWait(100);
    require(!canvas->property("hoverCardVisible").toBool(), "hover card survived switching flows");
    app.selectFlow(flow);
    QTest::qWait(100);

    other = nodeItem(remainingId);
    require(other != nullptr, "node delegate is missing after flow restore");
    QMetaObject::invokeMethod(canvas, "centerNode", Q_ARG(QVariant, remainingId));
    QTest::qWait(50);
    other = nodeItem(remainingId);
    hoverNode(other);
    window->hide();
    QTest::qWait(100);
    require(!canvas->property("hoverCardVisible").toBool(), "hover card survived window deactivation");
    window->show();
    window->requestActivate();
    QTest::qWait(200);

    other = nodeItem(remainingId);
    hoverNode(other);
    QSignalSpy catalogChanges(&app, &visionrt::AppController::catalogChanged);
    app.rescanTools();
    require(QTest::qWaitFor([&] { return !catalogChanges.isEmpty() && !app.busy(); }, 3000), "catalog refresh did not finish");
    require(!canvas->property("hoverCardVisible").toBool(), "hover card survived catalog refresh");
    QTemporaryDir projectDirectory;
    require(projectDirectory.isValid(), "hover replacement fixture is unavailable");
    const auto replacementPath = projectDirectory.filePath("hover.vrt");
    require(app.save(replacementPath), "cannot save hover replacement fixture");
    other = nodeItem(remainingId);
    QMetaObject::invokeMethod(canvas, "centerNode", Q_ARG(QVariant, remainingId));
    hoverNode(other);
    QSignalSpy projectChanges(&app, &visionrt::AppController::projectReplaced);
    app.requestOpen(replacementPath);
    require(QTest::qWaitFor([&] { return !projectChanges.isEmpty() && !app.busy(); }, 3000), "hover replacement did not finish");
    require(!canvas->property("hoverCardVisible").toBool(), "hover card survived project replacement");

    const auto windowSize = window->size();
    QMetaObject::invokeMethod(canvas, "centerNode", Q_ARG(QVariant, remainingId));
    QTest::qWait(50);
    auto *resizeNode = nodeItem(remainingId);
    require(resizeNode != nullptr, "resize fixture node is missing");
    const auto beforeResize = resizeNode->mapToItem(surface, QPointF(resizeNode->width() / 2, resizeNode->height() / 2))
        - QPointF(surface->width() / 2, surface->height() / 2);
    window->resize(1600, 900);
    QTest::qWait(100);
    const auto afterResize = resizeNode->mapToItem(surface, QPointF(resizeNode->width() / 2, resizeNode->height() / 2))
        - QPointF(surface->width() / 2, surface->height() / 2);
    require(QLineF(beforeResize, afterResize).length() < 1, "resizing the viewport moved the visible scene away from its center");
    const auto resizedFlow = app.currentFlow();
    app.addFlow(QStringLiteral("视图恢复"));
    const auto temporaryFlow = app.currentFlow();
    window->resize(1400, 800);
    QTest::qWait(100);
    app.selectFlow(resizedFlow);
    QTest::qWait(50);
    resizeNode = nodeItem(remainingId);
    require(resizeNode != nullptr, "restored flow lost its node delegate");
    const auto restoredCenter = resizeNode->mapToItem(surface, QPointF(resizeNode->width() / 2, resizeNode->height() / 2))
        - QPointF(surface->width() / 2, surface->height() / 2);
    require(QLineF(beforeResize, restoredCenter).length() < 1, "restored flow used pixel offsets from a different viewport size");
    app.deleteFlow(temporaryFlow);
    window->resize(1600, 900);
    app.renameNode(remainingId, QStringLiteral("图像读取"));
    app.selectNode(remainingId);
    QTest::qWait(100);
    for (const double zoom : {1.0, 1.5, 2.0, 3.0})
    {
        QTest::mouseMove(window, surface->mapToScene(QPointF(2, 2)).toPoint());
        canvas->setProperty("zoom", zoom);
        QMetaObject::invokeMethod(canvas, "centerNode", Q_ARG(QVariant, remainingId));
        QTest::qWait(100);
        auto *zoomed = nodeItem(remainingId);
        require(zoomed != nullptr, "node delegate is missing in the zoom matrix");
        checkRendering(checkRendering, zoomed);
        QObject *selectionPath = nullptr;
        for (auto *object : zoomed->findChildren<QObject *>())
            if (object->property("strokeWidth").isValid() && object->property("dashPattern").isValid())
            {
                selectionPath = object;
                break;
            }
        require(selectionPath != nullptr, "selection border path is missing");
        require(qAbs(selectionPath->property("strokeWidth").toDouble() * zoom - 2.0) < 0.01,
                "selection border scales with the canvas instead of the screen");
        const auto dashPattern = selectionPath->property("dashPattern").value<QList<qreal>>();
        require(dashPattern.size() == 2, "selection border dash pattern is missing");
        for (const auto segment : dashPattern)
            require(qAbs(segment * selectionPath->property("strokeWidth").toDouble() * zoom - 8.0) < 0.01,
                    "selection border dash length changes with canvas zoom");
        QTest::mouseMove(window, nodeCenter(zoomed));
        QTest::qWait(550);
        require(canvas->property("hoverCardVisible").toBool(), "hover card is missing in the zoom matrix");
        require(qFuzzyCompare(card->width(), cardWidth), "hover card width changed with canvas zoom");
        const auto screenshot = QStringLiteral("build/evidence/workbench_dpi_%1_zoom_%2.png")
                                    .arg(scale, 0, 'f', 1).arg(qRound(zoom * 100));
        require(window->grabWindow().save(screenshot.left(screenshot.size() - 4) + "_hover.png"),
                "failed to save zoom matrix hover evidence");
        QTest::mouseMove(window, surface->mapToScene(QPointF(2, 2)).toPoint());
        QTest::qWait(50);
        require(!canvas->property("hoverCardVisible").toBool(), "hover card blocked the node rendering evidence");
        require(window->grabWindow().save(screenshot), "failed to save zoom matrix evidence");
    }

    app.renameNode(remainingId, longName);
    app.selectNode(remainingId);
    hoverNode(nodeItem(remainingId));
    require(app.setParameter("step", false, "100"), "hover revision fixture parameter could not be edited");
    require(QTest::qWaitFor([&]
    {
        return canvas->property("hoverCardVisible").toBool()
            && nodeMap(remainingId).value("state").toString() == QStringLiteral("gray")
            && texts().contains(QStringLiteral("未运行"));
    }, 1000), "visible hover card retained measurements from an invalidated execution revision");
    app.runNode();
    require(QTest::qWaitFor([&] { return !app.busy(); }, 5000), "long-name rendering fixture did not execute");
    require(nodeMap(remainingId).value("state").toString() == "green"
                && nodeMap(remainingId).value("timingText").toString().endsWith(QStringLiteral(" ms")),
            "long-name rendering fixture has no successful measured execution");
    canvas->setProperty("zoom", 3.0);
    QMetaObject::invokeMethod(canvas, "centerNode", Q_ARG(QVariant, remainingId));
    QTest::qWait(50);
    auto *executedNode = nodeItem(remainingId);
    require(executedNode != nullptr, "executed long-name node delegate is missing");
    hoverNode(executedNode);
    require(texts().contains(longName + "-" + remainingId), "executed hover card lost the complete name");
    require(window->grabWindow().save(QStringLiteral("build/evidence/workbench_dpi_%1_executed_long_name.png")
                                        .arg(scale, 0, 'f', 1)),
            "failed to save executed long-name rendering evidence");

    const auto savedFlow = app.currentFlow();
    app.addFlow(QStringLiteral("缩放交互"));
    const auto interactionFlow = app.currentFlow();
    app.addNode("test.plugin.counter", 0, 0);
    app.addNode("test.plugin.counter", 1, 2);
    require(app.nodes().size() == 2, "canvas interaction fixtures were not created");
    const auto sourceId = app.nodes().first().toMap().value("id").toString();
    const auto targetId = app.nodes().last().toMap().value("id").toString();
    app.selectNode(targetId);
    require(app.bindInput(app.portContext("step"), sourceId, "count"), "canvas fixture binding failed");
    QTest::qWait(100);
    for (const bool fitted : {false, true})
    {
        if (fitted)
            require(QMetaObject::invokeMethod(canvas, "fit"), "fit view action is missing");
        else
        {
            require(QMetaObject::invokeMethod(canvas, "zoomAt", Q_ARG(QVariant, 0.001),
                                             Q_ARG(QVariant, 100), Q_ARG(QVariant, 100)),
                    "minimum zoom action is missing");
            require(qAbs(canvas->property("zoom").toDouble() - 0.15) < 0.001, "minimum zoom did not clamp at 15%");
            QMetaObject::invokeMethod(canvas, "centerNode", Q_ARG(QVariant, targetId));
        }
        QTest::qWait(100);
        const auto zoom = canvas->property("zoom").toDouble();
        auto *target = nodeItem(targetId);
        require(target != nullptr, "target node is missing at minimum/fit zoom");
        const auto position = nodeMap(targetId);
        const auto start = nodeCenter(target);
        const QPoint finish = start + QPoint(qRound(180 * zoom), qRound(80 * zoom));
        QTest::mousePress(window, Qt::LeftButton, Qt::NoModifier, start);
        QTest::mouseMove(window, finish, 30);
        QTest::qWait(30);
        require(!canvas->property("hoverCardVisible").toBool(), "dragging kept the hover card visible");
        QTest::mouseRelease(window, Qt::LeftButton, Qt::NoModifier, finish);
        QTest::qWait(100);
        const auto moved = nodeMap(targetId);
        require(moved.value("x").toInt() == position.value("x").toInt() + 1
                    && moved.value("y").toInt() == position.value("y").toInt() + 1,
                "node drag does not follow the canvas coordinate transform at minimum/fit zoom");
        require(app.selectedNode() == targetId, "drag selected the wrong node at minimum/fit zoom");

        QQuickItem *wire = nullptr;
        QList<QQuickItem *> pending{surface};
        while (!pending.isEmpty())
        {
            auto *candidate = pending.takeLast();
            if (candidate->property("sourceX").isValid() && candidate->property("targetX").isValid())
                wire = candidate;
            pending.append(candidate->childItems());
        }
        require(wire != nullptr && app.wires().size() == 1, "bound nodes lost their rendered wire");
        auto *source = nodeItem(sourceId);
        target = nodeItem(targetId);
        const QPointF wireSource = wire->mapToScene(QPointF(wire->property("sourceX").toDouble(),
                                                           wire->property("sourceY").toDouble()));
        const QPointF wireTarget = wire->mapToScene(QPointF(wire->property("targetX").toDouble(),
                                                           wire->property("targetY").toDouble()));
        require(QLineF(wireSource, source->mapToScene(QPointF(78, 52))).length() < 0.01,
                "wire source is not aligned with its node after a zoomed drag");
        require(QLineF(wireTarget, target->mapToScene(QPointF(78, 0))).length() < 0.01,
                "wire target is not aligned with its node after a zoomed drag");
        const auto bounds = source->mapRectToScene(source->boundingRect()).united(
            target->mapRectToScene(target->boundingRect()));
        const auto boxStart = bounds.topLeft().toPoint() - QPoint(12, 12);
        const auto boxEnd = bounds.bottomRight().toPoint() + QPoint(12, 12);
        QTest::mousePress(window, Qt::LeftButton, Qt::NoModifier, boxStart);
        QTest::mouseMove(window, boxEnd, 30);
        QTest::mouseRelease(window, Qt::LeftButton, Qt::NoModifier, boxEnd);
        QTest::qWait(50);
        const auto selected = canvas->property("selection").toStringList();
        require(selected.contains(sourceId) && selected.contains(targetId) && selected.size() == 2,
                "box selection does not follow the minimum/fit coordinate transform");
        app.selectNode(targetId);
        app.unbindInput(app.portContext("step"));
        QTest::qWait(50);
        require(app.wires().isEmpty(), "unbinding did not remove the zoomed wire");
        require(app.bindInput(app.portContext("step"), sourceId, "count"), "reconnecting the zoomed nodes failed");
        QTest::qWait(50);
        require(app.wires().size() == 1, "reconnecting did not restore the wire");
    }
    app.selectFlow(savedFlow);
    app.deleteFlow(interactionFlow);
    std::cout << "[Evidence T-02] Minimum 15% and fitted views: pointer drag, box selection and bound wire endpoints verified.\n";

    window->resize(windowSize);
    QTest::qWait(100);
    canvas->setProperty("zoom", savedZoom);
    canvas->setProperty("panX", savedPanX);
    canvas->setProperty("panY", savedPanY);
    std::cout << "[Evidence T-02] Single hover card content, delay, edge clamp, wrap and lifecycle verified.\n";
    std::cout << "[Evidence T-03] Visible hover card updates during real loop execution, completion and execution-revision invalidation.\n";
    const auto api = window->rendererInterface()->graphicsApi();
    std::cout << "[Evidence D-01] DPR=" << scale << ", zoom=100/150/200/300%, backend="
              << (api == QSGRendererInterface::Direct3D11 ? "Direct3D11" : "other")
              << " (api=" << api << "), QtRendering and screen-sized selection border verified.\n";
}

