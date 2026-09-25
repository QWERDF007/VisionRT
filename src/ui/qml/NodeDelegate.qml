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
    width: 168; height: 70; radius: 4
    antialiasing: true
    color: selected ? "#eef6fc" : "#ffffff"
    border.color: selected ? QuiColor.Highlight : root.stateColor !== "#a5adb8" ? root.stateColor : "#b8c2cc"
    border.width: selected ? 2 : 1

    readonly property color stateColor: node.state === "blue" ? "#1887d8" : node.state === "green" ? "#2da44e" : node.state === "red" ? "#cf222e" : "#a5adb8"

    function getIcon(title, category) {
        let t = (title + " " + category).toLowerCase()
        if (t.indexOf("裁") >= 0 || t.indexOf("crop") >= 0 || t.indexOf("roi") >= 0) return QuiFontIcon.Crop
        if (t.indexOf("读") >= 0 || t.indexOf("图") >= 0 || t.indexOf("image") >= 0) return QuiFontIcon.Camera
        if (t.indexOf("检") >= 0 || t.indexOf("detect") >= 0 || t.indexOf("yolo") >= 0) return QuiFontIcon.Search
        if (t.indexOf("示") >= 0 || t.indexOf("result") >= 0 || t.indexOf("view") >= 0) return QuiFontIcon.AllApps
        if (t.indexOf("转") >= 0 || t.indexOf("convert") >= 0) return QuiFontIcon.Refresh
        return QuiFontIcon.Settings
    }

    Rectangle {
        id: nodeHeader
        anchors.top: parent.top; anchors.left: parent.left; anchors.right: parent.right
        anchors.topMargin: root.border.width > 1 ? 1 : 0
        anchors.leftMargin: root.border.width > 1 ? 1 : 0
        anchors.rightMargin: root.border.width > 1 ? 1 : 0
        height: 20
        radius: 4
        color: root.selected ? "#c77848" : "#cfd6df"
        Rectangle {
            anchors.bottom: parent.bottom; anchors.left: parent.left; anchors.right: parent.right
            height: 4; color: parent.color
        }
        RowLayout {
            anchors.fill: parent; anchors.leftMargin: 6; anchors.rightMargin: 6; spacing: 4
            Item { Layout.fillWidth: true }
            QuiText {
                text: root.node.version ? ("v" + String(root.node.version).replace(/^v/i, "")) : "v1.0"
                font: Qt.font({ family: "Microsoft YaHei UI", pixelSize: 10, bold: true })
                color: root.selected ? "#ffffff" : "#57606a"
                renderType: Text.CurveRendering; renderTypeQuality: Text.VeryHighRenderTypeQuality
            }
        }
    }

    RowLayout {
        anchors.top: nodeHeader.bottom; anchors.left: parent.left; anchors.right: parent.right; anchors.bottom: parent.bottom
        anchors.margins: 6; spacing: 6
        Rectangle {
            Layout.preferredWidth: 26; Layout.preferredHeight: 26; radius: 3
            color: "#e6f1fb"; border.color: "#bfe0f9"; border.width: 1
            QuiTextIcon {
                anchors.centerIn: parent
                iconSource: root.getIcon(root.node.title, root.node.category || "")
                iconSize: 14; iconColor: QuiColor.Highlight
            }
        }
        ColumnLayout {
            Layout.fillWidth: true; spacing: 2
            QuiText {
                text: root.node.title; Layout.fillWidth: true; elide: Text.ElideRight
                font: Qt.font({ family: "Microsoft YaHei UI", pixelSize: 12, bold: true })
                color: QuiColor.FontPrimary
                renderType: Text.CurveRendering; renderTypeQuality: Text.VeryHighRenderTypeQuality
            }
            RowLayout {
                Layout.fillWidth: true; spacing: 4
                QuiText {
                    text: root.node.timing || "未运行"
                    font: QuiFont.Caption; color: "#57606a"
                    renderType: Text.CurveRendering; renderTypeQuality: Text.VeryHighRenderTypeQuality
                }
                Item { Layout.fillWidth: true }
                QuiText {
                    text: "缺失"; visible: root.node.missing === true; color: "#cf222e"
                    font: Qt.font({ family: "Microsoft YaHei UI", pixelSize: 10, bold: true })
                    renderType: Text.CurveRendering; renderTypeQuality: Text.VeryHighRenderTypeQuality
                }
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
