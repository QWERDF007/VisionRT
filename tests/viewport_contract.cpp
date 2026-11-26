#ifdef NDEBUG
#    undef NDEBUG
#endif

#include "ui/DisplayConverter.h"
#include "ui/ImageViewport.h"
#include "ui/Size.h"

#include <QCoreApplication>
#include <QDir>
#include <QElapsedTimer>
#include <QGuiApplication>
#include <QHoverEvent>
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
    bool    ready = false;
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

void hover(ImageViewport &view, QEvent::Type type, QPointF position)
{
    QHoverEvent event(type, position, view.mapToGlobal(position), position, Qt::NoModifier);
    QCoreApplication::sendEvent(&view, &event);
}

int inputBluePixels(const QImage &image, QRectF area, double dpr)
{
    const auto pixels = QRectF(area.topLeft() * dpr, area.size() * dpr).toAlignedRect().intersected(image.rect());
    int count = 0;
    for (int y = pixels.top(); y <= pixels.bottom(); ++y)
        for (int x = pixels.left(); x <= pixels.right(); ++x)
        {
            const auto color = image.pixelColor(x, y);
            if (color.red() < 30 && color.green() > 80 && color.green() < 160 && color.blue() > 220) ++count;
        }
    return count;
}
} // namespace

int main(int argc, char **argv)
{
    QGuiApplication application(argc, argv);
    const auto      evidence = QDir(application.applicationDirPath()).absoluteFilePath("../evidence");
    QDir().mkpath(evidence);
    cv::Mat pixels(1, 5, CV_32FC1);
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
    assert(DisplayConverter::convert(color, ColorOrder::RGB).image.pixelColor(0, 0) == QColor(1, 2, 3));
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
    viewport.beginRoiEdit({9007199254740993ULL, 1, 0, -1}, {});
    int                          commits = 0;
    std::optional<EncodedRegion> committed;
    RoiEditTarget                committedTarget;
    QObject::connect(&viewport, &ImageViewport::roiCommitted,
                     [&](const RoiEditTarget &target, const EncodedRegion &roi)
                     {
                         ++commits;
                         committed       = roi;
                         committedTarget = target;
                     });
    viewport.setDrawShape(2);
    drag(viewport, {30, 40}, {10, 20});
    assert(commits == 1 && roiKind(*committed) == RoiKind::Rectangle);
    assert(committedTarget.nodeSeq == 9007199254740993ULL && committedTarget.inputPortId == 1);
    assert(committedTarget.roiIndex == -1);
    assert(near((*committed)[1], 10) && near((*committed)[2], 20));
    assert(near((*committed)[3], 20) && near((*committed)[4], 20));
    mouse(viewport, QEvent::MouseButtonPress, viewport.imageToItem({70, 70}), Qt::LeftButton, Qt::LeftButton);
    mouse(viewport, QEvent::MouseMove, viewport.imageToItem({90, 90}), Qt::NoButton, Qt::LeftButton);
    viewport.cancelDraft();
    mouse(viewport, QEvent::MouseButtonRelease, viewport.imageToItem({90, 90}), Qt::LeftButton, Qt::NoButton);
    assert(commits == 1);
    viewport.beginRoiEdit({4, 2, 0, -1}, {});
    viewport.setDrawShape(4);
    for (const QPointF point : {QPointF(10, 10), QPointF(30, 10), QPointF(30, 30)})
    {
        mouse(viewport, QEvent::MouseButtonPress, viewport.imageToItem(point), Qt::LeftButton, Qt::LeftButton);
        mouse(viewport, QEvent::MouseButtonRelease, viewport.imageToItem(point), Qt::LeftButton, Qt::NoButton);
    }
    mouse(viewport, QEvent::MouseButtonDblClick, viewport.imageToItem({30, 30}), Qt::LeftButton, Qt::LeftButton);
    assert(commits == 2 && committedTarget.inputPortId == 2);
    assert((*committed).size() == 7);
    viewport.setReadOnly(true);
    viewport.setDrawShape(3);
    drag(viewport, {10, 10}, {20, 20});
    assert(commits == 2);

    viewport.beginRoiEdit({4, 3, 2, -1}, {});
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
    viewport.beginRoiEdit({4, 4, 0, -1}, {});
    viewport.setDrawShape(3);
    drag(viewport, {100, 50}, {110, 50});
    assert(commits == 4 && near((*committed)[3], 10));
    viewport.beginRoiEdit({4, 5, 0, -1}, {});
    viewport.setDrawShape(5);
    mouse(viewport, QEvent::MouseButtonPress, viewport.imageToItem({20, 30}), Qt::LeftButton, Qt::LeftButton);
    mouse(viewport, QEvent::MouseButtonRelease, viewport.imageToItem({20, 30}), Qt::LeftButton, Qt::NoButton);
    QKeyEvent enter(QEvent::KeyPress, Qt::Key_Return, Qt::NoModifier);
    QCoreApplication::sendEvent(&viewport, &enter);
    assert(commits == 5 && roiKind(*committed) == RoiKind::Point && (*committed).size() == 3);

    viewport.endRoiEdit();
    viewport.setSize({400, 300});
    viewport.fitContain();
    viewport.setOverlays({makeRectangle(20, 20, 40, 40)}, {makeCircle({120, 50}, 15)});
    window.show();
    await([&] { return window.isExposed(); });
    const double dpr      = window.effectiveDevicePixelRatio();
    auto         rendered = renderedFrame(window);
    const auto   at       = [&](const QImage &image, QPointF source)
    {
        const auto location = viewport.imageToItem(source) * dpr;
        return image.pixelColor(qRound(location.x()), qRound(location.y()));
    };
    const auto inactive = at(rendered, {20, 40});
    rendered.save(evidence + "/roi-render-control.png");
    std::cerr << "inactive boundary=" << inactive.name().toStdString() << '\n';
    assert(inactive.blue() > 180 && inactive.red() < 100 && inactive.green() > 60);
    assert(at(rendered, {100, 50}).red() == 45);
    mouse(viewport, QEvent::MouseButtonPress, viewport.imageToItem({120, 50}), Qt::LeftButton, Qt::LeftButton);
    mouse(viewport, QEvent::MouseButtonRelease, viewport.imageToItem({120, 50}), Qt::LeftButton, Qt::NoButton);
    rendered                    = renderedFrame(window);
    const auto outputAfterClick = at(rendered, {135, 50});
    assert(outputAfterClick.blue() > 160 && outputAfterClick.red() > 100 && outputAfterClick.green() < 100);
    assert(!viewport.editing() && viewport.selectedRoi() == -1);
    rendered.save(evidence + "/roi-output-click.png");
    viewport.beginRoiEdit({4, 6, 0, 0}, {makeRectangle(20, 20, 40, 20)}, false);
    viewport.setOverlays({}, {});
    for (double scale : {0.1, 1.0, 8.0})
    {
        viewport.restoreViewState({
            ImageViewport::Manual,
            scale,
            {40, 30}
        });
        rendered          = renderedFrame(window);
        const auto stable = renderedFrame(window);
        assert(rendered == stable);
        if (scale >= 1)
        {
            const auto cross = viewport.imageToItem({40, 30}) * dpr;
            const auto interior = rendered.pixelColor(qRound(cross.x() + 4 * dpr), qRound(cross.y() - 6 * dpr));
            assert(interior.red() < 240 && "The center cross handle must have a transparent background");
        }
    }
    viewport.setHostFrame(DisplayConverter::convert(cv::Mat(700, 900, CV_8UC1, cv::Scalar(45))));
    window.resize(900, 700);
    viewport.setSize({900, 700});
    const std::array<cv::Point2d, 4> polygon{cv::Point2d(350, 270), cv::Point2d(550, 270),
                                           cv::Point2d(550, 430), cv::Point2d(350, 430)};
    for (const auto &region : {makeRectangle(300, 270, 300, 160), makeCircle({450, 350}, 100),
                              makeSegment({350, 300}, {550, 400}), makePoint(450, 350), makePolygon(polygon)})
    for (double scale : {0.5, 1.0, 2.0})
    {
        viewport.beginRoiEdit({4, 6, 0, 0}, {region});
        viewport.restoreViewState({ImageViewport::Manual, scale, {450, 350}});
        const auto cross = viewport.imageToItem({450, 350});
        hover(viewport, QEvent::HoverEnter, cross + QPointF(30, 30));
        const auto hidden = renderedFrame(window);
        hover(viewport, QEvent::HoverMove, cross);
        const auto shown = renderedFrame(window);
        assert(shown != hidden && "ROI measurements must appear when hovering the center cross");
        const qreal centerHalf = VisionRTSize::getInstance()->ROICenterHandleSize() / 2.0;
        hover(viewport, QEvent::HoverMove, cross + QPointF(centerHalf - 1.0, centerHalf - 1.0));
        assert(renderedFrame(window) == shown && "ROI labels must stay anchored inside the center hover area");
        hover(viewport, QEvent::HoverMove, cross + QPointF(centerHalf + 0.5, 0));
        assert(renderedFrame(window) == hidden && "ROI measurements must hide outside the center handle");
        hover(viewport, QEvent::HoverMove, cross);
        assert(renderedFrame(window) == shown);
        hover(viewport, QEvent::HoverLeave, cross);
        assert(renderedFrame(window) == hidden && "ROI measurements must hide immediately on hover leave");
    }
    viewport.beginRoiEdit({4, 6, 0, 0}, {makeRectangle(300, 270, 300, 160)});
    viewport.restoreViewState({ImageViewport::Manual, 1, {450, 350}});
    hover(viewport, QEvent::HoverEnter, viewport.imageToItem({480, 380}));
    rendered = renderedFrame(window);
    assert(at(rendered, {310, 260}) == QColor(45, 45, 45) && "Selected ROIs must not draw an outer frame");
    hover(viewport, QEvent::HoverEnter, viewport.imageToItem({450, 350}));
    rendered = renderedFrame(window);
    assert(inputBluePixels(rendered, {420, 235, 60, 25}, dpr) > 30
           && "Width labels must sit outside their corresponding edge");
    assert(inputBluePixels(rendered, {615, 320, 25, 60}, dpr) > 30
           && "Height labels must sit outside their corresponding edge");
    assert(at(rendered, {598, 348}).red() < 240 && at(rendered, {452, 348}).red() < 240
           && "Edge handles must use the same transparent fill as the center handle");
    rendered.save(evidence + "/roi-style-measurements.png");
    hover(viewport, QEvent::HoverMove, viewport.imageToItem({480, 380}));
    renderedFrame(window).save(evidence + "/roi-style-selected.png");
    for (double angle : {-6.0, 89.0, 160.0})
    {
        viewport.beginRoiEdit({4, 6, 0, 0}, {makeRectangle(300, 270, 300, 160, angle)});
        hover(viewport, QEvent::HoverMove, viewport.imageToItem({450, 350}));
        renderedFrame(window).save(evidence + QStringLiteral("/roi-style-angle-%1.png").arg(angle));
    }
    viewport.beginRoiEdit({4, 6, 0, 0}, {makeCircle({450, 350}, 100)});
    hover(viewport, QEvent::HoverMove, viewport.imageToItem({450, 350}));
    rendered = renderedFrame(window);
    assert(at(rendered, {500, 350}).red() < 30 && at(rendered, {500, 350}).blue() > 220
           && "A selected circle must draw its radius line in the input ROI color");
    assert(inputBluePixels(rendered, {565, 337, 75, 26}, dpr) > 30
           && "Circle radius labels must sit outside the circumference handle");
    assert(inputBluePixels(rendered, {480, 331, 65, 15}, dpr) > 30
           && "Circle coordinates must sit to the right of the center");
    rendered.save(evidence + "/roi-style-circle-center.png");
    hover(viewport, QEvent::HoverMove, viewport.imageToItem({550, 350}));
    rendered = renderedFrame(window);
    assert(inputBluePixels(rendered, {565, 337, 75, 26}, dpr) == 0 && "Circle edge hover must hide the radius text");
    assert(inputBluePixels(rendered, {480, 331, 65, 15}, dpr) == 0 && "Circle edge hover must hide the coordinates");
    assert(at(rendered, {552, 348}).red() < 240 && "Circle radius handles must have a transparent background");
    rendered.save(evidence + "/roi-style-circle-edge.png");
    hover(viewport, QEvent::HoverMove, viewport.imageToItem({370, 410}));
    hover(viewport, QEvent::HoverMove, viewport.imageToItem({450, 350}));
    rendered = renderedFrame(window);
    assert(at(rendered, {410, 380}).red() < 30 && at(rendered, {410, 380}).blue() > 220
           && "The radius handle direction must persist when returning to the center");
    rendered.save(evidence + "/roi-style-circle-reference.png");
    hover(viewport, QEvent::HoverLeave, viewport.imageToItem({550, 350}));
    viewport.restoreViewState({
        ImageViewport::Manual,
        0.75,
        {450, 350}
    });
    viewport.beginRoiEdit({4, 6, 0, 0}, {makeRectangle(295.3, 189.25, 456.8, 238.9, 0.3)});
    renderedFrame(window).save(evidence + "/roi-rectangle.png");
    viewport.beginRoiEdit({4, 6, 0, 0}, {makeSegment({231.4, 253.6}, {431.4, 353.6})});
    renderedFrame(window).save(evidence + "/roi-line.png");
    viewport.beginRoiEdit({4, 6, 0, 1}, {makeCircle({383.3, 406.1}, 100), makeCircle({583.3, 606.1}, 100)});
    renderedFrame(window).save(evidence + "/roi-circles.png");
    viewport.endRoiEdit();
    auto replacement      = frame;
    replacement.requestId = 3;
    replacement.image.fill(82);
    viewport.setDisplayFrame(replacement);
    viewport.setDisplayFrame(frame);
    assert(viewport.displayedImage() == replacement.image);
    viewport.clearImage(4);
    assert(!viewport.hasImage());
    frame.requestId = 5;
    viewport.setDisplayFrame(frame);
    const int before = commits;
    viewport.restoreViewState({
        ImageViewport::Manual,
        100,
        {100000010.375, 100000005.625}
    });
    viewport.beginRoiEdit({4, 6, 0, 0}, {makeRectangle(100000000.125, 100000000.25, 20.5, 10.75)});
    drag(viewport, {100000010.375, 100000005.625}, {100000010.5, 100000005.875});
    assert(commits == before + 1 && near((*committed)[1], 100000000.25) && near((*committed)[2], 100000000.5));
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
            5, 18, 0, 0, 100
    },
        {EncodedRegion{2., 10, 10, 20, 20, 0}}, false, true);
    viewport.setParentItem(relocatedWindow.contentItem());
    assert(windowCancellations == 1 && !viewport.editing() && viewport.selectedRoi() == -1
           && viewport.drawShape() == 0);
    relocatedWindow.requestActivate();
    await([&] { return relocatedWindow.isActive(); });
    viewport.beginRoiEdit(
        {
            5, 18, 0, 0, 101
    },
        {EncodedRegion{2., 10, 10, 20, 20, 0}}, false, true);
    viewport.setParentItem(nullptr);
    assert(windowCancellations == 2 && !viewport.editing() && viewport.selectedRoi() == -1
           && viewport.drawShape() == 0);
    viewport.setParentItem(window.contentItem());
    window.requestActivate();
    await([&] { return window.isActive(); });
    viewport.beginRoiEdit(
        {
            5, 18, 0, 0, 102
    },
        {EncodedRegion{2., 10, 10, 20, 20, 0}}, false, true);
    relocatedWindow.requestActivate();
    await([&] { return relocatedWindow.isActive() && !window.isActive(); });
    assert(windowCancellations == 3 && !viewport.editing() && viewport.selectedRoi() == -1
           && viewport.drawShape() == 0);
    viewport.setParentItem(nullptr);
    assert(windowCancellations == 3);
    std::cout << "[Viewport owning window] Reparenting and detaching cancel once; reattaching observes the current "
                 "window without retaining editing state.\n";
    window.hide();
    ImageViewport stable;
    stable.setWidth(200);
    stable.setHeight(200);
    stable.setDisplayFrame(DisplayConverter::convert(cv::Mat(100, 100, CV_8UC1, cv::Scalar(0))));
    const RoiEditTarget stableTarget{99, 19, 0, -1, 7};
    stable.beginRoiEdit(stableTarget, {});
    stable.setDrawShape(2);
    int stableCommits = 0;
    QObject::connect(&stable, &ImageViewport::roiCommitted, &stable,
                     [&](const RoiEditTarget &, const EncodedRegion &) { ++stableCommits; });
    mouse(stable, QEvent::MouseButtonPress, stable.imageToItem({10, 10}), Qt::LeftButton, Qt::LeftButton);
    mouse(stable, QEvent::MouseMove, stable.imageToItem({30, 30}), Qt::NoButton, Qt::LeftButton);
    stable.beginRoiEdit(stableTarget, {});
    mouse(stable, QEvent::MouseButtonRelease, stable.imageToItem({30, 30}), Qt::LeftButton, Qt::NoButton);
    assert(stableCommits == 1);
    return 0;
}
