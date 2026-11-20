#pragma once
#include <QObject>
#include "common/Singleton.h"

namespace visionrt {
class VisionRTSize final : public QObject
{
    Q_OBJECT
    VISIONRT_QML_SINGLETON(VisionRTSize)
public:
    enum Metric {
        HeaderSize = 40,
        BarSize = 36,
        ButtonSize = 32,
        IconButtonSize = 28,
        IconSize = 20,
        SeparatorSize = 32,
        ContentSize = 32,
        NodeWidth = 160,
        NodeHeight = 60,
        NodeTooltipWidth = 240,
        ThumbnailWidth = 140,
        ThumbnailHeight = 60,
        ParamLabelWidth = 120,
        NavbarWidth = 160
    };
    Q_ENUM(Metric)
private:
    explicit VisionRTSize(QObject *parent = nullptr) : QObject(parent) {}
};
}
