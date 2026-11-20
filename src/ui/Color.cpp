#include "ui/Color.h"

namespace visionrt {

VisionRTColor *VisionRTColor::getInstance()
{
    static VisionRTColor instance;
    return &instance;
}

VisionRTColor *VisionRTColor::create(QQmlEngine *, QJSEngine *)
{
    auto *instance = getInstance();
    QJSEngine::setObjectOwnership(instance, QJSEngine::CppOwnership);
    return instance;
}


VisionRTColor::VisionRTColor(QObject *parent)
    : QObject(parent)
{
    NodeGrey(QColor(0xcf, 0xcf, 0xcf));
    NodeRed(QColor(0xed, 0x1c, 0x24));
    NodeGreen(QColor(0x1a, 0x7f, 0x37));
    NodeLine(QColor(0xcf, 0xcf, 0xcf));
    NodeInputLine(QColor(0xff, 0x88, 0x00));
    NodeOutputLine(QColor(0x00, 0x66, 0xb4));
    InputROI(QColor(0x00, 0xbe, 0xbe));
    OutputROI(QColor(0x88, 0x00, 0xbe));
}

} // namespace visionrt
