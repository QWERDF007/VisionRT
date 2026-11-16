#include "WorkbenchFixtures.h"
#include "ui/AppController.h"
#include "ui/ImageViewport.h"
#include "ui/Theme.h"
#include <quickui/Color.h>

#include <QCoreApplication>
#include <QDir>
#include <QFileInfo>
#include <QGuiApplication>
#include <QFontMetricsF>
#include <QFontInfo>
#include <QProcess>
#include <QQmlComponent>
#include <QQmlContext>
#include <QQmlEngine>
#include <QQuickItem>
#include <QQuickStyle>
#include <QQuickWindow>
#include <QTest>
#include <cmath>
#include <iostream>
#include <memory>
#include <stdexcept>


using namespace visionrt;

namespace {
void require(bool condition, const std::string &message)
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

void collectByClass(QQuickItem *parent, const QString &fragment, QList<QQuickItem *> &result)
{
    for (auto *child : parent->childItems())
    {
        if (QString(child->metaObject()->className()).contains(fragment) && child->isVisible()
            && child->width() > 0 && child->height() > 0)
            result.push_back(child);
        collectByClass(child, fragment, result);
    }
}

void collectItems(QQuickItem *parent, QList<QQuickItem *> &result)
{
    for (auto *child : parent->childItems())
    {
        result.push_back(child);
        collectItems(child, result);
    }
}

QRectF sceneRect(QQuickItem *item)
{
    return QRectF(item->mapToScene(QPointF(0, 0)), item->size());
}

bool overlaps(QQuickItem *first, QQuickItem *second)
{
    const auto left  = sceneRect(first).adjusted(1, 1, -1, -1);
    const auto right = sceneRect(second).adjusted(1, 1, -1, -1);
    return left.intersects(right);
}

void requireDisjoint(const QList<QQuickItem *> &items, const char *message)
{
    for (int left = 0; left < items.size(); ++left)
        for (int right = left + 1; right < items.size(); ++right)
            require(!overlaps(items[left], items[right]),
                    std::string(message) + ": " + items[left]->objectName().toStdString() + " and "
                        + items[right]->objectName().toStdString());
}

bool containsText(QQuickItem *parent, const QString &text)
{
    for (auto *child : parent->childItems())
    {
        if (child->isVisible() && child->property("text").toString() == text)
            return true;
        if (containsText(child, text))
            return true;
    }
    return false;
}
}

