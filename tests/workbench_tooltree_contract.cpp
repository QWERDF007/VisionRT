#include "WorkbenchFixtures.h"
#include "ui/AppController.h"
#include "ui/Theme.h"
#include "ui/ToolTreeModel.h"
#include <quickui/Color.h>

#include <QAbstractItemModel>
#include <QGuiApplication>
#include <QQmlComponent>
#include <QQmlContext>
#include <QQmlEngine>
#include <QQuickItem>
#include <QQuickWindow>
#include <QSGRendererInterface>
#include <QSignalSpy>
#include <QStyleHints>
#include <QTest>
#include <functional>
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
    std::sort(result.begin(), result.end(), [](QQuickItem *a, QQuickItem *b) {
        return a->property("row").toInt() < b->property("row").toInt();
    });
    return result;
}

QQuickItem *delegateWithKey(QQuickItem *tree, const QString &key)
{
    QList<QQuickItem *> delegates;
    collectDelegates(tree, delegates);
    for (auto *delegate : delegates)
        if (delegate->property("stableKey").toString() == key)
            return delegate;
    return nullptr;
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
    registerStyleModule();
    QQuickWindow::setGraphicsApi(QSGRendererInterface::Software);
    testing::WorkbenchInstallation installation;
    AppController                  app(1, installation.directory());
    require(QTest::qWaitFor([&] { return !app.busy() && app.toolModel()->rowCount() > 0; }, 20000),
            "tool tree catalog did not become ready");
    QQmlEngine engine;
    quickui::QuiColor::getInstance()->setThemeMode(quickui::QuiColor::Light);
    engine.addImportPath(QStringLiteral(VISIONRT_BINARY_DIR "/qml"));
    engine.rootContext()->setContextProperty("app", &app);
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
    require(QTest::qWaitForWindowExposed(&window), "tool tree window was not exposed");
    window.requestActivate();
    require(QTest::qWaitForWindowActive(&window), "tool tree window was not activated");
    auto *tree = findItem(library, "toolTree");
    auto *filter = findItem(library, "toolFilter");
    require(tree && filter, "tool tree or filter is missing");
    require(QTest::qWaitFor([&] { return !categories(tree).isEmpty(); }, 3000),
            "tool tree categories did not lay out");
    auto category = categories(tree).front();
    const auto targetKey = category->property("stableKey").toString();
    require(!targetKey.isEmpty(), "tool tree category stableKey is empty");
    if (category->property("expanded").toBool())
    {
        clickItem(window, category);
        require(QTest::qWaitFor([&] {
                    auto item = delegateWithKey(tree, targetKey);
                    return item && !item->property("expanded").toBool();
                }, 2000),
                "tool tree category did not collapse");
        QTest::qWait(QGuiApplication::styleHints()->mouseDoubleClickInterval() + 50);
    }
    QList<QQuickItem *> collapsed;
    collectDelegates(tree, collapsed);
    category = delegateWithKey(tree, targetKey);
    require(category != nullptr, "tool tree category delegate was not found");
    clickItem(window, category);
    require(QTest::qWaitFor([&] {
                auto item = delegateWithKey(tree, targetKey);
                return item && item->property("expanded").toBool();
            }, 2000),
            "tool tree category did not expand");
    require(QTest::qWaitFor(
                [&]
                {
                    QList<QQuickItem *> expanded;
                    collectDelegates(tree, expanded);
                    return expanded.size() > collapsed.size();
                },
                2000),
            "expanded tool tree category revealed no children");
    QAbstractItemModel *model = app.toolModel();
    QModelIndex          leaf;
    const std::function<void(const QModelIndex &)> findLeaf = [&](const QModelIndex &parent)
    {
        for (int row = 0; row < model->rowCount(parent); ++row)
        {
            const auto index = model->index(row, 0, parent);
            if (model->rowCount(index) > 0)
                findLeaf(index);
            else if (!leaf.isValid())
                leaf = index;
        }
    };
    findLeaf({});
    require(leaf.isValid(), "tool tree catalog has no leaf");
    const auto toolId = model->data(leaf, ToolTreeModel::ToolIdRole).toString();
    const auto toolName = model->data(leaf, Qt::DisplayRole).toString();
    require(!toolId.isEmpty() && toolName.size() >= 2, "tool tree leaf identity is incomplete");
    filter->setProperty("text", toolName.left(2));
    require(QTest::qWaitFor([&] { return delegateWithTool(tree, toolId) != nullptr; }, 3000),
            "tool tree search did not reveal the matching leaf");
    const auto matched = delegateWithTool(tree, toolId);
    require(matched && matched->property("hasChildren").toBool() == false, "matching tool tree leaf is malformed");
    require(QTest::qWaitFor(
                [&]
                {
                    for (auto *item : categories(tree))
                        if (!item->property("expanded").toBool())
                            return false;
                    return true;
                },
                2000),
            "tool tree search did not expand every matching path");
    filter->setProperty("text", "不存在");
    filter->setProperty("text", "");
    require(QTest::qWaitFor(
                [&]
                {
                    for (auto *item : categories(tree))
                        if (item->property("expanded").toBool())
                            return false;
                    return true;
                },
                2000),
            "clearing the tool search left a stale expansion");
    require(QTest::qWaitFor([&] { return delegateWithTool(tree, toolId) == nullptr; }, 2000),
            "clearing the tool search left a stale leaf");
    QSignalSpy toggleSpy(library, SIGNAL(toggleRequested()));
    auto     *toggle = findItem(library, "toolLibraryToggle");
    require(toggle, "tool library toggle is missing");
    clickItem(window, toggle);
    require(toggleSpy.count() == 1, "tool library toggle did not request a collapse");
    library->setProperty("expanded", false);
    require(QTest::qWaitFor([&] { return !tree->isVisible(); }, 2000), "collapsed tool library kept its tree visible");
    library->setProperty("expanded", true);
    require(QTest::qWaitFor([&] { return tree->isVisible(); }, 2000), "expanded tool library kept its tree hidden");
    std::cout << "WORKBENCH_TOOLTREE_PASS collapse=1 search=1 clear=1 toggle=1\n";
    window.hide();
    return 0;
}
catch (const std::exception &error)
{
    std::cerr << error.what() << '\n';
    return 1;
}
