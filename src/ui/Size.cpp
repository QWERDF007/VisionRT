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
    SeparatorSize(32);
    ContentSize(32);
    NodeWidth(160);
    NodeHeight(60);
    NodeTooltipWidth(240);
    ThumbnailWidth(140);
    ThumbnailHeight(60);
    ParamLabelWidth(120);
    NavbarWidth(160);
}

} // namespace visionrt
