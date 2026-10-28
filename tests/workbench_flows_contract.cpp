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

void click(QQuickWindow &window, QQuickItem *item)
{
    require(item && item->isVisible(), "flow fixture target is not visible");
    QTest::mouseClick(&window, Qt::LeftButton, Qt::NoModifier, item->mapToScene(item->boundingRect().center()).toPoint());
}

QQuickItem *dialogButton(QObject *dialog, const QString &text)
{
    if (!dialog)
        return nullptr;
    for (auto *item : dialog->findChildren<QQuickItem *>())
        if (item->isVisible() && item->property("text").toString() == text)
            return item;
    return nullptr;
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
    require(QTest::qWaitFor([&] { return !app.busy(); }, 5000), "flow catalog did not become ready");
    QQmlEngine engine;
    quickui::QuiColor::getInstance()->setThemeMode(quickui::QuiColor::Light);
    engine.addImportPath(QStringLiteral(VISIONRT_BINARY_DIR "/qml"));
    engine.rootContext()->setContextProperty("app", &app);
    QQmlComponent component(&engine,
                            QUrl::fromLocalFile(QStringLiteral(VISIONRT_SOURCE_DIR "/src/ui/qml/FlowCanvas.qml")));
    std::unique_ptr<QObject> owner(component.create());
    if (!owner)
        throw std::runtime_error(component.errorString().toStdString());
    auto *canvas = qobject_cast<QQuickItem *>(owner.get());
    require(canvas, "flow canvas root is not an item");
    QQuickWindow window;
    window.resize(960, 640);
    canvas->setParentItem(window.contentItem());
    canvas->setSize(QSizeF(960, 640));
    window.show();
    require(QTest::qWaitForWindowExposed(&window), "flow canvas window was not exposed");
    window.requestActivate();
    require(QTest::qWaitForWindowActive(&window), "flow canvas window was not activated");
    const auto idle = [&]
    {
        require(QTest::qWaitFor([&] { return !app.busy(); }, 5000), "flow operation did not finish");
    };
    const auto flows = [&]
    {
        return app.flows();
    };
    const auto findFlow = [&](const QString &id) -> QVariantMap
    {
        for (const auto &value : flows())
            if (value.toMap().value("id").toString() == id)
                return value.toMap();
        return {};
    };
    auto *add = findItem(canvas, "flowAddButton");
    require(add, "flow add button is missing");
    QSet<QString> names;
    for (const auto &value : flows()) names.insert(value.toMap().value("name").toString());
    const auto initial = flows().size();
    for (int count = 0; count < 3; ++count)
    {
        click(window, add);
        idle();
        const auto current = flows();
        require(current.size() == initial + count + 1, "flow add button did not create exactly one flow");
        const auto added = current.back().toMap();
        require(added.value("id").toString() == app.currentFlow(), "new flow did not become active");
        const auto name = added.value("name").toString();
        require(!name.isEmpty() && !names.contains(name), "new flow name is empty or duplicated");
        names.insert(name);
        auto *dialog = canvas->findChild<QObject *>("flowNameDialog");
        require(!dialog || !dialog->property("visible").toBool(), "adding a flow opened a naming dialog");
        auto *tab = findItem(canvas, "flowTab_" + added.value("id").toString());
        require(tab, "new flow tab is missing");
        bool titled = false;
        const auto findTitle = [&](auto &&self, QQuickItem *parent) -> void
        {
            for (auto *item : parent->childItems())
            {
                titled = titled || item->property("text").toString() == name;
                self(self, item);
            }
        };
        findTitle(findTitle, tab);
        require(titled, "flow tab title does not match the project name");
    }
    const auto target = flows().front().toMap();
    auto *tab       = findItem(canvas, "flowTab_" + target.value("id").toString());
    require(tab, "first flow tab is missing");
    require(QTest::qWaitFor([&] { return tab->width() > 0 && tab->isVisible(); }, 2000),
            "first flow tab did not lay out");
    QTest::qWait(100);
    click(window, tab);
    idle();
    require(app.currentFlow() == target.value("id").toString(), "tab click did not activate its flow");

    auto *dropdown = findItem(canvas, "flowDropdown");
    require(dropdown, "flow dropdown is missing");
    click(window, dropdown);
    idle();
    auto *popup = canvas->findChild<QObject *>("selectFlowPopup");
    require(popup && popup->property("visible").toBool(), "select flow popup did not open");

    auto *toggle = canvas->findChild<QQuickItem *>("flowToggle_" + target.value("id").toString());
    if (!toggle)
        toggle = findItem(window.contentItem(), "flowToggle_" + target.value("id").toString());
    require(toggle, "flow toggle item in popup is missing");
    const bool selected = target.value("selected").toBool();
    click(window, toggle);
    idle();
    require(findFlow(target.value("id").toString()).value("selected").toBool() != selected,
            "flow toggle did not change its run selection");

    auto *indicator = findItem(canvas, "flowTabIndicator_" + target.value("id").toString());
    require(indicator, "flow tab indicator is missing");
    const bool nowSelected = findFlow(target.value("id").toString()).value("selected").toBool();
    const auto indicatorColor = indicator->property("color").value<QColor>();
    require(nowSelected ? (indicatorColor == QColor("#2da44e")) : (indicatorColor == QColor("transparent")),
            "indicator color did not reflect run selection");

    click(window, dropdown);
    idle();
    auto *close = findItem(canvas, "flowTabClose_" + target.value("id").toString());
    require(close, "flow close control is missing");
    click(window, close);
    auto *dialog = canvas->findChild<QObject *>("flowDeleteDialog");
    require(dialog && QTest::qWaitFor([&] { return dialog->property("visible").toBool(); }, 2000),
            "flow close did not open the confirmation dialog");
    auto *cancel = dialogButton(dialog, "取消");
    require(cancel, "flow delete dialog has no cancel button");
    click(window, cancel);
    require(QTest::qWaitFor([&] { return !dialog->property("visible").toBool(); }, 2000)
                && !findFlow(target.value("id").toString()).isEmpty(),
            "cancel deleted the flow or left the dialog open");
    click(window, close);
    require(QTest::qWaitFor([&] { return dialog->property("visible").toBool(); }, 2000),
            "second close did not reopen the confirmation dialog");
    auto *confirm = dialogButton(dialog, "确认");
    require(confirm, "flow delete dialog has no confirm button");
    click(window, confirm);
    idle();
    require(findFlow(target.value("id").toString()).isEmpty(), "confirm did not delete the target flow");
    require(!app.currentFlow().isEmpty() && !findFlow(app.currentFlow()).isEmpty(), "deleted flow left no active flow");
    while (flows().size() > 1)
    {
        app.deleteFlow(flows().last().toMap().value("id").toString());
        idle();
    }
    const auto remaining = flows().front().toMap().value("id").toString();
    auto *lastClose      = findItem(canvas, "flowTabClose_" + remaining);
    require(lastClose && QTest::qWaitFor([&] { return !lastClose->property("enabled").toBool(); }, 2000),
            "the last flow still offers deletion");
    click(window, lastClose);
    QTest::qWait(100);
    require(!dialog->property("visible").toBool(), "the last flow opened a delete confirmation");
    auto *lastTab = findItem(canvas, "flowTab_" + remaining);
    require(lastTab, "remaining flow tab is missing");
    QTest::mouseClick(&window, Qt::RightButton, Qt::NoModifier,
                      lastTab->mapToScene(lastTab->boundingRect().center()).toPoint());
    auto *menu = canvas->findChild<QObject *>("flowContextMenu");
    require(menu && QTest::qWaitFor([&] { return menu->property("visible").toBool(); }, 2000),
            "right click did not open the flow menu");
    QTest::keyClick(&window, Qt::Key_Escape);
    std::cout << "WORKBENCH_FLOWS_PASS added=3 tabs=4 cancel=1 confirm=1 last_guarded=1 menu=1\n";
    window.hide();
    return 0;
}
catch (const std::exception &error)
{
    std::cerr << error.what() << '\n';
    return 1;
}
