import QtQuick
import quickui
import VisionRT.Ui 1.0

QuiScrollBar {
    implicitWidth: VisionRTSize.ScrollBarWidth
    implicitHeight: VisionRTSize.ScrollBarWidth
    leftPadding: vertical ? 0 : horizontalPadding
    rightPadding: vertical ? 0 : horizontalPadding
    topPadding: horizontal ? 0 : verticalPadding
    bottomPadding: horizontal ? 0 : verticalPadding
    anchors.right: vertical ? parent.right : undefined
    anchors.bottom: horizontal ? parent.bottom : undefined
}
