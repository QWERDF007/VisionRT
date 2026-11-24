#include "WorkbenchFixtures.h"
#include "core/ValueJson.h"
#include "ui/AppController.h"
#include "ui/ImageViewport.h"
#include "ui/UiTypes.h"

#include <QDir>
#include <QColor>
#include <QGuiApplication>
#include <QQmlComponent>
#include <QQmlContext>
#include <QQmlEngine>
#include <QQuickItemGrabResult>
#include <QQuickStyle>
#include <QQuickWindow>
#include <QSignalSpy>
#include <QTest>
#include <iostream>
#include <memory>
#include <stdexcept>

using namespace visionrt;

int main(int argc, char **argv)
try
{
    QGuiApplication application(argc, argv);
    QQuickStyle::setStyle("Basic");
    registerUiTypes();
    qmlRegisterType<ImageViewport>("VisionRT.View", 1, 0, "ImageViewport");
    const auto require = [](bool condition, const char *message)
    {
        if (!condition)
            throw std::runtime_error(message);
    };
    testing::WorkbenchInstallation installation;
    const auto                     images = installation.directory() + "/images";
    require(QDir().mkpath(images), "cannot create image directory");
    QImage image(256, 256, QImage::Format_RGB888);
    image.fill(QColor(231, 64, 17));
    require(image.save(images + "/1.png"), "cannot save first image");
    AppController app(2, installation.directory());
    const auto    settle = [&]
    {
        require(QTest::qWaitFor([&] { return app.workspaceEnabled(); }, 5000), "workbench did not settle");
        QTest::qWait(100);
        require(QTest::qWaitFor([&] { return app.workspaceEnabled(); }, 5000), "workbench did not synchronize");
    };
    settle();
    app.addNode("ReadImage", 0, 0);
    settle();
    const auto source = app.selectedNode();
    require(app.setParameter(1, false, images), "image directory was rejected");
    settle();
    app.addNode("CreateCircle", 0, 100);
    settle();
    const auto creator = app.selectedNode();
    require(app.bindInput(app.portContext(2), source, 2), "drawing image could not be connected");
    settle();
    app.runOnce();
    settle();

    QQmlEngine engine;
    engine.addImportPath(QStringLiteral(VISIONRT_BINARY_DIR "/qml"));
    engine.rootContext()->setContextProperty("app", &app);
    for (const auto *name : {"demoMenu", "demoFileMenu", "demoSettings", "demoSelect"})
        engine.rootContext()->setContextProperty(name, false);
    QQmlComponent component(&engine, QUrl::fromLocalFile(QStringLiteral(VISIONRT_SOURCE_DIR "/src/ui/qml/Main.qml")));
    std::unique_ptr<QObject> owner(component.create());
    if (!owner)
        throw std::runtime_error(component.errorString().toStdString());
    auto *window = qobject_cast<QQuickWindow *>(owner.get());
    require(window != nullptr, "workbench window is missing");
    require(app.metaObject()->indexOfProperty("roiEditor") == -1,
            "the standalone ROI coordinate editor is still exposed");
    for (const auto *signature : {"setRoiCoordinate(QString,int,QString,bool)", "cancelRoiCoordinates()",
                                  "setRoiRectangle(QString,QVariantList)", "setRoiFieldsValid(QString,bool)",
                                  "applyRoi()", "roiApplyRequested()"})
        require(app.metaObject()->indexOfMethod(signature) == -1,
                "a standalone ROI coordinate editing command is still exposed");
    for (auto *object : window->findChildren<QObject *>())
        require(object->property("text").toString() != QStringLiteral("编辑输入 ROI"),
                "the global ROI editing button is still available");
    window->showMaximized();
    require(QTest::qWaitForWindowExposed(window), "workbench window was not exposed");
    auto *viewport = window->findChild<ImageViewport *>("imageViewport");
    require(viewport != nullptr, "image viewport is missing");
    require(QTest::qWaitFor([&] { return viewport->hasImage(); }, 3000), "drawing image was not presented");
    settle();
    require(app.clearRoi(1), "ROI could not be cleared");
    settle();
    require(app.startRoiDraw(1, ImageViewport::DrawCircle), "circle drawing did not start");
    require(QTest::qWaitFor([&] { return viewport->editing() && viewport->hasImage(); }, 3000),
            "drawing session did not reach the viewport");
    const auto capture = [&](const char *name)
    {
        auto result = viewport->grabToImage();
        require(result && QTest::qWaitFor([&] { return !result->image().isNull(); }, 3000),
                "viewport capture did not finish");
        const auto rendered = result->image();
        rendered.save(QStringLiteral(VISIONRT_BINARY_DIR "/evidence/") + name + ".png");
        return rendered;
    };
    const auto hasRoi = [](const QImage &rendered)
    {
        for (int y = 0; y < rendered.height(); ++y)
            for (int x = 0; x < rendered.width(); ++x)
            {
                const auto color = rendered.pixelColor(x, y);
                if (color.blue() > 180 && color.green() > 60 && color.green() < 170 && color.red() < 80)
                    return true;
            }
        return false;
    };
    const auto start = viewport->mapToScene(viewport->imageToItem({100, 100})).toPoint();
    const auto end   = viewport->mapToScene(viewport->imageToItem({130, 100})).toPoint();
    QTest::mousePress(window, Qt::LeftButton, {}, start);
    QTest::mouseMove(window, end, 20);
    require(hasRoi(capture("roi-persistence-drawing")), "circle was not visible during drawing");
    QTest::mouseRelease(window, Qt::LeftButton, {}, end);
    QTest::qWait(1200);
    require(hasRoi(capture("roi-persistence-released")), "drawn ROI disappeared after mouse release");
    for (const auto &value : app.parameters())
    {
        const auto row = value.toMap();
        if (row.value("init").toBool() || row.value("id").toInt() != 1)
            continue;
        const auto roi = std::get<DataRef<ROI>>(ValueJson::parse(row.value("literal").toString(), {Type::ROI}));
        require(roi.get().size() == 1 && roi.get().front().size() == 1,
                "drawn ROI was not committed to the actual input");
    }
    app.endRoiPreview();
    settle();
    require(hasRoi(capture("roi-persistence-ended")),
            "input ROI must remain visible and blue after leaving its editor");
    QTest::mouseClick(window, Qt::LeftButton, {}, viewport->mapToScene(viewport->imageToItem({100, 100})).toPoint());
    require(QTest::qWaitFor([&] { return app.activeRoiParameter() == 1 && viewport->selectedRoi() == 0; }, 3000),
            "committed input ROI could not be selected again");
    window->requestActivate();
    require(QTest::qWaitForWindowActive(window), "workbench did not become active");
    QQuickWindow otherWindow;
    otherWindow.resize(160, 100);
    otherWindow.show();
    otherWindow.requestActivate();
    require(QTest::qWaitForWindowActive(&otherWindow), "focus test window did not become active");
    require(QTest::qWaitFor([&] { return !viewport->editing(); }, 3000), "focus switch did not end editing");
    settle();
    require(hasRoi(capture("roi-persistence-focus")), "input ROI disappeared or changed color after losing focus");
    otherWindow.close();
    window->requestActivate();
    require(QTest::qWaitForWindowActive(window), "workbench focus did not return");
    app.selectNode(source);
    settle();
    app.selectNode(creator);
    settle();
    require(hasRoi(capture("roi-persistence-reselected")), "input ROI disappeared after switching nodes");
    QTest::mouseClick(window, Qt::LeftButton, {}, viewport->mapToScene(viewport->imageToItem({100, 100})).toPoint());
    require(QTest::qWaitFor([&] { return app.activeRoiParameter() == 1 && viewport->selectedRoi() == 0; }, 3000),
            "input ROI could not be selected after returning to its node");
    const auto circle = [&]
    {
        for (const auto &value : app.parameters())
        {
            const auto row = value.toMap();
            if (!row.value("init").toBool() && row.value("id").toUInt() == 1)
                return std::get<DataRef<ROI>>(ValueJson::parse(row.value("literal").toString(), {Type::ROI})).get().at(0).at(0);
        }
        throw std::runtime_error("circle parameter is missing");
    };
    require(window->findChild<QObject *>("roiCoordinate0") == nullptr,
            "selecting an ROI restored the standalone coordinate editor");
    const auto beforeX = circle()[1];
    QTest::keyClick(window, Qt::Key_Right);
    require(QTest::qWaitFor(
                [&] { return circle()[1] == beforeX + 1; }, 3000),
            "input ROI could not be edited after a focus switch");
    QTest::mouseClick(window, Qt::LeftButton, {}, viewport->mapToScene(viewport->imageToItem({220, 220})).toPoint());
    QTest::qWait(150);
    require(viewport->selectedRoi() == -1 && hasRoi(capture("roi-persistence-deselected")),
            "deselecting the input ROI hid it or changed its color");
    require(window->grabWindow().save(QStringLiteral(VISIONRT_BINARY_DIR "/evidence/roi-entry-removal.png")),
            "workbench evidence could not be saved");
    QTest::mouseMove(window, viewport->mapToScene(viewport->imageToItem({220.5, 220.5})).toPoint());
    auto *search = window->findChild<QQuickItem *>("flowSearch");
    require(search != nullptr, "flow search field is missing");
    search->forceActiveFocus();
    require(search->hasActiveFocus(), "flow search did not receive keyboard focus");
    require(QTest::qWaitFor([&] { return viewport->pixelText().isEmpty(); }, 1000),
            "pixel tooltip appeared without Alt");
    QTest::keyPress(window, Qt::Key_Alt);
    if (!QTest::qWaitFor([&] { return viewport->pixelText() == QStringLiteral("220,220\nR: 231\nG: 64\nB: 17"); }, 1000)
        || !search->hasActiveFocus())
        throw std::runtime_error((QStringLiteral("Alt pixel tooltip text=%1, focus=%2, pointer=%3,%4")
            .arg(viewport->pixelText()).arg(viewport->hasActiveFocus())
            .arg(viewport->pointerPosition().x()).arg(viewport->pointerPosition().y())).toStdString());
    auto *pixelTooltip = window->findChild<QObject *>("pixelTooltip");
    require(pixelTooltip && QTest::qWaitFor([&] { return pixelTooltip->property("visible").toBool(); }, 1000),
            "Alt pixel tooltip did not become visible");
    require(QTest::qWaitFor([&] { return pixelTooltip->property("opened").toBool(); }, 2000),
            "pixel tooltip did not finish opening");
    auto *tipContent = qobject_cast<QQuickItem *>(pixelTooltip->property("contentItem").value<QObject *>());
    require(tipContent && tipContent->isVisible() && tipContent->width() > 0 && tipContent->height() > 0,
            "pixel tooltip has no visible content");
    QSignalSpy tooltipFrames(window, &QQuickWindow::frameSwapped);
    window->update();
    require(tooltipFrames.wait(3000), "pixel tooltip did not render a frame");
    require(tipContent->isVisible() && pixelTooltip->property("opened").toBool(),
            "pixel tooltip closed while Alt remained pressed");
    const auto tooltipFrame = window->grabWindow();
    const auto tipBounds = tipContent->mapRectToScene(tipContent->boundingRect()).toAlignedRect().intersected(tooltipFrame.rect());
    bool textRendered = false;
    for (int y = tipBounds.top(); y <= tipBounds.bottom(); ++y)
        for (int x = tipBounds.left(); x <= tipBounds.right(); ++x)
        {
            const auto color = tooltipFrame.pixelColor(x, y);
            textRendered = textRendered || (color.red() < 40 && color.green() < 40 && color.blue() < 40);
        }
    require(textRendered, "pixel tooltip text was absent from the actual rendered frame");
    require(tooltipFrame.save(QStringLiteral(VISIONRT_BINARY_DIR "/evidence/pixel-alt-tooltip.png")),
            "pixel tooltip evidence could not be saved");
    QTest::keyRelease(window, Qt::Key_Alt);
    require(QTest::qWaitFor([&] { return viewport->pixelText().isEmpty() && !pixelTooltip->property("visible").toBool(); }, 1000),
            "pixel tooltip remained visible after releasing Alt");
    app.addNode("RotateImage", 0, 200);
    settle();
    require(app.setParameter(2, false, "1.123456789"), "precise float fixture was rejected");
    settle();
    auto *panel = window->findChild<QObject *>("parameterPanel");
    require(panel && panel->setProperty("currentTab", static_cast<int>(ParameterTab::Detection)),
            "detection parameter page is missing");
    QQuickItem *floatInput = nullptr;
    const bool floatReady = QTest::qWaitFor([&]
    {
        QList<QQuickItem *> pending{window->contentItem()};
        while (!pending.isEmpty())
        {
            auto *item = pending.takeLast();
            if (item->property("text").toString() == "1.123457")
                if (auto *input = qobject_cast<QQuickItem *>(item->property("inputItem").value<QObject *>()))
                    floatInput = input;
            pending.append(item->childItems());
        }
        return floatInput != nullptr;
    }, 3000);
    require(floatReady, "float input did not display six decimal places");
    floatInput->forceActiveFocus();
    QTest::keyClick(window, Qt::Key_Return);
    viewport->forceActiveFocus();
    QTest::qWait(20);
    for (const auto &value : app.parameters())
    {
        const auto row = value.toMap();
        if (row.value("id").toUInt() == 2)
            require(row.value("literal").toString().toDouble() == 1.123456789,
                    "unmodified float input wrote its rounded display value");
    }
    require(app.setParameter(2, false, "1"), "whole-valued float fixture was rejected");
    settle();
    require(QTest::qWaitFor([&] { return floatInput->property("text").toString() == "1"; }, 1000),
            "whole-valued float inputs must omit trailing decimal zeros");
    require(window->grabWindow().save(QStringLiteral(VISIONRT_BINARY_DIR "/evidence/parameter-six-decimals.png")),
            "six-decimal parameter evidence could not be saved");
    std::cout << "Drawn circle remains visible after release and leaving the editor.\n";
    return 0;
}
catch (const std::exception &error)
{
    std::cerr << error.what() << '\n';
    return 1;
}
