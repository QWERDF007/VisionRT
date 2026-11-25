#pragma once

#include "common/Singleton.h"
#include "ui/Def.h"
#include "ui/Export.h"

#include <QColor>
#include <QObject>

namespace visionrt {
class VISIONRT_UI_API VisionRTColor final : public QObject
{
    Q_OBJECT
    VISIONRT_QML_SINGLETON(VisionRTColor)
    VISIONRT_Q_PROPERTY_AUTO(QColor, NodeGrey)
    VISIONRT_Q_PROPERTY_AUTO(QColor, NodeRed)
    VISIONRT_Q_PROPERTY_AUTO(QColor, NodeGreen)
    VISIONRT_Q_PROPERTY_AUTO(QColor, NodeLine)
    VISIONRT_Q_PROPERTY_AUTO(QColor, NodeInputLine)
    VISIONRT_Q_PROPERTY_AUTO(QColor, NodeOutputLine)
    VISIONRT_Q_PROPERTY_AUTO(QColor, ROIInput)
    VISIONRT_Q_PROPERTY_AUTO(QColor, ROIOutput)
    VISIONRT_Q_PROPERTY_AUTO(QColor, ROIGuide)
    VISIONRT_Q_PROPERTY_AUTO(QColor, ROICrosshair)
    VISIONRT_Q_PROPERTY_AUTO(QColor, ROILabelText)
    VISIONRT_Q_PROPERTY_AUTO(QColor, ROIPreview)
    VISIONRT_Q_PROPERTY_AUTO(QColor, CoordinateAxisX)
    VISIONRT_Q_PROPERTY_AUTO(QColor, CoordinateAxisY)
    VISIONRT_Q_PROPERTY_AUTO(QColor, CoordinateOrigin)
    VISIONRT_Q_PROPERTY_AUTO(qreal, ROISelectionFillOpacity)
private:
    explicit VisionRTColor(QObject *parent = nullptr);
};
} // namespace visionrt
