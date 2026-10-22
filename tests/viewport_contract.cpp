#ifdef NDEBUG
#    undef NDEBUG
#endif

#include "ui/DisplayConverter.h"
#include "ui/ImageViewport.h"

#include <QCoreApplication>
#include <QElapsedTimer>
#include <QGuiApplication>
#include <QKeyEvent>
#include <QMouseEvent>
#include <QQuickWindow>
#include <QSGRendererInterface>
#include <QThread>
#include <array>
#include <cassert>
#include <cmath>
#include <functional>
#include <iostream>
#include <limits>

using namespace visionrt;

namespace {
bool near(double left, double right)
{
    return std::abs(left - right) < 0.001;
}

template<class Predicate>
void await(Predicate predicate)
{
    QElapsedTimer timer;
    timer.start();
    while (!predicate() && timer.elapsed() < 10000)
    {
        QCoreApplication::processEvents();
        QThread::msleep(1);
    }
    assert(predicate());
}

QImage renderedFrame(QQuickWindow &window)
{
    QObject context;
    bool ready = false;
    QObject::connect(&window, &QQuickWindow::frameSwapped, &context, [&] { ready = true; });
    window.update();
    await([&] { return ready; });
    return window.grabWindow();
}

void mouse(ImageViewport &view, QEvent::Type type, QPointF position, Qt::MouseButton button, Qt::MouseButtons buttons)
{
    QMouseEvent event(type, position, position, button, buttons, Qt::NoModifier);
    QCoreApplication::sendEvent(&view, &event);
}

void drag(ImageViewport &view, QPointF start, QPointF end)
{
    mouse(view, QEvent::MouseButtonPress, view.imageToItem(start), Qt::LeftButton, Qt::LeftButton);
    mouse(view, QEvent::MouseMove, view.imageToItem(end), Qt::NoButton, Qt::LeftButton);
    mouse(view, QEvent::MouseButtonRelease, view.imageToItem(end), Qt::LeftButton, Qt::NoButton);
}
} // namespace

