#include "ui/Size.h"

namespace visionrt {

VisionRTSize *VisionRTSize::getInstance()
{
    static VisionRTSize instance;
    return &instance;
}

VisionRTSize *VisionRTSize::create(QQmlEngine *, QJSEngine *)
{
    auto *instance = getInstance();
    QJSEngine::setObjectOwnership(instance, QJSEngine::CppOwnership);
    return instance;
}

VisionRTSize::VisionRTSize(QObject *parent)
    : QObject(parent)
{
    HeaderSize(40);
    BarSize(36);
    ButtonSize(32);
    IconButtonSize(28);
    IconSize(20);
    SmallIconButtonSize(16);
    SmallIconSize(12);
    SeparatorSize(32);
    ContentSize(32);
    SmallContentSize(20);
    NodeWidth(160);
    NodeHeight(60);
    NodeTooltipWidth(240);
    ThumbnailWidth(140);
    ThumbnailHeight(60);
    ThumbnailHorizontalMargin(10);
    ScrollBarWidth(8);
    ParamLabelWidth(120);
    Spacing(5);
    NavbarWidth(160);
    ROICenterHandleSize(18);
    ROIHandleSize(10);
    ROIHandleStrokeWidth(2);
    ROIInputStrokeWidth(2.5);
    ROIOutputStrokeWidth(2.5);
    ROIGuideStrokeWidth(1);
    ROIRotationArrowSize(20);
    ROIArrowStrokeWidth(2);
    ROIArrowHeadSize(12);
    CoordinateAxisStrokeWidth(2.5);
    CoordinateArrowHeadSize(14);
    CoordinateOriginSize(22);
    ROIHandleHitRadius(8);
    ROIEdgeHitTolerance(6);
    ROILabelOffset(12);
    ROILabelHorizontalPadding(4);
    ROILabelVerticalPadding(2);
}

} // namespace visionrt
