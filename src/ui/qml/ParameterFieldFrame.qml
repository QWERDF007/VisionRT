import QtQuick
import VisionRT.Ui 1.0
import quickui

QuiTextBoxBackground {
    id: control
    border.color: invalid ? QuiColor.Error : (inputItem && inputItem.activeFocus) || hovered ? QuiColor.Highlight : QuiColor.Border
    Rectangle {
        anchors.fill: parent
        z: 10
        color: QuiColor.Transparent
        radius: control.radius
        border.width: control.border.width
        border.color: control.border.color
    }
}
