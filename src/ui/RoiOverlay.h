#pragma once
#include <visionrt/contracts/ROI.h>

#include <QQuickPaintedItem>
#include <QVariantAnimation>
#include <optional>

namespace visionrt {
class ImageViewport;

class RoiOverlay final : public QQuickPaintedItem
{
public:
    explicit RoiOverlay(ImageViewport &viewport);
    void paint(QPainter *painter) override;
    void fade(const EncodedRegion &region);
    void clear();

private:
    ImageViewport               &viewport_;
    QVariantAnimation            transition_;
    std::optional<EncodedRegion> previous_;
};
} // namespace visionrt