int main(int argc, char **argv)
{
    QGuiApplication application(argc, argv);
    cv::Mat         pixels(1, 5, CV_32FC1);
    pixels.at<float>(0, 0) = -10;
    pixels.at<float>(0, 1) = 0;
    pixels.at<float>(0, 2) = 10;
    pixels.at<float>(0, 3) = std::numeric_limits<float>::quiet_NaN();
    pixels.at<float>(0, 4) = std::numeric_limits<float>::infinity();
    const auto source      = pixels;
    const auto display     = DisplayConverter::convert(source);
    assert(display.image.pixelColor(0, 0) == QColor(0, 0, 0));
    assert(display.image.pixelColor(1, 0) == QColor(128, 128, 128));
    assert(display.image.pixelColor(2, 0) == QColor(255, 255, 255));
    assert(display.image.pixelColor(3, 0) == QColor(0, 0, 0));
    assert(display.image.pixelColor(4, 0) == QColor(0, 0, 0));
    assert(!display.warning.isEmpty());
    assert(source.at<float>(0, 0) == -10);
    assert(source.at<float>(0, 2) == 10);
    assert(std::isnan(source.at<float>(0, 3)));
    const auto constant = DisplayConverter::convert(cv::Mat(2, 2, CV_16UC1, cv::Scalar(600)));
    assert(constant.image.pixelColor(1, 1) == QColor(128, 128, 128));
    const auto color = cv::Mat(2, 2, CV_8UC3, cv::Scalar(1, 2, 3));
    assert(DisplayConverter::convert(color).image.pixelColor(0, 0) == QColor(3, 2, 1));
    assert(DisplayConverter::convert(color, "RGB").image.pixelColor(0, 0) == QColor(1, 2, 3));
    const auto unsupported = DisplayConverter::convert(cv::Mat(2, 2, CV_8UC2, cv::Scalar(1, 2)));
    assert(unsupported.image.isNull() && !unsupported.error.isEmpty());
    DisplayConverter          converter;
    std::vector<DisplayFrame> accepted;
    QObject::connect(&converter, &DisplayConverter::batchConverted,
                     [&](const std::vector<DisplayFrame> &frames) { accepted = frames; });
    converter.requestBatch(DataRef<Image>(Image{source}), {}, 1, 1);
    converter.requestBatch(DataRef<Image>(Image{color}), {}, 2, 1);
    converter.requestBatch(DataRef<Image>(Image{color, source}), {}, 3, 1);
    await([&] { return !accepted.empty(); });
    assert(accepted.size() == 2 && accepted[0].requestId == 3 && accepted[1].batchIndex == 1);
    assert(accepted[0].image.pixelColor(0, 0) == QColor(3, 2, 1));
    accepted.clear();
    converter.requestBatch(DataRef<Image>(Image{source}), {}, 4, 1);
    converter.invalidate(2);
    converter.requestBatch(DataRef<Image>(Image{color}), {}, 1, 2);
    await([&] { return !accepted.empty(); });
    assert(accepted[0].generation == 2 && accepted[0].requestId == 1);

    QQuickWindow window;
    window.resize(400, 300);
    ImageViewport viewport(window.contentItem());
    viewport.setSize(QSizeF(400, 300));
    auto frame      = DisplayConverter::convert(cv::Mat(100, 200, CV_8UC1, cv::Scalar(45)));
    frame.requestId = 1;
    viewport.setDisplayFrame(frame);
    assert(near(viewport.zoom(), 2));
    assert(viewport.imageToItem({0, 0}) == QPointF(0, 50));
    viewport.fitCover();
    assert(near(viewport.zoom(), 3));
    viewport.setWidth(600);
    assert(near(viewport.zoom(), 3));
    viewport.fitContain();
    viewport.zoomBy(2);
    const auto center = viewport.itemToImage({300, 150});
    viewport.setWidth(500);
    assert(near(viewport.zoom(), 6));
    assert(viewport.itemToImage({250, 150}) == center);
    viewport.fitOneToOne();
    assert(near(viewport.zoom(), 1 / window.effectiveDevicePixelRatio()));
    viewport.zoomBy(2);
    viewport.beginRoiEdit({9007199254740993ULL, "first", 0, -1}, {});
    int                commits = 0;
    std::optional<ROI::value_type::value_type> committed;
    RoiEditTarget      committedTarget;
    QObject::connect(&viewport, &ImageViewport::roiCommitted,
                     [&](const RoiEditTarget &target, const ROI::value_type::value_type &roi)
                     {
                         ++commits;
                         committed       = roi;
                         committedTarget = target;
                     });
    viewport.setDrawShape(2);
    drag(viewport, {30, 40}, {10, 20});
    assert(commits == 1 && roiKind(*committed) == RoiKind::Rectangle);
    assert(committedTarget.nodeSeq == 9007199254740993ULL && committedTarget.inputPortId == "first");
    assert(committedTarget.roiIndex == -1);
    assert(near((*committed)[1], 10) && near((*committed)[2], 20));
    assert(near((*committed)[3], 20) && near((*committed)[4], 20));
    mouse(viewport, QEvent::MouseButtonPress, viewport.imageToItem({70, 70}), Qt::LeftButton, Qt::LeftButton);
    mouse(viewport, QEvent::MouseMove, viewport.imageToItem({90, 90}), Qt::NoButton, Qt::LeftButton);
    viewport.cancelDraft();
    mouse(viewport, QEvent::MouseButtonRelease, viewport.imageToItem({90, 90}), Qt::LeftButton, Qt::NoButton);
    assert(commits == 1);
    viewport.beginRoiEdit({4, "second", 0, -1}, {});
    viewport.setDrawShape(4);
    for (const QPointF point : {QPointF(10, 10), QPointF(30, 10), QPointF(30, 30)})
    {
        mouse(viewport, QEvent::MouseButtonPress, viewport.imageToItem(point), Qt::LeftButton, Qt::LeftButton);
        mouse(viewport, QEvent::MouseButtonRelease, viewport.imageToItem(point), Qt::LeftButton, Qt::NoButton);
    }
    mouse(viewport, QEvent::MouseButtonDblClick, viewport.imageToItem({30, 30}), Qt::LeftButton, Qt::LeftButton);
    assert(commits == 2 && committedTarget.inputPortId == "second");
    assert((*committed).size() == 7);
    viewport.setReadOnly(true);
    viewport.setDrawShape(3);
    drag(viewport, {10, 10}, {20, 20});
    assert(commits == 2);

    viewport.beginRoiEdit({4, "line", 2, -1}, {});
    viewport.setDrawShape(1);
    mouse(viewport, QEvent::MouseButtonPress, viewport.imageToItem({10, 10}), Qt::LeftButton, Qt::LeftButton);
    viewport.setWidth(700);
    mouse(viewport, QEvent::MouseMove, viewport.imageToItem({30, 40}), Qt::NoButton, Qt::LeftButton);
    mouse(viewport, QEvent::MouseButtonRelease, viewport.imageToItem({30, 40}), Qt::LeftButton, Qt::NoButton);
    assert(commits == 2);
    drag(viewport, {10, 10}, {30, 40});
    assert(commits == 3 && roiKind(*committed) == RoiKind::Segment);
    assert(committedTarget.imageIndex == 2 && (*committed)[1] == 10);
    assert(near((*committed)[3], 30) && near((*committed)[4], 40));
    viewport.beginRoiEdit({4, "circle", 0, -1}, {});
    viewport.setDrawShape(3);
    drag(viewport, {100, 50}, {110, 50});
    assert(commits == 4 && near((*committed)[3], 10));
    viewport.beginRoiEdit({4, "points", 0, -1}, {});
    viewport.setDrawShape(5);
    mouse(viewport, QEvent::MouseButtonPress, viewport.imageToItem({20, 30}), Qt::LeftButton, Qt::LeftButton);
    mouse(viewport, QEvent::MouseButtonRelease, viewport.imageToItem({20, 30}), Qt::LeftButton, Qt::NoButton);
    QKeyEvent enter(QEvent::KeyPress, Qt::Key_Return, Qt::NoModifier);
    QCoreApplication::sendEvent(&viewport, &enter);
    assert(commits == 5 && roiKind(*committed) == RoiKind::PointSet && (*committed).size() == 3);

    viewport.beginRoiEdit(
        {
            4, "rotated", 0, 0
    },
        {ROI::value_type::value_type{2., 40, 30, 40, 20, 0}});
    viewport.setDrawShape(0);
    const double rotationArm = 10 + 24 / viewport.zoom();
    drag(viewport, {60, 40 - rotationArm}, {60 + rotationArm, 40});
    assert(commits == 6 && committedTarget.roiIndex == 0 && near((*committed)[5], 90));
    drag(viewport, {60, 40}, {65, 47});
    assert(commits == 7 && near((*committed)[1], 45) && near((*committed)[2], 37));
    assert(near((*committed)[5], 90));
    viewport.beginRoiEdit(
        {
            4, "rotated", 0, 0
    },
        {ROI::value_type::value_type{2., 40, 30, 40, 20, 90}});
    drag(viewport, {70, 20}, {80, 10});
    assert(commits == 8 && near((*committed)[1], 40) && near((*committed)[2], 20));
    assert(near((*committed)[3], 50) && near((*committed)[4], 30));
    viewport.beginRoiEdit(
        {
            4, "rotated", 0, 0
    },
        {ROI::value_type::value_type{2., 40, 30, 40, 20, 0}});
    mouse(viewport, QEvent::MouseButtonPress, viewport.imageToItem({40, 30}), Qt::LeftButton, Qt::LeftButton);
    mouse(viewport, QEvent::MouseMove, viewport.imageToItem({30, 20}), Qt::NoButton, Qt::LeftButton);
    mouse(viewport, QEvent::MouseButtonRelease, viewport.imageToItem({80, 50}), Qt::LeftButton, Qt::NoButton);
    assert(commits == 8); // Invalid final geometry must not commit the last valid mouse move.
    int deletions = 0;
    QObject::connect(&viewport, &ImageViewport::roiDeleted,
                     [&](const RoiEditTarget &key)
                     {
                         ++deletions;
                         assert(key.inputPortId == "rotated" && key.roiIndex == 0);
                     });
    viewport.setReadOnly(true);
    viewport.deleteSelectedRoi();
    assert(deletions == 0);
    viewport.setReadOnly(false);
    viewport.deleteSelectedRoi();
    assert(deletions == 1);
    viewport.setDrawShape(2);
    mouse(viewport, QEvent::MouseButtonPress, viewport.imageToItem({70, 70}), Qt::LeftButton, Qt::LeftButton);
    viewport.beginRoiEdit({4, "switched", 1, -1}, {});
    mouse(viewport, QEvent::MouseButtonRelease, viewport.imageToItem({90, 90}), Qt::LeftButton, Qt::NoButton);
    assert(commits == 8);
    DisplayFrame missing;
    missing.requestId = 2;
    viewport.setDisplayFrame(missing);
    assert(!viewport.hasImage() && !viewport.statusText().isEmpty());
    drag(viewport, {10, 10}, {30, 30});
    assert(commits == 8);
    viewport.setDisplayFrame(frame); // Stale id cannot restore a disappeared image.
    assert(!viewport.hasImage());
    frame.requestId = 3;
    viewport.setDisplayFrame(frame);

    viewport.endRoiEdit();
    viewport.fitContain();
    viewport.setSize(QSizeF(400, 300));
    viewport.setOverlays(
        {
            ROI::value_type::value_type{2., 20, 20, 40, 40, 0}
    },
        {});
    window.show();
    await([&] { return window.isExposed(); });
    const auto rendered = renderedFrame(window);
    assert(!rendered.isNull());
    const double dpr      = window.effectiveDevicePixelRatio();
    std::cout << "[Viewport rendering] API=" << window.rendererInterface()->graphicsApi() << " DPR=" << dpr << '\n';
    const auto   interior = rendered.pixelColor(qRound(200 * dpr), qRound(150 * dpr));
    assert(std::abs(interior.red() - 45) <= 1 && std::abs(interior.green() - 45) <= 1);
    const auto yellow = rendered.pixelColor(qRound(40 * dpr), qRound(130 * dpr));
    const bool yellowVisible = yellow.red() - yellow.blue() > 100 && yellow.green() - yellow.blue() > 80;
    if (!yellowVisible)
    {
        rendered.save("viewport_rendering_failure.png");
        std::cerr << "ROI boundary pixel: " << yellow.name().toStdString() << '\n';
    }
    assert(yellowVisible);
    viewport.setOverlays(
        {
            {0., 35., 35.}
    },
        {});
    const auto pointFrame    = renderedFrame(window);
    const auto pointLocation = viewport.imageToItem({35., 35.}) * dpr;
    const auto pointColor    = pointFrame.pixelColor(qRound(pointLocation.x()), qRound(pointLocation.y()));
    assert(pointColor.red() > 240 && pointColor.green() > 200 && pointColor.blue() < 10);
    viewport.setOverlays(
        {
            ROI::value_type::value_type{2., 20, 20, 40, 40, 0}
    },
        {ROI::value_type::value_type{2., 20, 20, 40, 40, 0}});
    const auto overlaid = renderedFrame(window);
    const auto blue     = overlaid.pixelColor(qRound(40 * dpr), qRound(130 * dpr));
    if (!(blue.blue() - blue.red() > 100 && blue.green() - blue.red() > 50))
    {
        overlaid.save("viewport_overlap_failure.png");
        std::cerr << "ROI overlap pixel: " << blue.name().toStdString() << '\n';
    }
    assert(blue.blue() - blue.red() > 100 && blue.green() - blue.red() > 50);
    viewport.setOverlays(
        {
            ROI::value_type::value_type{2., 100, 10, 40, 20, 0}
    },
        {ROI::value_type::value_type{2., 20, 20, 40, 40, 0}});
    viewport.beginRoiEdit(
        {
            4, "layers", 0, 0
    },
        {ROI::value_type::value_type{2., 20, 20, 40, 40, 0}}, false, true);
    const auto editingLayers = renderedFrame(window);
    const auto selectedPoint = viewport.imageToItem({20, 20}) * dpr;
    const auto selectedColor = editingLayers.pixelColor(qRound(selectedPoint.x()), qRound(selectedPoint.y()));
    assert(selectedColor.red() > 240 && selectedColor.green() > 200 && selectedColor.blue() < 10);
    const auto sourcePoint = viewport.imageToItem({120, 10}) * dpr;
    const auto sourceColor = editingLayers.pixelColor(qRound(sourcePoint.x()), qRound(sourcePoint.y()));
    assert(sourceColor.red() - sourceColor.blue() > 100 && sourceColor.green() - sourceColor.blue() > 80);
    viewport.endRoiEdit();
    auto replacement      = DisplayConverter::convert(cv::Mat(100, 200, CV_8UC1, cv::Scalar(82)));
    replacement.requestId = 4;
    viewport.setDisplayFrame(replacement);
    const auto refreshed       = renderedFrame(window);
    const auto refreshedCenter = refreshed.pixelColor(qRound(200 * dpr), qRound(150 * dpr));
    assert(std::abs(refreshedCenter.red() - 82) <= 1);
    const std::array<std::function<void()>, 6> transforms = {
        [&] { viewport.zoomBy(1.5); }, [&] { viewport.fitContain(); }, [&] { viewport.fitCover(); },
        [&] { viewport.fitOneToOne(); }, [&] { viewport.setWidth(viewport.width() + 30); },
        [&] { viewport.restoreViewState({ImageViewport::Manual, 2, {50, 30}}); }};
    for (const auto &transform : transforms)
    {
        const auto original = ROI::value_type::value_type{2., 20, 20, 40, 20, 25};
        viewport.beginRoiEdit({4, "transform", 0, 0}, {original}, false, true);
        viewport.setDrawShape(0);
        const int before = commits;
        mouse(viewport, QEvent::MouseButtonPress, viewport.imageToItem({40, 30}), Qt::LeftButton, Qt::LeftButton);
        mouse(viewport, QEvent::MouseMove, viewport.imageToItem({45, 35}), Qt::NoButton, Qt::LeftButton);
        transform();
        mouse(viewport, QEvent::MouseButtonRelease, viewport.imageToItem({55, 40}), Qt::LeftButton, Qt::NoButton);
        assert(commits == before);
        drag(viewport, {40, 30}, {45, 35});
        assert(commits == before + 1 && near((*committed)[1], 25) && near((*committed)[2], 25));
        assert(near((*committed)[5], 25));
    }
    const auto    original = ROI::value_type::value_type{2., 40, 30, 40, 20, 25};
    const auto    corners  = rectangleCorners(original);
    const QPointF rectangleCenter(60, 40);
    for (std::size_t corner = 0; corner < corners.size(); ++corner)
    {
        viewport.beginRoiEdit({4, "corner", 0, 0}, {original}, false, true);
        viewport.setDrawShape(0);
        const int before = commits;
        const QPointF start(corners[corner].x, corners[corner].y);
        drag(viewport, start, rectangleCenter + (start - rectangleCenter) * 1.5);
        assert(commits == before + 1 && near((*committed)[5], 25));
        const auto adjusted = rectangleCorners(*committed);
        const auto opposite = (corner + 2) % 4;
        assert(near(adjusted[opposite].x, corners[opposite].x)
            && near(adjusted[opposite].y, corners[opposite].y));
    }
    viewport.beginRoiEdit({4, "rotation", 0, 0}, {original}, false, true);
    viewport.setDrawShape(0);
    QTransform rotation;
    rotation.rotate(25);
    const double arm = 10 + 24 / viewport.zoom();
    const int beforeRotation = commits;
    drag(viewport, rectangleCenter + rotation.map(QPointF(0, -arm)),
         rectangleCenter + rotation.map(QPointF(arm, 0)));
    assert(commits == beforeRotation + 1 && near((*committed)[5], 115));
    for (std::size_t component = 1; component < 5; ++component)
        assert(near((*committed)[component], original[component]));
    viewport.beginRoiEdit({4, "paired", 0, -1, 91}, {}, false, true);
    viewport.setDrawShape(2);
    const int beforePaired = commits;
    mouse(viewport, QEvent::MouseButtonPress, viewport.imageToItem({10, 10}), Qt::RightButton, Qt::RightButton);
    mouse(viewport, QEvent::MouseMove, viewport.imageToItem({20, 20}), Qt::NoButton, Qt::RightButton);
    mouse(viewport, QEvent::MouseButtonPress, viewport.imageToItem({30, 30}), Qt::LeftButton,
          Qt::LeftButton | Qt::RightButton);
    mouse(viewport, QEvent::MouseMove, viewport.imageToItem({50, 50}), Qt::NoButton,
          Qt::LeftButton | Qt::RightButton);
    mouse(viewport, QEvent::MouseButtonRelease, viewport.imageToItem({50, 50}), Qt::LeftButton, Qt::RightButton);
    assert(commits == beforePaired);
    mouse(viewport, QEvent::MouseButtonRelease, viewport.imageToItem({50, 50}), Qt::RightButton, Qt::NoButton);
    drag(viewport, {10, 10}, {30, 30});
    assert(commits == beforePaired + 1 && committedTarget.session == 91);

    viewport.beginRoiEdit({4, "paired", 0, -1, 92}, {}, false, true);
    viewport.setDrawShape(2);
    const int beforeRelease = commits;
    mouse(viewport, QEvent::MouseButtonPress, viewport.imageToItem({10, 10}), Qt::LeftButton, Qt::LeftButton);
    mouse(viewport, QEvent::MouseMove, viewport.imageToItem({30, 30}), Qt::NoButton, Qt::LeftButton);
    mouse(viewport, QEvent::MouseButtonPress, viewport.imageToItem({30, 30}), Qt::RightButton,
          Qt::LeftButton | Qt::RightButton);
    mouse(viewport, QEvent::MouseButtonRelease, viewport.imageToItem({30, 30}), Qt::RightButton, Qt::LeftButton);
    assert(commits == beforeRelease);
    mouse(viewport, QEvent::MouseButtonRelease, viewport.imageToItem({30, 30}), Qt::LeftButton, Qt::NoButton);
    assert(commits == beforeRelease + 1 && committedTarget.session == 92);
    assert(near((*committed)[1], 10) && near((*committed)[2], 10) && near((*committed)[3], 20)
           && near((*committed)[4], 20));
    mouse(viewport, QEvent::MouseButtonRelease, viewport.imageToItem({40, 40}), Qt::LeftButton, Qt::NoButton);
    assert(commits == beforeRelease + 1);
    viewport.beginRoiEdit({4, "paired", 0, -1, 93}, {}, true, true);
    mouse(viewport, QEvent::MouseButtonPress, viewport.imageToItem({10, 10}), Qt::LeftButton, Qt::LeftButton);
    viewport.setReadOnly(false);
    mouse(viewport, QEvent::MouseButtonRelease, viewport.imageToItem({30, 30}), Qt::LeftButton, Qt::NoButton);
    assert(commits == beforeRelease + 1);
    for (const double scale : {0.5, 1.0, 4.0})
    {
        viewport.restoreViewState({ImageViewport::Manual, scale, {50, 40}});
        viewport.beginRoiEdit(
            {
                4, "small", 0, 0
        },
            {ROI::value_type::value_type{2., 40, 30, 2, 2, 0}}, false, true);
        viewport.setDrawShape(0);
        const int beforeSmall = commits;
        drag(viewport, {42, 32}, {46, 35});
        assert(commits == beforeSmall + 1);
        assert(near((*committed)[1], 40) && near((*committed)[2], 30) && near((*committed)[3], 6)
               && near((*committed)[4], 5));
        viewport.beginRoiEdit(
            {
                4, "tie", 0, 0
        },
            {ROI::value_type::value_type{2., 40, 30, 2, 2, 0}}, false, true);
        drag(viewport, {41, 31}, {39, 29});
        assert(near((*committed)[1], 39) && near((*committed)[2], 29) && near((*committed)[3], 3)
               && near((*committed)[4], 3));
        const auto    rotatedSmall = ROI::value_type::value_type{2., 40, 30, 2, 2, 25};
        const auto    smallCorners = rectangleCorners(rotatedSmall);
        const QPointF smallCenter(41, 31);
        const QPointF smallCorner(smallCorners[2].x, smallCorners[2].y);
        viewport.beginRoiEdit({4, "rotated-small", 0, 0}, {rotatedSmall}, false, true);
        drag(viewport, smallCorner, smallCenter + (smallCorner - smallCenter) * 3);
        const auto resizedCorners = rectangleCorners(*committed);
        assert(near(resizedCorners[0].x, smallCorners[0].x) && near(resizedCorners[0].y, smallCorners[0].y)
               && near((*committed)[3], 4) && near((*committed)[4], 4) && near((*committed)[5], 25));
    }
    viewport.endRoiEdit();
    viewport.beginRoiEdit(
        {
            4, "precision", 0, 0
    },
        {{2., 100000000.125, 100000000.25, 20.5, 10.75, 0.}}, false, true);
    viewport.restoreViewState({
        ImageViewport::Manual,
        4,
        {100000010.375, 100000005.625}
    });
    viewport.setDrawShape(0);
    const auto preciseBefore = commits;
    drag(viewport, {100000010.375, 100000005.625}, {100000010.5, 100000005.875});
    assert(commits == preciseBefore + 1);
    assert(near((*committed)[1], 100000000.25) && near((*committed)[2], 100000000.5));
    assert(near((*committed)[3], 20.5) && near((*committed)[4], 10.75));
    viewport.endRoiEdit();
    viewport.setSize(QSizeF(320, 200));
    viewport.setPosition(QPointF(40, 50));
    viewport.restoreViewState({ImageViewport::Manual, 2, {0, 20}});
    viewport.setOverlays({}, {});
    window.setColor(QColor(18, 32, 46));
    const auto beyondImage = ROI::value_type::value_type{2., -90, 5, 40, 30, 0};
    viewport.beginRoiEdit({4, "viewport-edge", 0, 0}, {beyondImage}, false, true);
    viewport.setDrawShape(0);
    const int beforeEdge = commits;
    const auto clipped = renderedFrame(window);
    const auto outsideViewport = clipped.pixelColor(qRound(20 * dpr), qRound(120 * dpr));
    assert(outsideViewport == QColor(18, 32, 46));
    const auto visibleHandle = viewport.mapToScene(viewport.imageToItem({-50, 5}));
    const auto edgeHandleColor = clipped.pixelColor(qRound(visibleHandle.x() * dpr), qRound(visibleHandle.y() * dpr));
    assert(edgeHandleColor.red() > 240 && edgeHandleColor.green() > 200 && edgeHandleColor.blue() < 10);
    assert(commits == beforeEdge);
    const auto windowMouse = [&](QEvent::Type type, const QPointF &position, Qt::MouseButton button, Qt::MouseButtons buttons)
    {
        QMouseEvent event(type, position, window.mapToGlobal(position.toPoint()), button, buttons, Qt::NoModifier);
        QCoreApplication::sendEvent(&window, &event);
    };
    const auto adjustedHandle = viewport.mapToScene(viewport.imageToItem({-40, 0}));
    windowMouse(QEvent::MouseButtonPress, visibleHandle, Qt::LeftButton, Qt::LeftButton);
    windowMouse(QEvent::MouseMove, adjustedHandle, Qt::NoButton, Qt::LeftButton);
    windowMouse(QEvent::MouseButtonRelease, adjustedHandle, Qt::LeftButton, Qt::NoButton);
    assert(commits == beforeEdge + 1 && committedTarget.inputPortId == "viewport-edge");
    assert(near((*committed)[1], -90) && near((*committed)[2], 0) && near((*committed)[3], 50)
           && near((*committed)[4], 35));
    const auto clippedAgain = renderedFrame(window);
    assert(clippedAgain.pixelColor(qRound(20 * dpr), qRound(120 * dpr)) == QColor(18, 32, 46));
    std::cout << "[Viewport edge] Out-of-image geometry remains unchanged by viewport clipping; visible handles retain source coordinates.\n";
    viewport.endRoiEdit();
    int windowCancellations = 0;
    QObject::connect(&viewport, &ImageViewport::roiEditCanceled, &viewport,
                     [&]
                     {
                         ++windowCancellations;
                         viewport.endRoiEdit();
                     });
    QQuickWindow relocatedWindow;
    relocatedWindow.resize(400, 300);
    relocatedWindow.show();
    window.requestActivate();
    await([&] { return window.isActive(); });
    viewport.beginRoiEdit(
        {
            5, "window-owner", 0, 0, 100
    },
        {ROI::value_type::value_type{2., 10, 10, 20, 20, 0}}, false, true);
    viewport.setParentItem(relocatedWindow.contentItem());
    assert(windowCancellations == 1 && !viewport.editing() && viewport.selectedRoi() == -1
        && viewport.drawShape() == 0);
    relocatedWindow.requestActivate();
    await([&] { return relocatedWindow.isActive(); });
    viewport.beginRoiEdit(
        {
            5, "window-owner", 0, 0, 101
    },
        {ROI::value_type::value_type{2., 10, 10, 20, 20, 0}}, false, true);
    viewport.setParentItem(nullptr);
    assert(windowCancellations == 2 && !viewport.editing() && viewport.selectedRoi() == -1
        && viewport.drawShape() == 0);
    viewport.setParentItem(window.contentItem());
    window.requestActivate();
    await([&] { return window.isActive(); });
    viewport.beginRoiEdit(
        {
            5, "window-owner", 0, 0, 102
    },
        {ROI::value_type::value_type{2., 10, 10, 20, 20, 0}}, false, true);
    relocatedWindow.requestActivate();
    await([&] { return relocatedWindow.isActive() && !window.isActive(); });
    assert(windowCancellations == 3 && !viewport.editing() && viewport.selectedRoi() == -1
        && viewport.drawShape() == 0);
    viewport.setParentItem(nullptr);
    assert(windowCancellations == 3);
    std::cout << "[Viewport owning window] Reparenting and detaching cancel once; reattaching observes the current window without retaining editing state.\n";
    window.hide();
    ImageViewport stable;
    stable.setWidth(200);
    stable.setHeight(200);
    stable.setDisplayFrame(DisplayConverter::convert(cv::Mat(100, 100, CV_8UC1, cv::Scalar(0))));
    const RoiEditTarget stableTarget{99, "idempotent", 0, -1, 7};
    stable.beginRoiEdit(stableTarget, {});
    stable.setDrawShape(2);
    int stableCommits = 0;
    QObject::connect(&stable, &ImageViewport::roiCommitted, &stable,
                     [&](const RoiEditTarget &, const ROI::value_type::value_type &) { ++stableCommits; });
    mouse(stable, QEvent::MouseButtonPress, stable.imageToItem({10, 10}), Qt::LeftButton, Qt::LeftButton);
    mouse(stable, QEvent::MouseMove, stable.imageToItem({30, 30}), Qt::NoButton, Qt::LeftButton);
    stable.beginRoiEdit(stableTarget, {});
    mouse(stable, QEvent::MouseButtonRelease, stable.imageToItem({30, 30}), Qt::LeftButton, Qt::NoButton);
    assert(stableCommits == 1);
    return 0;
}
