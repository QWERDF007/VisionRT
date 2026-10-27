// Contract for the viewport side of smart point selection (T-U02).
// Real window, real hover and mouse events: hover reports image coordinates, a click reports the
// append modifier, a drag pans instead of committing, and Esc cancels.
#include "ui/DisplayConverter.h"
#include "ui/ImageViewport.h"

#include <QGuiApplication>
#include <QImage>
#include <QQuickWindow>
#include <QSignalSpy>
#include <QTest>
#include <iostream>
#include <stdexcept>

using namespace visionrt;

namespace {
void require(bool condition, const char *message)
{
    if (!condition)
        throw std::runtime_error(message);
}
} // namespace

int main(int argc, char **argv)
try
{
    QGuiApplication application(argc, argv);
    QQuickWindow    window;
    window.resize(320, 240);
    ImageViewport viewport;
    viewport.setParentItem(window.contentItem());
    viewport.setSize({320, 240});
    window.show();
    require(QTest::qWaitForWindowExposed(&window), "assist viewport window was not exposed");
    window.requestActivate();
    require(QTest::qWaitForWindowActive(&window), "assist viewport window was not activated");

    QImage pixels(320, 240, QImage::Format_RGB32);
    pixels.fill(QColor(30, 30, 30));
    viewport.setGeneration(1);
    DisplayFrame frame;
    frame.image      = pixels;
    frame.requestId  = 1;
    frame.generation = 1;
    viewport.setDisplayFrame(frame);
    require(viewport.hasImage(), "assist viewport did not present its image");

    QSignalSpy hover(&viewport, &ImageViewport::assistHover);
    QSignalSpy clicked(&viewport, &ImageViewport::assistClicked);
    QSignalSpy canceled(&viewport, &ImageViewport::assistCanceled);

    // Without assist the same gestures stay inert, so normal read-only browsing is unaffected.
    QTest::mouseMove(&window, QPoint(160, 120));
    QTest::mouseClick(&window, Qt::LeftButton, Qt::NoModifier, QPoint(160, 120));
    require(hover.isEmpty() && clicked.isEmpty(), "read-only browsing emitted an assist interaction");

    viewport.setAssistActive(true);
    viewport.forceActiveFocus(Qt::MouseFocusReason);
    QTest::mouseMove(&window, QPoint(160, 120));
    require(QTest::qWaitFor([&] { return !hover.isEmpty(); }, 2000), "hover was not reported");
    const auto point = hover.last().at(0).toPointF();
    require(std::abs(point.x() - 160.0) <= 1.0 && std::abs(point.y() - 120.0) <= 1.0,
            "hover was not reported in image coordinates");

    QTest::mouseClick(&window, Qt::LeftButton, Qt::NoModifier, QPoint(160, 120));
    require(QTest::qWaitFor([&] { return clicked.count() == 1; }, 2000), "left click was not reported");
    require(clicked.last().at(1).toBool() == false, "a plain click must not append");

    QTest::mouseClick(&window, Qt::LeftButton, Qt::ControlModifier, QPoint(160, 120));
    require(QTest::qWaitFor([&] { return clicked.count() == 2; }, 2000), "Ctrl+click was not reported");
    require(clicked.last().at(1).toBool(), "Ctrl+click must request an append");

    // A drag is an inspection gesture, never a selection.
    const int beforeDrag = clicked.count();
    QTest::mousePress(&window, Qt::LeftButton, Qt::NoModifier, QPoint(120, 120));
    QTest::mouseMove(&window, QPoint(150, 120), 10);
    QTest::mouseRelease(&window, Qt::LeftButton, Qt::NoModifier, QPoint(150, 120));
    require(QTest::qWaitFor([&] { return clicked.count() == beforeDrag; }, 300),
            "a drag beyond the start distance must not commit a candidate");

    QTest::keyClick(&window, Qt::Key_Escape);
    require(QTest::qWaitFor([&] { return canceled.count() == 1; }, 2000), "Esc did not cancel the assist mode");

    viewport.setAssistActive(false);
    const int beforeIdleMove = hover.count();
    QTest::mouseMove(&window, QPoint(160, 120));
    QTest::qWait(300);
    require(hover.count() == beforeIdleMove, "assist hover kept reporting after deactivation");

    std::cout << "ASSIST_VIEWPORT_PASS hover=image click=1 ctrl=append drag=0 esc=cancel\n";
    return 0;
}
catch (const std::exception &error)
{
    std::cerr << "[Assist viewport error] " << error.what() << '\n';
    return 1;
}
