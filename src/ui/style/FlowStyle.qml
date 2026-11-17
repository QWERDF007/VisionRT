pragma Singleton
import QtQuick
import quickui
import VisionRT.Style 1.0
import VisionRT.Style 1.0

QtObject {
    readonly property int nodeWidth: 160
    readonly property int nodeHeight: 60
    readonly property int tooltipWidth: 232
    readonly property color nodeStateGreen: "#1a7f37"
    readonly property color nodeHeaderRed: "#ffebe9"
    readonly property color nodeHeaderGray: "#dfe4ea"
    readonly property color nodeStateGray: WorkbenchStyle.iconMuted
}
