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
    antialiasing: true
    color: selected ? "#e1f1fd" : QuiColor.Primary
    border.color: selected ? QuiColor.Highlight : QuiColor.Border
    border.width: selected ? 2 : 1
    readonly property color stateColor: node.state === "blue" ? "#2589df" : node.state === "green" ? "#42a374" : node.state === "red" ? "#d35e55" : "#a5adb8"
    Rectangle { anchors.left: parent.left; anchors.top: parent.top; anchors.bottom: parent.bottom; width: 4; radius: 2; color: root.stateColor; antialiasing: true }
    ColumnLayout {
        anchors.fill: parent; anchors.margins: 9; spacing: 5
        QuiText {
            text: root.node.title; Layout.fillWidth: true; elide: Text.ElideRight; font: QuiFont.Body
            renderType: Text.CurveRendering; renderTypeQuality: Text.VeryHighRenderTypeQuality
        }
        RowLayout {
            Layout.fillWidth: true
            QuiText {
                text: root.node.timing || "未运行"; font: QuiFont.Caption; color: QuiColor.FontCaption
                renderType: Text.CurveRendering; renderTypeQuality: Text.VeryHighRenderTypeQuality
            }
            Item { Layout.fillWidth: true }
            QuiText {
                text: "工具缺失"; visible: root.node.missing === true; color: "#c9483d"; font: QuiFont.Caption
                renderType: Text.CurveRendering; renderTypeQuality: Text.VeryHighRenderTypeQuality
            }
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
    QuiToolTip {
        visible: pointer.containsMouse && !pointer.pressed
        x: {
            if (!parent) return 0
            let idealX = parent.width + 4
            let win = parent.Window.window ? parent.Window.window.contentItem : null
            if (win) {
                let pos = parent.mapToItem(win, 0, 0)
                if (pos.x + idealX + implicitWidth > win.width - 10) {
                    return -implicitWidth - 4
                }
            }
            return idealX
        }
        y: 0
        text: {
            let lines = []
            if (root.node.title) lines.push(root.node.title)
            if (root.node.timingDetail) {
                lines.push(root.node.timingDetail)
            } else if (root.node.timing) {
                lines.push("运行时间:" + root.node.timing)
            } else {
                lines.push("未运行")
            }
            if (root.node.missing) lines.push("⚠️ 工具缺失")
            return lines.join("\n")
        }
        delay: 500
    }
}
