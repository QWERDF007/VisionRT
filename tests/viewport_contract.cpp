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
#include <QThread>
#include <cassert>
#include <cmath>
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
    const auto source      = OwnedImage::copyOf(pixels);
    const auto display     = DisplayConverter::convert(source);
    assert(display.image.pixelColor(0, 0) == QColor(0, 0, 0));
    assert(display.image.pixelColor(1, 0) == QColor(128, 128, 128));
    assert(display.image.pixelColor(2, 0) == QColor(255, 255, 255));
    assert(display.image.pixelColor(3, 0) == QColor(0, 0, 0));
    assert(display.image.pixelColor(4, 0) == QColor(0, 0, 0));
    assert(!display.warning.isEmpty());
    assert(source.readOnlyMat().at<float>(0, 0) == -10);
    assert(source.readOnlyMat().at<float>(0, 2) == 10);
    assert(std::isnan(source.readOnlyMat().at<float>(0, 3)));
    const auto constant = DisplayConverter::convert(OwnedImage::copyOf(cv::Mat(2, 2, CV_16UC1, cv::Scalar(600))));
    assert(constant.image.pixelColor(1, 1) == QColor(128, 128, 128));
    const auto color = OwnedImage::copyOf(cv::Mat(2, 2, CV_8UC3, cv::Scalar(1, 2, 3)));
    assert(DisplayConverter::convert(color).image.pixelColor(0, 0) == QColor(3, 2, 1));
    assert(DisplayConverter::convert(color, "RGB").image.pixelColor(0, 0) == QColor(1, 2, 3));
    const auto unsupported = DisplayConverter::convert(OwnedImage::copyOf(cv::Mat(2, 2, CV_8UC2, cv::Scalar(1, 2))));
    assert(unsupported.image.isNull() && !unsupported.error.isEmpty());
    DisplayConverter          converter;
    std::vector<DisplayFrame> accepted;
    QObject::connect(&converter, &DisplayConverter::batchConverted,
                     [&](const std::vector<DisplayFrame> &frames) { accepted = frames; });
    converter.request(source, {}, 1, 1);
    converter.request(color, {}, 2, 1);
    converter.requestBatch({color, source}, {}, 3, 1);
    await([&] { return !accepted.empty(); });
    assert(accepted.size() == 2 && accepted[0].requestId == 3 && accepted[1].batchIndex == 1);
    assert(accepted[0].image.pixelColor(0, 0) == QColor(3, 2, 1));
    accepted.clear();
    converter.request(source, {}, 4, 1);
    converter.invalidate(2);
    converter.request(color, {}, 1, 2);
    await([&] { return !accepted.empty(); });
    assert(accepted[0].generation == 2 && accepted[0].requestId == 1);

    QQuickWindow window;
    window.resize(400, 300);
    ImageViewport viewport(window.contentItem());
    viewport.setSize(QSizeF(400, 300));
    auto frame      = DisplayConverter::convert(OwnedImage::copyOf(cv::Mat(100, 200, CV_8UC1, cv::Scalar(45))));
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
    std::optional<Roi> committed;
    RoiEditTarget      committedTarget;
    QObject::connect(&viewport, &ImageViewport::roiCommitted,
                     [&](const RoiEditTarget &target, const Roi &roi)
                     {
                         ++commits;
                         committed       = roi;
                         committedTarget = target;
                     });
    viewport.setDrawShape(2);
    drag(viewport, {30, 40}, {10, 20});
    assert(commits == 1 && committed->kind() == RoiKind::Rectangle);
    assert(committedTarget.nodeSeq == 9007199254740993ULL && committedTarget.inputPortId == "first");
    assert(committedTarget.roiIndex == -1);
    assert(near(committed->encoded()[1], 10) && near(committed->encoded()[2], 20));
    assert(near(committed->encoded()[3], 20) && near(committed->encoded()[4], 20));
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
    assert(committed->encoded().size() == 7);
    viewport.setReadOnly(true);
    viewport.setDrawShape(3);
    drag(viewport, {10, 10}, {20, 20});
    assert(commits == 2);

    viewport.beginRoiEdit({4, "line", 2, -1}, {});
    viewport.setDrawShape(1);
    mouse(viewport, QEvent::MouseButtonPress, viewport.imageToItem({10, 10}), Qt::LeftButton, Qt::LeftButton);
    viewport.setWidth(700); // A live layout change preserves the draft's image-space anchor.
    mouse(viewport, QEvent::MouseMove, viewport.imageToItem({30, 40}), Qt::NoButton, Qt::LeftButton);
    mouse(viewport, QEvent::MouseButtonRelease, viewport.imageToItem({30, 40}), Qt::LeftButton, Qt::NoButton);
    assert(commits == 3 && committed->kind() == RoiKind::Segment);
    assert(committedTarget.imageIndex == 2 && committed->encoded()[1] == 10);
    assert(near(committed->encoded()[3], 30) && near(committed->encoded()[4], 40));
    viewport.beginRoiEdit({4, "circle", 0, -1}, {});
    viewport.setDrawShape(3);
    drag(viewport, {100, 50}, {110, 50});
    assert(commits == 4 && near(committed->encoded()[3], 10));
    viewport.beginRoiEdit({4, "points", 0, -1}, {});
    viewport.setDrawShape(5);
    mouse(viewport, QEvent::MouseButtonPress, viewport.imageToItem({20, 30}), Qt::LeftButton, Qt::LeftButton);
    mouse(viewport, QEvent::MouseButtonRelease, viewport.imageToItem({20, 30}), Qt::LeftButton, Qt::NoButton);
    QKeyEvent enter(QEvent::KeyPress, Qt::Key_Return, Qt::NoModifier);
    QCoreApplication::sendEvent(&viewport, &enter);
    assert(commits == 5 && committed->kind() == RoiKind::PointSet && committed->encoded().size() == 3);

    viewport.beginRoiEdit({4, "rotated", 0, 0}, {Roi::rectangle(40, 30, 40, 20, 0)});
    viewport.setDrawShape(0);
    const double rotationArm = 10 + 24 / viewport.zoom();
    drag(viewport, {60, 40 - rotationArm}, {60 + rotationArm, 40});
    assert(commits == 6 && committedTarget.roiIndex == 0 && near(committed->encoded()[5], 90));
    drag(viewport, {60, 40}, {65, 47});
    assert(commits == 7 && near(committed->encoded()[1], 45) && near(committed->encoded()[2], 37));
    assert(near(committed->encoded()[5], 90));
    viewport.beginRoiEdit({4, "rotated", 0, 0}, {Roi::rectangle(40, 30, 40, 20, 90)});
    drag(viewport, {70, 20}, {80, 10});
    assert(commits == 8 && near(committed->encoded()[1], 40) && near(committed->encoded()[2], 20));
    assert(near(committed->encoded()[3], 50) && near(committed->encoded()[4], 30));
    viewport.beginRoiEdit({4, "rotated", 0, 0}, {Roi::rectangle(40, 30, 40, 20, 0)});
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
    viewport.setOverlays({Roi::rectangle(20, 20, 40, 40, 0)}, {});
    window.show();
    await([&] { return window.isExposed(); });
    const auto rendered = window.grabWindow();
    assert(!rendered.isNull());
    const double dpr      = window.effectiveDevicePixelRatio();
    const auto   interior = rendered.pixelColor(qRound(200 * dpr), qRound(150 * dpr));
    assert(std::abs(interior.red() - 45) <= 1 && std::abs(interior.green() - 45) <= 1);
    const auto yellow = rendered.pixelColor(qRound(40 * dpr), qRound(130 * dpr));
    assert(yellow.red() > 240 && yellow.green() > 200 && yellow.blue() < 10);
    viewport.setOverlays({Roi::rectangle(20, 20, 40, 40, 0)}, {Roi::rectangle(20, 20, 40, 40, 0)});
    const auto overlaid = window.grabWindow();
    const auto blue     = overlaid.pixelColor(qRound(40 * dpr), qRound(130 * dpr));
    assert(blue.red() < 10 && blue.green() > 100 && blue.blue() > 240);
    auto replacement      = DisplayConverter::convert(OwnedImage::copyOf(cv::Mat(100, 200, CV_8UC1, cv::Scalar(82))));
    replacement.requestId = 4;
    viewport.setDisplayFrame(replacement);
    const auto refreshed       = window.grabWindow();
    const auto refreshedCenter = refreshed.pixelColor(qRound(200 * dpr), qRound(150 * dpr));
    assert(std::abs(refreshedCenter.red() - 82) <= 1);
    window.hide();
    return 0;
}
