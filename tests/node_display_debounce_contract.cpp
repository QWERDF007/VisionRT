#include "ui/Color.h"
#include "ui/DashedBorderItem.h"
#include "ui/NodeListModel.h"
#include "ui/UiTypes.h"

#include <QDir>
#include <QGuiApplication>
#include <QQmlComponent>
#include <QQmlContext>
#include <QQmlEngine>
#include <QQuickItem>
#include <QQuickWindow>
#include <QSGRendererInterface>
#include <QSignalSpy>
#include <QTest>
#include <quickui/Color.h>

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

NodeListModel::Row makeRow(QString id, NodeState state, bool missing = false)
{
    NodeListModel::Row row;
    row.id       = std::move(id);
    row.title    = QStringLiteral("节点-") + row.id;
    row.toolName = QStringLiteral("工具");
    row.version  = QStringLiteral("1.0");
    row.category = QStringLiteral("检测");
    row.state    = state;
    row.missing  = missing;
    return row;
}

QQuickItem *findItem(QQuickItem *parent, const QString &name)
{
    if (!parent)
        return nullptr;
    if (parent->objectName() == name)
        return parent;
    for (auto *child : parent->childItems())
        if (auto *found = findItem(child, name))
            return found;
    return nullptr;
}

bool hasColorNear(const QImage &image, const QRect &area, const QColor &expected)
{
    const int x0 = std::max(0, area.left());
    const int y0 = std::max(0, area.top());
    const int x1 = std::min(image.width() - 1, area.right());
    const int y1 = std::min(image.height() - 1, area.bottom());
    for (int y = y0; y <= y1; ++y)
        for (int x = x0; x <= x1; ++x)
        {
            const auto c = image.pixelColor(x, y);
            if (std::abs(c.red() - expected.red()) < 25 && std::abs(c.green() - expected.green()) < 25
                && std::abs(c.blue() - expected.blue()) < 25)
                return true;
        }
    return false;
}
} // namespace

