#pragma once

#include "ui/Export.h"
#include <QColor>
#include <QObject>
#include "common/Singleton.h"
#include "ui/Def.h"

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
    VISIONRT_Q_PROPERTY_AUTO(QColor, InputROI)
    VISIONRT_Q_PROPERTY_AUTO(QColor, OutputROI)
private:
    explicit VisionRTColor(QObject *parent = nullptr);
};
}
