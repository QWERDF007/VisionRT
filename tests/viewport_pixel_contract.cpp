#ifdef NDEBUG
#    undef NDEBUG
#endif
#include "ui/ImageViewport.h"

#include <QGuiApplication>
#include <QHoverEvent>
#include <QKeyEvent>
#include <QQuickWindow>
#include <cassert>

using namespace visionrt;

int main(int argc, char **argv)
{
    QGuiApplication app(argc, argv);
    QQuickWindow   window;
    ImageViewport   view;
    view.setParentItem(window.contentItem());
    view.setSize({400, 300});
    auto frame      = DisplayConverter::convert(cv::Mat(2, 3, CV_8UC3, cv::Scalar(17, 64, 231)));
    frame.requestId = 1;
    view.setDisplayFrame(frame);
    for (double zoom : {0.1, 1.0, 8.0})
    {
        view.restoreViewState({
            ImageViewport::Manual,
            zoom,
            {1.5, 1}
        });
        for (QPointF pixel : {QPointF(0.1, 0.1), QPointF(2.9, 1.9)})
        {
            const auto text = view.pixelTextAt(view.imageToItem(pixel));
            assert(text.endsWith("\nR: 231\nG: 64\nB: 17"));
        }
        assert(view.pixelTextAt(view.imageToItem({-0.01, 0})).isEmpty());
        assert(view.pixelTextAt(view.imageToItem({3, 0})).isEmpty());
    }
    view.setOverlays({makeRectangle(0, 0, 3, 2)}, {});
    assert(view.pixelTextAt(view.imageToItem({0, 0})).contains("R: 231"));
    frame           = DisplayConverter::convert(cv::Mat(2, 3, CV_8UC3, cv::Scalar(231, 64, 17)), ColorOrder::RGB);
    frame.requestId = 2;
    view.setDisplayFrame(frame);
    assert(view.pixelTextAt(view.imageToItem({0, 0})) == "0,0\nR: 231\nG: 64\nB: 17");
    frame           = DisplayConverter::convert(cv::Mat(2, 3, CV_8UC1, cv::Scalar(137)));
    frame.requestId = 3;
    view.setDisplayFrame(frame);
    assert(view.pixelTextAt(view.imageToItem({2.9, 1.9})) == "2,1\nGray: 137");
    const auto  pointer = view.imageToItem({1.1, 0.1});
    QHoverEvent enter(QEvent::HoverEnter, pointer, pointer, pointer, Qt::NoModifier);
    QCoreApplication::sendEvent(&view, &enter);
    assert(view.pixelText().isEmpty());
    QKeyEvent altPress(QEvent::KeyPress, Qt::Key_Alt, Qt::AltModifier);
    QCoreApplication::sendEvent(&window, &altPress);
    assert(!view.hasActiveFocus() && view.pixelText() == "1,0\nGray: 137");
    frame           = DisplayConverter::convert(cv::Mat(2, 3, CV_16UC1, cv::Scalar(600)));
    frame.requestId = 4;
    view.setDisplayFrame(frame);
    assert(view.pixelText() == "1,0");
    assert(view.pixelTextAt(view.imageToItem({0, 0})) == "0,0");
    QKeyEvent altRelease(QEvent::KeyRelease, Qt::Key_Alt, Qt::NoModifier);
    QCoreApplication::sendEvent(&window, &altRelease);
    assert(view.pixelText().isEmpty());
    QHoverEvent heldEnter(QEvent::HoverEnter, pointer, pointer, pointer, Qt::AltModifier);
    QCoreApplication::sendEvent(&view, &heldEnter);
    assert(view.pixelText() == "1,0");
    QEvent deactivated(QEvent::WindowDeactivate);
    QCoreApplication::sendEvent(&window, &deactivated);
    assert(view.pixelText().isEmpty());
    view.restoreViewState({
        ImageViewport::Manual,
        1000,
        {1.5, 1}
    });
    assert(view.pixelTextAt({-1, 150}).isEmpty());
    assert(view.pixelTextAt({400, 150}).isEmpty());
    view.clearImage(5);
    assert(view.pixelText().isEmpty());
    assert(view.pixelTextAt({200, 150}).isEmpty());
}
