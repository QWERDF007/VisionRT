#ifdef NDEBUG
#    undef NDEBUG
#endif
#include "ui/ImageViewport.h"

#include <QGuiApplication>
#include <QHoverEvent>
#include <cassert>

using namespace visionrt;

int main(int argc, char **argv)
{
    QGuiApplication app(argc, argv);
    ImageViewport   view;
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
            assert(text.contains("R: 231  G: 64  B: 17"));
        }
        assert(view.pixelTextAt(view.imageToItem({-0.01, 0})).isEmpty());
        assert(view.pixelTextAt(view.imageToItem({3, 0})).isEmpty());
    }
    view.setOverlays({makeRectangle(0, 0, 3, 2)}, {});
    assert(view.pixelTextAt(view.imageToItem({0, 0})).contains("R: 231"));
    frame           = DisplayConverter::convert(cv::Mat(2, 3, CV_8UC3, cv::Scalar(231, 64, 17)), ColorOrder::RGB);
    frame.requestId = 2;
    view.setDisplayFrame(frame);
    assert(view.pixelTextAt(view.imageToItem({0, 0})).endsWith("R: 231  G: 64  B: 17"));
    frame           = DisplayConverter::convert(cv::Mat(2, 3, CV_8UC1, cv::Scalar(137)));
    frame.requestId = 3;
    view.setDisplayFrame(frame);
    assert(view.pixelTextAt(view.imageToItem({2.9, 1.9})) == "X: 2  Y: 1\nGray: 137");
    const auto  pointer = view.imageToItem({1.1, 0.1});
    QHoverEvent enter(QEvent::HoverEnter, pointer, pointer, pointer, Qt::NoModifier);
    QCoreApplication::sendEvent(&view, &enter);
    assert(view.pixelText() == "X: 1  Y: 0\nGray: 137");
    frame           = DisplayConverter::convert(cv::Mat(2, 3, CV_16UC1, cv::Scalar(600)));
    frame.requestId = 4;
    view.setDisplayFrame(frame);
    assert(view.pixelText() == "X: 1  Y: 0");
    assert(view.pixelTextAt(view.imageToItem({0, 0})) == "X: 0  Y: 0");
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
