#pragma once

#include "ui/Export.h"
#include <QObject>
#include "common/Singleton.h"
#include "ui/Def.h"

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
    VISIONRT_Q_PROPERTY_AUTO(int, SeparatorSize)
    VISIONRT_Q_PROPERTY_AUTO(int, ContentSize)
    VISIONRT_Q_PROPERTY_AUTO(int, NodeWidth)
    VISIONRT_Q_PROPERTY_AUTO(int, NodeHeight)
    VISIONRT_Q_PROPERTY_AUTO(int, NodeTooltipWidth)
    VISIONRT_Q_PROPERTY_AUTO(int, ThumbnailWidth)
    VISIONRT_Q_PROPERTY_AUTO(int, ThumbnailHeight)
    VISIONRT_Q_PROPERTY_AUTO(int, ParamLabelWidth)
    VISIONRT_Q_PROPERTY_AUTO(int, NavbarWidth)
private:
    explicit VisionRTSize(QObject *parent = nullptr);
};
}
