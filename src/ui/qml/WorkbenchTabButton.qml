import QtQuick
import quickui
import VisionRT.Ui 1.0

QuiTabButton {
    id: control
    property int orientation: Qt.Horizontal

    font: QuiFont.Body
    verticalText: orientation === Qt.Vertical
    showIndicator: orientation === Qt.Horizontal
    radius: 0
    normalColor: QuiColor.Primary
    checkedColor: QuiColor.Button
    normalTextColor: QuiColor.FontPrimary
    checkedTextColor: QuiColor.Highlight
    hoverTextColor: QuiColor.Highlight

    Rectangle {
        parent: control
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.bottom: parent.bottom
        height: control.indicatorHeight
        visible: control.orientation === Qt.Vertical || !control.checked
        color: QuiColor.Border
        z: 1
    }
    Rectangle {
        parent: control
        anchors.top: parent.top
        anchors.bottom: parent.bottom
        anchors.right: parent.right
        width: control.indicatorHeight
        color: control.orientation === Qt.Vertical && control.checked ? control.indicatorColor : QuiColor.Border
        z: 2
    }
}
