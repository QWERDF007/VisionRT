#ifdef NDEBUG
#    undef NDEBUG
#endif
#include "ui/ImageViewport.h"

#include <QGuiApplication>
#include <QKeyEvent>
#include <QMouseEvent>
#include <cassert>
#include <cmath>
using namespace visionrt;

void mouse(ImageViewport &view, QEvent::Type type, QPointF point)
{
    QMouseEvent event(type, point, point, type == QEvent::MouseMove ? Qt::NoButton : Qt::LeftButton,
                      type == QEvent::MouseButtonRelease ? Qt::NoButton : Qt::LeftButton, Qt::NoModifier);
    QCoreApplication::sendEvent(&view, &event);
}

int main(int argc, char **argv)
{
    QGuiApplication app(argc, argv);
    ImageViewport   view;
    view.setSize({800, 600});
    auto frame      = DisplayConverter::convert(cv::Mat(400, 400, CV_8UC1, cv::Scalar(137)));
    frame.requestId = 1;
    view.setDisplayFrame(frame);
    EncodedRegion result;
    int           commits = 0, previews = 0;
    QObject::connect(&view, &ImageViewport::roiCommitted,
                     [&](auto, const auto &value)
                     {
                         ++commits;
                         result = value;
                     });
    QObject::connect(&view, &ImageViewport::roiDraftChanged, [&](auto, const auto &) { ++previews; });
    for (double zoom : {0.1, 1.0, 8.0})
    {
        view.restoreViewState({
            ImageViewport::Manual,
            zoom,
            {200, 200}
        });
        for (int moves : {1, 1000})
        {
            view.beginRoiEdit({7, 6, 0, 0, 9}, {makeCircle({200, 200}, 50)});
            const int before = commits;
            mouse(view, QEvent::MouseButtonPress, view.imageToItem({250, 200}));
            for (int i = 1; i <= moves; ++i)
                mouse(view, QEvent::MouseMove, view.imageToItem({250.0 + 20.0 * i / moves, 200}));
            assert(commits == before && previews > 0);
            mouse(view, QEvent::MouseButtonRelease, view.imageToItem({270, 200}));
            assert(commits == before + 1 && std::abs(result[3] - 70) < 1e-9);
        }
    }
    view.restoreViewState({
        ImageViewport::Manual,
        1,
        {200, 200}
    });
    view.beginRoiEdit({7, 6, 0, 0, 10}, {makeRectangle(100, 100, 100, 40)});
    mouse(view, QEvent::MouseButtonPress, view.imageToItem({100, 120}));
    mouse(view, QEvent::MouseButtonRelease, view.imageToItem({80, 120}));
    assert(result == makeRectangle(80, 100, 120, 40));
    const int before = commits;
    mouse(view, QEvent::MouseButtonPress, view.imageToItem({140, 120}));
    mouse(view, QEvent::MouseMove, view.imageToItem({145, 125}));
    QKeyEvent escape(QEvent::KeyPress, Qt::Key_Escape, Qt::NoModifier);
    QCoreApplication::sendEvent(&view, &escape);
    mouse(view, QEvent::MouseButtonRelease, view.imageToItem({145, 125}));
    assert(commits == before);
    QKeyEvent right(QEvent::KeyPress, Qt::Key_Right, Qt::ShiftModifier);
    QCoreApplication::sendEvent(&view, &right);
    assert(commits == before + 1 && result == makeRectangle(90, 100, 120, 40));
    view.beginRoiEdit({7, 6, 0, 0, 11}, {makeSegment({100, 100}, {200, 100})});
    mouse(view, QEvent::MouseButtonPress, view.imageToItem({100, 100}));
    mouse(view, QEvent::MouseButtonRelease, view.imageToItem({220, 110}));
    assert(result == makeSegment({220, 110}, {200, 100}));
    view.beginRoiEdit({7, 6, 0, 0, 12}, {makeRectangle(100, 100, 100, 40)});
    mouse(view, QEvent::MouseButtonPress, view.imageToItem({210, 90}));
    mouse(view, QEvent::MouseButtonRelease, view.imageToItem({180, 180}));
    assert(std::abs(result[5] - 90) < 1e-9 && result[3] == 100 && result[4] == 40);
    mouse(view, QEvent::MouseButtonPress, view.imageToItem({170, 120}));
    mouse(view, QEvent::MouseButtonRelease, view.imageToItem({190, 120}));
    assert(std::abs(result[1] - 110) < 1e-9 && std::abs(result[2] - 90) < 1e-9 && result[3] == 100 && result[4] == 60);
}
