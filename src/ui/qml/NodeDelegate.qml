import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import quickui

Rectangle {
    id: root
    required property var node
    property bool selected: false
    signal pointerPressed(var mouse, var item)
    signal pointerMoved(var mouse, var item)
    signal pointerReleased(var mouse, var item)
    signal executeRequested()
    width: 156; height: 68; radius: 4
    color: selected ? "#e1f1fd" : QuiColor.Primary
    border.color: selected ? QuiColor.Highlight : QuiColor.Border
    border.width: selected ? 2 : 1
    readonly property color stateColor: node.state === "blue" ? "#2589df" : node.state === "green" ? "#42a374" : node.state === "red" ? "#d35e55" : "#a5adb8"
    Rectangle { anchors.left: parent.left; anchors.top: parent.top; anchors.bottom: parent.bottom; width: 4; radius: 2; color: root.stateColor }
    ColumnLayout {
        anchors.fill: parent; anchors.margins: 9; spacing: 5
        QuiText { text: root.node.title; Layout.fillWidth: true; elide: Text.ElideRight; font: QuiFont.Body }
        RowLayout {
            Layout.fillWidth: true
            QuiText { text: root.node.timing || "未运行"; font: QuiFont.Caption; color: QuiColor.FontCaption }
            Item { Layout.fillWidth: true }
            QuiText { text: "工具缺失"; visible: root.node.missing === true; color: "#c9483d"; font: QuiFont.Caption }
        }
    }
    MouseArea {
        id: pointer; anchors.fill: parent; hoverEnabled: true; preventStealing: true
        acceptedButtons: Qt.LeftButton | Qt.RightButton | Qt.MiddleButton
        onPressed: function(mouse) { root.pointerPressed(mouse, root) }
        onPositionChanged: function(mouse) { if (pressed) root.pointerMoved(mouse, root) }
        onReleased: function(mouse) { root.pointerReleased(mouse, root) }
        onDoubleClicked: function(mouse) { if (mouse.button === Qt.LeftButton && !app.busy) root.executeRequested() }
    }
    QuiToolTip { visible: pointer.containsMouse && !pointer.pressed; text: (root.node.title || "") + "\n" + (root.node.timingDetail || root.node.timing || "未运行"); delay: 600 }
}