int main(int argc, char **argv)
try
{
    QGuiApplication app(argc, argv);
    QQuickWindow::setGraphicsApi(QSGRendererInterface::Software);
    registerUiTypes();

    // 1. NodeListModel running display debounce semantics
    NodeListModel model;
    model.setRunningHoldMs(120);
    model.reset({makeRow("1", NodeState::Idle), makeRow("2", NodeState::Idle)});

    const auto stateOf = [&](const QString &id)
    {
        return static_cast<NodeState>(model.data(model.index(model.rowOf(id)), NodeListModel::StateRole).toInt());
    };
    const auto displayOf = [&](const QString &id)
    {
        return static_cast<NodeState>(model.data(model.index(model.rowOf(id)), NodeListModel::DisplayStateRole).toInt());
    };

    // Immediate green on Running
    require(model.update(makeRow("1", NodeState::Running)), "running transition ignored");
    require(stateOf("1") == NodeState::Running && displayOf("1") == NodeState::Running,
            "running state did not immediately turn display green");

    // Fast exit to Succeeded keeps true state immediate while holding display Running
    require(model.update(makeRow("1", NodeState::Succeeded)), "succeeded transition ignored");
    require(stateOf("1") == NodeState::Succeeded, "true state must not be delayed");
    require(displayOf("1") == NodeState::Running, "display state did not hold Running during debounce window");

    // Re-entering Running during hold window cancels release without flickering back to Idle/Succeeded
    QTest::qWait(40);
    require(model.update(makeRow("1", NodeState::Running)), "loop re-entry ignored");
    require(displayOf("1") == NodeState::Running, "display state flickered during loop re-entry");

    // Exit to Succeeded again and wait for hold window to expire
    require(model.update(makeRow("1", NodeState::Succeeded)), "second succeeded transition ignored");
    require(displayOf("1") == NodeState::Running, "display state dropped before hold expiry");
    QSignalSpy spy(&model, &QAbstractItemModel::dataChanged);
    require(QTest::qWaitFor([&] { return displayOf("1") == NodeState::Succeeded; }, 500),
            "display state never settled to Succeeded after hold expired");
    require(spy.count() == 1 && spy.first().at(2).value<QList<int>>() == QList<int>{NodeListModel::DisplayStateRole},
            "hold expiry emitted unexpected roles");

    // Error must NEVER be delayed
    require(model.update(makeRow("1", NodeState::Running)), "running before failure ignored");
    require(model.update(makeRow("1", NodeState::Failed)), "failed transition ignored");
    require(stateOf("1") == NodeState::Failed && displayOf("1") == NodeState::Failed,
            "failed state was delayed by running hold");

    // Missing must NEVER be delayed
    require(model.update(makeRow("2", NodeState::Running)), "node 2 running ignored");
    require(model.update(makeRow("2", NodeState::Idle, true)), "missing transition ignored");
    require(displayOf("2") == NodeState::Idle, "missing node remained in Running hold");

    // Reset clears pending holds cleanly (no stray timer notifications)
    require(model.update(makeRow("1", NodeState::Running)), "re-enter running before reset ignored");
    require(model.update(makeRow("1", NodeState::Succeeded)), "exit running before reset ignored");
    model.reset({});
    spy.clear();
    QTest::qWait(180);
    require(spy.isEmpty(), "reset left an active hold timer emitting stray notifications");

    // 2. NodeDelegate single-border mutual exclusion and DashedBorder rendering under Software backend
    model.reset({makeRow("1", NodeState::Idle), makeRow("2", NodeState::Idle, true)});
    QQmlEngine engine;
    engine.addImportPath(QStringLiteral(VISIONRT_BINARY_DIR "/qml"));
    engine.rootContext()->setContextProperty("testModel", &model);
    QQmlComponent component(&engine);
    component.setData(
        "import QtQuick\n"
        "import VisionRT.Ui 1.0\n"
        "Item {\n"
        "  id: sceneRoot\n"
        "  width: 480; height: 200\n"
        "  property real scaleValue: 1.0\n"
        "  property string selectedId: ''\n"
        "  Item {\n"
        "    id: world; scale: sceneRoot.scaleValue; transformOrigin: Item.TopLeft\n"
        "    Repeater {\n"
        "      model: testModel\n"
        "      NodeDelegate {\n"
        "        x: nodeId === '1' ? 20 : 220; y: 40\n"
        "        selected: nodeId === sceneRoot.selectedId\n"
        "        viewScale: sceneRoot.scaleValue\n"
        "      }\n"
        "    }\n"
        "  }\n"
        "}\n",
        QUrl::fromLocalFile(QStringLiteral(VISIONRT_SOURCE_DIR "/src/ui/qml/NodeBorderContract.qml")));
    std::unique_ptr<QObject> owner(component.create());
    if (!owner)
        throw std::runtime_error(component.errorString().toStdString());
    auto *root = qobject_cast<QQuickItem *>(owner.get());
    require(root != nullptr, "test scene root missing");

    QQuickWindow window;
    window.resize(480, 200);
    root->setParentItem(window.contentItem());
    window.show();
    require(QTest::qWaitForWindowExposed(&window), "test window not exposed");

    auto *node1   = findItem(root, "flowNode_1");
    auto *node2   = findItem(root, "flowNode_2");
    auto *loader1 = findItem(root, "nodeDashedBorderLoader_1");
    auto *loader2 = findItem(root, "nodeDashedBorderLoader_2");
    require(node1 && node2 && loader1 && loader2, "node delegates or border loaders missing");

    // Normal unselected node has NO dashed border item and uses its own solid border
    require(!loader1->property("active").toBool(), "unselected normal node instantiated a dashed border");
    require(node1->property("border").value<QObject *>()->property("width").toInt() == 1,
            "unselected normal node lost its solid border");

    // Missing unselected node stays on a solid NodeMissing border without dashed warning frame or warning labels
    require(!loader2->property("active").toBool(), "missing unselected node must not activate a dashed border");
    require(node2->property("border").value<QObject *>()->property("width").toInt() == 1,
            "missing unselected node lost its solid border");
    require(node2->property("border").value<QObject *>()->property("color").value<QColor>()
                == VisionRTColor::getInstance()->NodeMissing(),
            "missing unselected node did not use NodeMissing border color");
    {
        QStringList missingTexts;
        const auto collectText = [&](auto &&self, QQuickItem *parent) -> void
        {
            for (auto *child : parent->childItems())
            {
                if (child->isVisible() && child->property("text").isValid())
                    missingTexts << child->property("text").toString();
                self(self, child);
            }
        };
        collectText(collectText, node2);
        for (const auto &text : missingTexts)
            require(!text.contains(QStringLiteral("缺失")) && !text.contains(QStringLiteral("删除")),
                    "missing node card must not show warning or missing prompt text");
        require(missingTexts.contains(QStringLiteral("工具")) && missingTexts.contains(QStringLiteral("v1.0"))
                    && missingTexts.contains(QStringLiteral("节点-2")),
                "missing node card did not keep normal card text");
    }
    QTest::qWait(50);
    {
        const QImage grab = window.grabWindow();
        require(!grab.isNull(), "failed to grab missing node comparison frame");
        const qreal  dpr     = grab.devicePixelRatio();
        const QRectF bounds2 = node2->mapRectToScene(node2->boundingRect());
        const QRect  header2(qRound(bounds2.left() * dpr), qRound(bounds2.top() * dpr),
                             qRound(bounds2.width() * dpr), qRound(20 * dpr));
        require(hasColorNear(grab, header2, VisionRTColor::getInstance()->NodeMissing()),
                "missing node header did not render NodeMissing color");
        QDir().mkpath(QStringLiteral(VISIONRT_BINARY_DIR "/evidence"));
        require(grab.save(QStringLiteral(VISIONRT_BINARY_DIR "/evidence/node-missing-card.png")),
                "failed to save missing node card evidence");
    }

    // Selecting missing node 2 swaps its single border from solid to dashed highlight
    root->setProperty("selectedId", QStringLiteral("2"));
    QTest::qWait(50);
    require(loader2->property("active").toBool(), "selected missing node did not activate dashed border");
    require(node2->property("border").value<QObject *>()->property("width").toInt() == 0,
            "selected missing node stacked a solid border under its dashed border");

    // Selecting node 1 swaps its single border from solid to dashed highlight
    root->setProperty("selectedId", QStringLiteral("1"));
    QTest::qWait(50);
    require(loader1->property("active").toBool(), "selected node did not activate dashed border");
    require(node1->property("border").value<QObject *>()->property("width").toInt() == 0,
            "selected node stacked a solid border under its dashed border");

    for (double scale : {0.5, 1.0, 2.0})
    {
        root->setProperty("scaleValue", scale);
        QTest::qWait(50);
        const QImage grab = window.grabWindow();
        require(!grab.isNull(), "failed to grab dashed border frame");
        const qreal  dpr    = grab.devicePixelRatio();
        const QRectF bounds = node1->mapRectToScene(node1->boundingRect());
        const QRect  topEdge(qRound(bounds.left() * dpr), qRound(bounds.top() * dpr) - 2,
                             qRound(bounds.width() * dpr), qRound(6 * dpr));
        require(hasColorNear(grab, topEdge, quickui::QuiColor::getInstance()->Highlight()),
                "selected node dashed border did not render highlight pixels on top edge");
    }

    window.hide();
    std::cout << "NODE_DISPLAY_DEBOUNCE_CONTRACT_PASS\n";
    return 0;
}
catch (const std::exception &error)
{
    std::cerr << error.what() << '\n';
    return 1;
}