int main(int argc, char **argv)
try
{
    if (argc >= 3 && std::string(argv[1]) == "--layout-child")
    {
        const double scale = std::stod(argv[2]);
        qputenv("QT_SCALE_FACTOR", QByteArray::number(scale, 'f', 1));
        QGuiApplication application(argc, argv);
    registerStyleModule();
        QQuickStyle::setStyle("Basic");
        testing::WorkbenchInstallation installation;
        AppController                  app(1, installation.directory());
        // Catalog loading is asynchronous and this contract starts a fresh process per scale; give
        // it a generous budget and keep the failure diagnosable.
        const bool ready
            = QTest::qWaitFor([&] { return !app.busy() && !app.flows().isEmpty(); }, 15000);
        require(ready, ("layout catalog did not become ready: " + app.runState() + " flows="
                        + QString::number(app.flows().size()) + " " + app.diagnostics().join(QStringLiteral(" | ")))
                           .toStdString());
        QQmlEngine engine;
        quickui::QuiColor::getInstance()->setThemeMode(quickui::QuiColor::Light);
        qmlRegisterType<visionrt::ImageViewport>("VisionRT.View", 1, 0, "ImageViewport");
        engine.addImportPath(QStringLiteral(VISIONRT_BINARY_DIR "/qml"));
        engine.addImportPath(QStringLiteral(VISIONRT_SOURCE_DIR "/src/ui/qml"));
        engine.addImportPath(QStringLiteral(VISIONRT_SOURCE_DIR "/3rdparty/QuickUI/quickui"));
        engine.rootContext()->setContextProperty("app", &app);
        engine.rootContext()->setContextProperty("demoMenu", false);
        engine.rootContext()->setContextProperty("demoSelect", false);
        QQmlComponent component(&engine,
                                QUrl::fromLocalFile(QStringLiteral(VISIONRT_SOURCE_DIR "/src/ui/qml/Main.qml")));
        std::unique_ptr<QObject> owner(component.create());
        if (!owner)
            throw std::runtime_error(component.errorString().toStdString());
        auto *window = qobject_cast<QQuickWindow *>(owner.get());
        require(window != nullptr, "Main.qml root is not a window");
        window->setFlags(Qt::FramelessWindowHint | Qt::Window);
        window->resize(960, 540);
        window->show();
        require(QTest::qWaitForWindowExposed(window), "layout window was not exposed");
        window->requestActivate();
        require(QTest::qWaitForWindowActive(window), "layout window was not activated");
        require(QTest::qWaitFor([&] { return !app.busy(); }, 5000), "layout project did not settle");
        for (int frame = 0; frame < 5; ++frame)
            QCoreApplication::processEvents();
        const double dpr = window->devicePixelRatio();
        require(std::abs(dpr - scale) < 0.05, "layout DPR does not match the requested scale");
        auto *surface = findItem(window->contentItem(), "flowSurface");
        auto *title   = findItem(window->contentItem(), "flowTitle");
        auto *search  = findItem(window->contentItem(), "flowSearch");
        auto *drop    = findItem(window->contentItem(), "flowDropdown");
        auto *fit     = findItem(window->contentItem(), "flowFitButton");
        auto *add     = findItem(window->contentItem(), "flowAddButton");
        require(surface && title && search && drop && fit && add, "layout controls are missing");
        auto *searchBox = search->parentItem() ? search->parentItem()->parentItem() : nullptr;
        require(searchBox != nullptr, "layout search box is missing");
        const QList<QQuickItem *> top{title, searchBox, drop, fit};
        requireDisjoint(top, "top bar controls overlap");
        auto *topBar = title->parentItem() ? title->parentItem()->parentItem() : nullptr;
        require(topBar != nullptr, "layout top bar is missing");
        require(!containsText(topBar, QStringLiteral("设置")), "flow chart still exposes a placeholder settings control");
        QQuickItem *firstTab = nullptr;
        QList<QQuickItem *> allItems;
        collectItems(window->contentItem(), allItems);
        for (auto *child : allItems)
        {
            if (child->objectName().startsWith("flowTab_") && child->isVisible())
            {
                firstTab = child;
                break;
            }
        }
        require(firstTab != nullptr, "layout flow tab is missing");
        require(!overlaps(add, firstTab), "add button overlaps the flow tab");
        QList<QQuickItem *> toolbars;
        collectByClass(window->contentItem(), "ToolBar", toolbars);
        require(!toolbars.isEmpty(), "layout tool bar is missing");
        QList<QQuickItem *> headerButtons;
        collectByClass(toolbars.front(), "QuiTextIconButton", headerButtons);
        require(headerButtons.size() >= 6, "layout header controls are incomplete");
        requireDisjoint(headerButtons, "header controls overlap");
        auto *fileMenu = window->findChild<QObject *>("fileMenu");
        auto *recentMenu = window->findChild<QObject *>("recentMenu");
        require(fileMenu && recentMenu, "file and recent project menus are missing");
        QMetaObject::invokeMethod(fileMenu, "open");
        QMetaObject::invokeMethod(recentMenu, "open");
        require(QTest::qWaitFor([&] { return recentMenu->property("opened").toBool(); }, 1000),
                "recent project submenu did not open");
        for (int index = 0; index < recentMenu->property("count").toInt(); ++index)
        {
            QQuickItem *entry = nullptr;
            QMetaObject::invokeMethod(recentMenu, "itemAt", Qt::DirectConnection,
                                      Q_RETURN_ARG(QQuickItem *, entry), Q_ARG(int, index));
            require(entry != nullptr, "recent project menu entry is missing");
            require(entry->width() >= QFontMetricsF(entry->property("font").value<QFont>())
                                         .horizontalAdvance(entry->property("text").toString())
                                         + entry->property("leftPadding").toDouble() + entry->property("rightPadding").toDouble(),
                    "recent project submenu clips a full path");
        }
        require(window->grabWindow().save(QStringLiteral(VISIONRT_SOURCE_DIR "/build/evidence/recent_paths_menu_%1.png")
                                             .arg(QString::number(scale, 'f', 1))), "recent menu screenshot could not be saved");
        QMetaObject::invokeMethod(recentMenu, "close");
        QMetaObject::invokeMethod(fileMenu, "close");
        require(QTest::qWaitFor([&] { return !fileMenu->property("visible").toBool(); }, 1000), "file menu did not close");
        QQmlComponent pathMenuComponent(&engine);
        pathMenuComponent.setData(R"(
            import QtQuick
            import quickui
            QuiMenu {
                animationEnabled: false
                QuiMenuItem {
                    objectName: "fullPathEntry"
                    text: "F:/Projects/VisionRT/examples/03_综合示例/02_识别线缆颜色/识别线缆颜色.vrt.json"
                }
            }
        )", QUrl());
        std::unique_ptr<QObject> pathMenu(pathMenuComponent.createWithInitialProperties(
            {{"parent", QVariant::fromValue(window->contentItem())}}));
        require(pathMenu != nullptr, pathMenuComponent.errorString().toStdString());
        auto *pathEntry = pathMenu->findChild<QQuickItem *>("fullPathEntry");
        require(pathEntry != nullptr, "full path menu entry is missing");
        QMetaObject::invokeMethod(pathMenu.get(), "open");
        const auto fitsPath = [&] {
            const auto font = pathEntry->property("font").value<QFont>();
            return pathEntry->width() >= QFontMetricsF(font).horizontalAdvance(pathEntry->property("text").toString())
                       + pathEntry->property("leftPadding").toDouble() + pathEntry->property("rightPadding").toDouble();
        };
        require(QTest::qWaitFor(fitsPath, 1000),
                "menu width clips the full project path: menu=" + std::to_string(pathMenu->property("width").toDouble())
                    + " content=" + std::to_string(pathMenu->property("contentWidth").toDouble())
                    + " item=" + std::to_string(pathEntry->width())
                    + " implicit=" + std::to_string(pathEntry->implicitWidth()));
        const auto oldWidth = pathMenu->property("width").toDouble();
        pathEntry->setProperty("text", pathEntry->property("text").toString() + QStringLiteral("/更多.json"));
        require(QTest::qWaitFor([&] { return fitsPath() && pathMenu->property("width").toDouble() > oldWidth; }, 1000),
                "menu width did not follow a longer path");
        const auto longWidth = pathMenu->property("width").toDouble();
        auto pathFont = pathEntry->property("font").value<QFont>();
        pathFont.setPixelSize(QFontInfo(pathFont).pixelSize() + 4);
        pathEntry->setProperty("font", pathFont);
        require(QTest::qWaitFor([&] { return fitsPath() && pathMenu->property("width").toDouble() > longWidth; }, 1000),
                "menu width did not follow the text font: width=" + std::to_string(pathMenu->property("width").toDouble())
                    + " content=" + std::to_string(pathMenu->property("contentWidth").toDouble())
                    + " implicit=" + std::to_string(pathEntry->implicitWidth()));
        const auto pathShot = QStringLiteral(VISIONRT_SOURCE_DIR "/build/evidence/full_path_menu_%1.png")
                                  .arg(QString::number(scale, 'f', 1));
        require(window->grabWindow().save(pathShot), "full path menu screenshot could not be saved");
        pathEntry->setProperty("text", QStringLiteral("F:/短路径.vrt.json"));
        require(QTest::qWaitFor([&] { return fitsPath() && pathMenu->property("width").toDouble() < oldWidth; }, 1000),
                "menu kept the previous long path width");
        QMetaObject::invokeMethod(pathMenu.get(), "close");
        const auto shot = QStringLiteral(VISIONRT_SOURCE_DIR "/build/evidence/workbench_layout_%1.png")
                              .arg(QString::number(scale, 'f', 1));
        require(window->grabWindow().save(shot), "layout screenshot could not be saved");
        std::cout << "WORKBENCH_LAYOUT_PASS scale=" << scale << " dpr=" << dpr << " top=" << top.size()
                  << " header=" << headerButtons.size() << " tab=" << firstTab->objectName().toStdString() << '\n';
        window->hide();
        return 0;
    }

    QCoreApplication application(argc, argv);
    const auto      executable = QCoreApplication::applicationFilePath();
    for (const double scale : {1.0, 1.5, 2.0})
    {
        QProcess process;
        QProcessEnvironment environment = QProcessEnvironment::systemEnvironment();
        environment.insert("QT_SCALE_FACTOR", QString::number(scale, 'f', 1));
        process.setProcessEnvironment(environment);
        process.start(executable, {"--layout-child", QString::number(scale, 'f', 1)});
        require(process.waitForStarted(5000), "layout child did not start");
        require(process.waitForFinished(60000), "layout child timed out");
        std::cout << process.readAllStandardOutput().toStdString();
        const auto error = process.readAllStandardError().toStdString();
        if (!error.empty())
            std::cerr << error;
        require(process.exitCode() == 0, "layout child failed at scale " + std::to_string(scale));
    }
    std::cout << "WORKBENCH_LAYOUT_ALL_PASS scales=3\n";
    return 0;
}
catch (const std::exception &error)
{
    std::cerr << error.what() << '\n';
    return 1;
}
