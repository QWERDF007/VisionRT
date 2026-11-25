#include "ui/Color.h"
#include "ui/DisplayConverter.h"
#include "ui/ImageViewport.h"

#include <QDir>
#include <QGuiApplication>
#include <QQuickWindow>
#include <QSGRendererInterface>
#include <QTest>
#include <cassert>

using namespace visionrt;

bool coloredNear(const QImage &image, QPointF position, QColor color, double dpr)
{
    const auto pixel = (position * dpr).toPoint();
    for (int y = pixel.y() - 3; y <= pixel.y() + 3; ++y)
        for (int x = pixel.x() - 3; x <= pixel.x() + 3; ++x)
            if (image.rect().contains(x, y))
            {
                const auto actual = image.pixelColor(x, y);
                const auto distance = std::abs(actual.hsvHueF() - color.hsvHueF());
                if ((distance < 0.04 || distance > 0.96) && actual.hsvSaturationF() > 0.5
                    && actual.valueF() > 0.25) return true;
            }
    return false;
}

int main(int argc, char **argv)
{
    QQuickWindow::setGraphicsApi(QSGRendererInterface::Software);
    QGuiApplication app(argc, argv);
    QQuickWindow window;
    window.resize(600, 500);
    ImageViewport viewport(window.contentItem());
    viewport.setSize({600, 500});
    viewport.setHostFrame(DisplayConverter::convert(cv::Mat(300, 400, CV_8UC1, cv::Scalar(20)), ColorOrder::Gray));
    viewport.fitContain();
    viewport.setGeometryOverlays({
        {Direction::fromNested({{{50, 230, 120, 0}}}), 0, false},
        {CoordinateFrame2D::fromNested({{{150, 50, 120, 0, 0, 120}}}), 0, false}
    });
    window.show();
    assert(QTest::qWaitForWindowExposed(&window));
    const auto &colors = *VisionRTColor::getInstance();
    const auto verify = [&]
    {
        QImage image;
        const bool ready = QTest::qWaitFor([&] {
            window.update();
            image = window.grabWindow();
            return coloredNear(image, viewport.imageToItem({210, 50}), colors.CoordinateAxisX(), window.devicePixelRatio());
        }, 5000);
        const auto evidence = QCoreApplication::applicationDirPath() + "/../evidence";
        QDir().mkpath(evidence);
        image.save(evidence + "/geometry-overlays-current.png");
        assert(ready);
        assert(coloredNear(image, viewport.imageToItem({150, 110}), colors.CoordinateAxisY(), window.devicePixelRatio()));
        assert(coloredNear(image, viewport.imageToItem({150, 50}), colors.CoordinateOrigin(), window.devicePixelRatio()));
        assert(coloredNear(image, viewport.imageToItem({110, 230}), colors.ROIOutput(), window.devicePixelRatio()));
        return image;
    };
    verify();
    viewport.zoomBy(1.25);
    auto view = viewport.viewState();
    view.imageCenter += QPointF(12, -7);
    viewport.restoreViewState(view);
    const auto image = verify();
    const auto evidence = QCoreApplication::applicationDirPath() + "/../evidence";
    QDir().mkpath(evidence);
    assert(image.save(evidence + "/geometry-overlays.png"));
}
