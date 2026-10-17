import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import QtQuick.Shapes
import quickui

Rectangle {
    id: root
    required property var node
    property bool selected: false
    signal pointerPressed(var mouse, var item)
    signal pointerMoved(var mouse, var item)
    signal pointerReleased(var mouse, var item)
    signal executeRequested()
    width: 156; height: 52; radius: 2
    antialiasing: true
    color: "#ffffff"

    readonly property string nodeState: root.node.state || "gray"
    readonly property bool isError: root.nodeState === "red" || root.node.missing === true
    readonly property color statusDotColor: {
        if (root.nodeState === "green") return Theme.nodeStateGreen
        if (root.nodeState === "blue") return Theme.nodeStateBlue
        if (root.nodeState === "red") return Theme.nodeStateRed
        return Theme.nodeStateGray
    }
    readonly property color headerBg: {
        if (root.nodeState === "green") return Theme.nodeHeaderGreen
        if (root.nodeState === "blue") return Theme.nodeHeaderBlue
        if (root.nodeState === "red") return Theme.nodeHeaderRed
        return Theme.nodeHeaderGray
    }
    readonly property color headerText: {
        if (root.nodeState === "green") return Theme.nodeHeaderTextGreen
        if (root.nodeState === "blue") return Theme.nodeHeaderTextBlue
        if (root.nodeState === "red") return Theme.nodeHeaderTextRed
        return Theme.nodeHeaderTextGray
    }

    border.color: root.selected ? "transparent" : (root.isError ? Theme.nodeStateRed : (root.nodeState === "green" ? Theme.nodeStateGreen : "#c0c6cf"))
    border.width: root.selected ? 0 : 1

    function getIcon(title, category) {
        let t = (title + " " + category).toLowerCase()
        if (t.indexOf("裁") >= 0 || t.indexOf("crop") >= 0 || t.indexOf("roi") >= 0) return QuiFontIcon.Crop
        if (t.indexOf("读") >= 0 || t.indexOf("图") >= 0 || t.indexOf("image") >= 0) return QuiFontIcon.Camera
        if (t.indexOf("检") >= 0 || t.indexOf("detect") >= 0 || t.indexOf("yolo") >= 0) return QuiFontIcon.Search
        if (t.indexOf("示") >= 0 || t.indexOf("result") >= 0 || t.indexOf("view") >= 0) return QuiFontIcon.AllApps
        if (t.indexOf("转") >= 0 || t.indexOf("convert") >= 0) return QuiFontIcon.Refresh
        return QuiFontIcon.Settings
    }

    // Top Header with four-state background and status indicator
    Rectangle {
        id: nodeHeader
        anchors.top: parent.top; anchors.left: parent.left; anchors.right: parent.right
        anchors.margins: 1
        height: 18
        radius: 1
        color: root.headerBg
        Rectangle {
            anchors.bottom: parent.bottom; anchors.left: parent.left; anchors.right: parent.right
            height: 2; color: parent.color
        }
        RowLayout {
            anchors.fill: parent; anchors.leftMargin: 6; anchors.rightMargin: 6; spacing: 4
            Rectangle {
                width: 7; height: 7; radius: 3.5
                color: root.statusDotColor
                Layout.alignment: Qt.AlignVCenter
            }
            QuiText {
                visible: root.node.defMissing === true
                text: "缺定义"
                font: Qt.font({ family: "Microsoft YaHei UI", pixelSize: 9, bold: true })
                color: "#d97706"
                Layout.alignment: Qt.AlignVCenter
            }
            QuiText {
                visible: !root.node.defMissing && root.node.implUnavailable === true
                text: "缺实现"
                font: Qt.font({ family: "Microsoft YaHei UI", pixelSize: 9, bold: true })
                color: Theme.nodeStateOrange
                Layout.alignment: Qt.AlignVCenter
            }
            Item { Layout.fillWidth: true }
            QuiText {
                text: root.node.version ? ("v" + String(root.node.version).replace(/^v/i, "")) : "v1.0"
                font: Qt.font({ family: "Microsoft YaHei UI", pixelSize: 10 })
                color: root.headerText
            }
        }
    }

    // Body content: Icon directly placed on left, Title on top, Timing below
    RowLayout {
        anchors.top: nodeHeader.bottom; anchors.left: parent.left; anchors.right: parent.right; anchors.bottom: parent.bottom
        anchors.leftMargin: 8; anchors.rightMargin: 8; anchors.topMargin: 2; anchors.bottomMargin: 4
        spacing: 8
        QuiTextIcon {
            Layout.preferredWidth: 16; Layout.preferredHeight: 16
            Layout.alignment: Qt.AlignVCenter
            iconSource: root.getIcon(root.node.title, root.node.category || "")
            iconSize: 15
            iconColor: root.isError ? Theme.nodeStateRed : (root.nodeState === "green" ? Theme.nodeStateGreen : "#1887d8")
        }
        ColumnLayout {
            Layout.fillWidth: true; spacing: 1
            QuiText {
                text: root.node.title; Layout.fillWidth: true; elide: Text.ElideRight
                font: Qt.font({ family: "Microsoft YaHei UI", pixelSize: 11 })
                color: QuiColor.FontPrimary
            }
            QuiText {
                text: root.node.timing || "0.00ms"
                font: Qt.font({ family: "Microsoft YaHei UI", pixelSize: 10 })
                color: "#8c959f"
            }
        }
    }

    // Dashed border on selection
    Shape {
        anchors.fill: parent
        visible: root.selected
        preferredRendererType: Shape.CurveRenderer
        ShapePath {
            strokeColor: "#1887d8"
            strokeWidth: 2
            strokeStyle: ShapePath.DashLine
            dashPattern: [4, 4]
            fillColor: "transparent"
            startX: 1; startY: 1
            PathLine { x: root.width - 1; y: 1 }
            PathLine { x: root.width - 1; y: root.height - 1 }
            PathLine { x: 1; y: root.height - 1 }
            PathLine { x: 1; y: 1 }
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
            if (root.node.toolId) lines.push("工具ID: " + root.node.toolId)
            let statusText = "状态: " + (root.nodeState === "green" ? "成功" : root.nodeState === "blue" ? "运行中" : root.nodeState === "red" ? "失败" : "未运行")
            lines.push(statusText)
            if (root.node.timingDetail) {
                lines.push(root.node.timingDetail)
            } else if (root.node.timing) {
                lines.push("运行时间: " + root.node.timing)
            }
            if (root.node.defMissing) {
                lines.push("⚠️ 算子定义缺失 (未找到 YAML 注册元数据)")
            } else if (root.node.implUnavailable) {
                lines.push("⚠️ 算子实现不可用 (未加载或缺失可执行实现)")
            }
            return lines.join("\n")
        }
        delay: 500
    }
}
