#pragma once

#include "common/Singleton.h"
#include "ui/Def.h"
#include "ui/Export.h"

#include <QObject>

namespace visionrt {
class VISIONRT_UI_API VisionRTSize final : public QObject
{
    Q_OBJECT
    VISIONRT_QML_SINGLETON(VisionRTSize)
    VISIONRT_Q_PROPERTY_AUTO(int, HeaderSize)
    VISIONRT_Q_PROPERTY_AUTO(int, BarSize)
    VISIONRT_Q_PROPERTY_AUTO(int, ButtonSize)
    VISIONRT_Q_PROPERTY_AUTO(int, IconButtonSize)
    VISIONRT_Q_PROPERTY_AUTO(int, IconSize)
    VISIONRT_Q_PROPERTY_AUTO(int, SmallIconButtonSize)
    VISIONRT_Q_PROPERTY_AUTO(int, SmallIconSize)
    VISIONRT_Q_PROPERTY_AUTO(int, SeparatorSize)
    VISIONRT_Q_PROPERTY_AUTO(int, ContentSize)
    VISIONRT_Q_PROPERTY_AUTO(int, SmallContentSize)
    VISIONRT_Q_PROPERTY_AUTO(int, NodeWidth)
    VISIONRT_Q_PROPERTY_AUTO(int, NodeHeight)
    VISIONRT_Q_PROPERTY_AUTO(int, NodeTooltipWidth)
    VISIONRT_Q_PROPERTY_AUTO(int, ThumbnailWidth)
    VISIONRT_Q_PROPERTY_AUTO(int, ThumbnailHeight)
    VISIONRT_Q_PROPERTY_AUTO(int, ThumbnailHorizontalMargin)
    VISIONRT_Q_PROPERTY_AUTO(int, ScrollBarWidth)
    VISIONRT_Q_PROPERTY_AUTO(int, ParamLabelWidth)
    VISIONRT_Q_PROPERTY_AUTO(int, Spacing)
    VISIONRT_Q_PROPERTY_AUTO(int, NavbarWidth)
    VISIONRT_Q_PROPERTY_AUTO(qreal, ROICenterHandleSize)
    VISIONRT_Q_PROPERTY_AUTO(qreal, ROIHandleSize)
    VISIONRT_Q_PROPERTY_AUTO(qreal, ROIInputStrokeWidth)
    VISIONRT_Q_PROPERTY_AUTO(qreal, ROIOutputStrokeWidth)
    VISIONRT_Q_PROPERTY_AUTO(qreal, ROIGuideStrokeWidth)
    VISIONRT_Q_PROPERTY_AUTO(qreal, ROIGuideOffset)
    VISIONRT_Q_PROPERTY_AUTO(qreal, ROIRotationArrowSize)
    VISIONRT_Q_PROPERTY_AUTO(qreal, ROIArrowHeadSize)
    VISIONRT_Q_PROPERTY_AUTO(qreal, ROIHandleHitRadius)
    VISIONRT_Q_PROPERTY_AUTO(qreal, ROIEdgeHitTolerance)
    VISIONRT_Q_PROPERTY_AUTO(qreal, ROILabelOffset)
    VISIONRT_Q_PROPERTY_AUTO(qreal, ROILabelHorizontalPadding)
    VISIONRT_Q_PROPERTY_AUTO(qreal, ROILabelVerticalPadding)
private:
    explicit VisionRTSize(QObject *parent = nullptr);
};
} // namespace visionrt
