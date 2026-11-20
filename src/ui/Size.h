#pragma once
#include <QObject>

namespace visionrt {
class VisionRTSize
{
    Q_GADGET
public:
    enum Metric {
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
        ParamLabelWidth = 120
    };
    Q_ENUM(Metric)
};
}
