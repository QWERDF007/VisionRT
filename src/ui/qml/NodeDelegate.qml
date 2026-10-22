import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import QtQuick.Shapes
import quickui

Rectangle {
    id: root
    required property var node
    property bool selected: false
    property real viewScale: 1
    signal pointerPressed(var mouse, var item)
    signal pointerMoved(var mouse, var item)
    signal pointerReleased(var mouse, var item)
    signal executeRequested()
    signal hoverEntered()
    signal hoverExited()
    width: 156; height: 52; radius: 2
    antialiasing: true
    color: "#ffffff"
    objectName: "flowNode_" + node.id

    readonly property string nodeState: root.node.state || "idle"
    readonly property bool isError: root.nodeState === "failed" || root.node.missing === true
    readonly property color statusDotColor: {
        if (root.nodeState === "running") return Theme.nodeStateGreen
        if (root.nodeState === "failed") return Theme.nodeStateRed
        return Theme.nodeStateGray
    }
    readonly property color headerBg: {
        if (root.nodeState === "running") return Theme.nodeHeaderGreen
        if (root.nodeState === "failed") return Theme.nodeHeaderRed
        return Theme.nodeHeaderGray
    }
    readonly property color headerText: {
        if (root.nodeState === "running") return Theme.nodeHeaderTextGreen
        if (root.nodeState === "failed") return Theme.nodeHeaderTextRed
        return Theme.nodeHeaderTextGray
    }

    border.color: root.selected ? "transparent" : (root.isError ? Theme.nodeStateRed : (root.nodeState === "running" ? Theme.nodeStateGreen : "#c0c6cf"))
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
                renderType: Text.QtRendering
                font: Qt.font({ family: "Microsoft YaHei UI", pixelSize: 9, bold: true })
                color: "#d97706"
                Layout.alignment: Qt.AlignVCenter
            }
            QuiText {
                visible: !root.node.defMissing && root.node.implUnavailable === true
                text: "缺实现"
                renderType: Text.QtRendering
                font: Qt.font({ family: "Microsoft YaHei UI", pixelSize: 9, bold: true })
                color: Theme.nodeStateOrange
                Layout.alignment: Qt.AlignVCenter
            }
            QuiText {
                visible: !root.node.defMissing && !root.node.implUnavailable && root.nodeState === "failed"
                text: "执行错误"
                renderType: Text.QtRendering
                font: Qt.font({ family: "Microsoft YaHei UI", pixelSize: 9, bold: true })
                color: Theme.nodeStateRed
                Layout.alignment: Qt.AlignVCenter
            }
            Item { Layout.fillWidth: true }
            QuiText {
                text: root.node.version ? ("v" + String(root.node.version).replace(/^v/i, "")) : "v1.0"
                renderType: Text.QtRendering
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
            iconSize: Theme.iconSizeBase
            iconColor: root.isError ? Theme.nodeStateRed : (root.nodeState === "running" ? Theme.nodeStateGreen : Theme.nodeStateGray)
            renderType: Text.QtRendering
        }
        ColumnLayout {
            Layout.fillWidth: true; spacing: 1
            QuiText {
                text: root.node.title; Layout.fillWidth: true; elide: Text.ElideRight
                renderType: Text.QtRendering
                font: Qt.font({ family: "Microsoft YaHei UI", pixelSize: Theme.fontSizeSmall })
                color: QuiColor.FontPrimary
            }
            QuiText {
                text: root.node.timingText
                renderType: Text.QtRendering
                font: Qt.font({ family: "Microsoft YaHei UI", pixelSize: Theme.fontSizeCaption })
                color: Theme.iconMuted
            }
        }
    }

    Shape {
        anchors.fill: parent
        visible: root.selected
        preferredRendererType: Shape.CurveRenderer
        ShapePath {
            id: selectionPath
            readonly property real inset: 1 / Math.max(0.01, root.viewScale)
            strokeColor: "#1887d8"
            strokeWidth: 2 / Math.max(0.01, root.viewScale)
            strokeStyle: ShapePath.DashLine
            dashPattern: [4, 4]
            fillColor: "transparent"
            startX: selectionPath.inset; startY: selectionPath.inset
            PathLine { x: root.width - selectionPath.inset; y: selectionPath.inset }
            PathLine { x: root.width - selectionPath.inset; y: root.height - selectionPath.inset }
            PathLine { x: selectionPath.inset; y: root.height - selectionPath.inset }
            PathLine { x: selectionPath.inset; y: selectionPath.inset }
        }
    }

    MouseArea {
        id: pointer; anchors.fill: parent; hoverEnabled: true; preventStealing: true
        acceptedButtons: Qt.LeftButton | Qt.RightButton | Qt.MiddleButton
        onPressed: function(mouse) { root.pointerPressed(mouse, root) }
        onPositionChanged: function(mouse) { if (pressed) root.pointerMoved(mouse, root) }
        onReleased: function(mouse) { root.pointerReleased(mouse, root) }
        onDoubleClicked: function(mouse) { if (mouse.button === Qt.LeftButton && !app.busy) root.executeRequested() }
        onEntered: root.hoverEntered()
        onExited: root.hoverExited()
    }
}
