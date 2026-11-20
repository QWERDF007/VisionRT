import QtQuick
import VisionRT.Ui 1.0
import quickui

Rectangle {
    property int orientation: Qt.Vertical
    implicitWidth: orientation === Qt.Vertical ? 2 : VisionRTSize.SeparatorSize
    implicitHeight: orientation === Qt.Vertical ? VisionRTSize.SeparatorSize : 2
    color: QuiColor.Border
}
