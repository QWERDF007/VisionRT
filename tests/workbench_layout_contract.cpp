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
#include <QSignalSpy>
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
        if ([&] { for (auto *type = child->metaObject(); type; type = type->superClass())
                if (QString(type->className()).contains(fragment)) return true; return false; }() && child->isVisible()
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
    initializeUiStyle();
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
        auto *mainToolbar = window->findChild<QQuickItem *>("mainToolbar");
        require(mainToolbar && mainToolbar->height() == 36, "main toolbar must be 36px high");
        for (auto *button : headerButtons)
        {
            const bool iconOnly = button->property("display").toInt() == 0;
            require(button->height() == (iconOnly ? 28 : 32), "toolbar button height is incorrect");
            require(!iconOnly || button->width() == 28, "standalone toolbar icon button must be square");
            require(button->property("iconSize").toInt() == 20, "toolbar icons must be 20px");
        }
        require(std::abs(sceneRect(headerButtons[0]).left() - sceneRect(mainToolbar).left() - 5) < 0.1,
                "toolbar left padding must be 5px");
        for (int index = 1; index < 4; ++index)
            require(std::abs(sceneRect(headerButtons[index]).left() - sceneRect(headerButtons[index - 1]).right() - 5) < 0.1,
                    "menu button gaps must be 5px");
        auto *imageToolbar = window->findChild<QQuickItem *>("imageToolbar");
        require(imageToolbar && imageToolbar->width() == 36, "viewport toolbar must be 36px wide");
        QList<QQuickItem *> imageButtons;
        auto *viewportActions = imageToolbar->findChild<QQuickItem *>("viewportActions");
        require(viewportActions != nullptr, "viewport actions group is missing");
        collectByClass(viewportActions, "QuiTextIconButton", imageButtons);
        require(imageButtons.size() == 6, "viewport toolbar must expose six standard actions, found " + std::to_string(imageButtons.size()));
        for (auto *button : imageButtons)
            require(button->width() == 28 && button->height() == 28
                        && button->property("iconSize").toInt() == 20,
                    "viewport toolbar actions must be 28px with 20px icons");
        requireDisjoint(headerButtons, "header controls overlap");
        QList<QQuickItem *> parameterTabs;
        collectByClass(window->contentItem(), "QuiTabButton", parameterTabs);
        for (auto *tab : parameterTabs)
        {
            if (tab->property("text").toString() != QStringLiteral("检测"))
                continue;
            QTest::mouseClick(window, Qt::LeftButton, Qt::NoModifier,
                              tab->mapToScene(QPointF(tab->width() / 2, tab->height() / 2)).toPoint());
            require(QTest::qWaitFor([&] { return tab->property("checked").toBool(); }),
                    "parameter tab click did not change the active tab");
        }
        QSignalSpy tabFrameReady(window, &QQuickWindow::frameSwapped);
        window->update();
        require(QTest::qWaitFor([&] { return !tabFrameReady.isEmpty(); }), "tab layout did not render");
        const auto tabImage = window->grabWindow();
        require(tabImage.save(QStringLiteral(VISIONRT_SOURCE_DIR "/build/evidence/parameter_tabs_%1.png")
                                  .arg(QString::number(scale, 'f', 1))), "cannot capture parameter tabs");
        auto *resultsPanel = imageToolbar->parentItem()->parentItem();
        auto *viewport = findItem(window->contentItem(), "imageViewport");
        auto *library = findItem(window->contentItem(), "library");
        require(viewport && library, "split boundary controls are missing");
        const auto resultsRect = sceneRect(resultsPanel);
        const auto viewportRect = sceneRect(viewport);
        require(std::abs(viewportRect.bottom() - resultsRect.bottom()) < 0.1
                    && std::abs(viewportRect.top() - resultsRect.top()) < 0.1,
                "results viewport must meet its split boundaries without background bands");
        const auto splitColor = quickui::QuiColor::getInstance()->Border();
        const auto white = quickui::QuiColor::getInstance()->Button();
        const int splitX = qFloor(resultsRect.left() * dpr);
        const int libraryY = qRound(sceneRect(library).center().y() * dpr);
        const int flowY = qRound(sceneRect(surface).center().y() * dpr);
        for (const int y : {libraryY, flowY})
        {
            require(tabImage.pixelColor(splitX - 1, y) == splitColor,
                    "main split boundary must retain its gray handle");
            require(tabImage.pixelColor(splitX - qCeil(2 * dpr), y) == white
                        && tabImage.pixelColor(splitX, y) == white,
                    "main split boundary must not include adjacent panel borders or background bands");
        }
        const int viewportX = qRound(viewportRect.center().x() * dpr);
        const int splitY = qFloor(resultsRect.bottom() * dpr);
        require(tabImage.pixelColor(viewportX, splitY - 1)
                    == viewport->parentItem()->property("color").value<QColor>(),
                "image content must extend to the horizontal split without an extra border");
        int checkedTabs = 0;
        for (auto *tab : parameterTabs)
        {
            if (!QStringList{"基本输入", "预处理", "检测", "结果处理"}.contains(tab->property("text").toString()))
                continue;
            const auto rect = sceneRect(tab);
            const auto border = quickui::QuiColor::getInstance()->Border();
            const int rightPixel = static_cast<int>(std::ceil(rect.right() * dpr - 0.5));
            const int bottomPixel = static_cast<int>(std::ceil(rect.bottom() * dpr - 0.5));
            for (int offset = 1; offset <= qRound(2 * dpr); ++offset)
            {
                require(tabImage.pixelColor(rightPixel - offset,
                                            qRound(rect.center().y() * dpr)) == border,
                        "parameter tabs must have a 2px right separator: " + tab->property("text").toString().toStdString()
                            + " x=" + std::to_string(rect.right()) + " y=" + std::to_string(rect.center().y())
                            + " actual=" + tabImage.pixelColor(rightPixel - offset, qRound(rect.center().y() * dpr)).name().toStdString());
                const auto bottomColor = tab->property("checked").toBool()
                    ? quickui::QuiColor::getInstance()->Highlight() : border;
                require(tabImage.pixelColor(qRound(rect.center().x() * dpr),
                                            bottomPixel - offset) == bottomColor,
                        "parameter tabs must have a 2px gray baseline or blue active indicator");
            }
            ++checkedTabs;
        }
        require(checkedTabs == 4, "parameter tab visual checks missed a tab");
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
