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
    ROIInput(QColor("#006EFF"));
    ROIOutput(QColor("#AA00FF"));
    ROIGuide(QColor("#6FB7F9"));
    ROICrosshair(QColor("#222222"));
    ROILabelText(QColor("#FFFFFF"));
    ROIPreview(QColor("#F1C40F"));
    CoordinateAxisX(QColor("#FF0000"));
    CoordinateAxisY(QColor("#45B045"));
    CoordinateOrigin(QColor("#AA00FF"));
    ROISelectionFillOpacity(0.18);
    NodeGrey(QColor(0xcf, 0xcf, 0xcf));
    NodeRed(QColor(0xed, 0x1c, 0x24));
    NodeGreen(QColor(0x1a, 0x7f, 0x37));
    NodeWarning(QColor(0xd9, 0x77, 0x06));
    NodeLine(QColor(0xcf, 0xcf, 0xcf));
    NodeInputLine(QColor(0xff, 0x88, 0x00));
    NodeOutputLine(QColor(0x00, 0x66, 0xb4));
}

} // namespace visionrt
