import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import QtQuick.Shapes
import quickui
import VisionRT.Ui 1.0

Rectangle {
    id: root
    required property string nodeId
    required property string nodeToolName
    required property string nodeTitle
    required property string nodeCategory
    required property string nodeVersion
    required property int nodeStatus
    required property bool nodeMissing
    required property string nodeTimingText
    property bool selected: false
    property real viewScale: 1
    signal pointerPressed(var mouse, var item)
    signal pointerMoved(var mouse, var item)
    signal pointerReleased(var mouse, var item)
    signal executeRequested
    signal hoverEntered(point position)
    signal hoverExited
    width: VisionRTSize.NodeWidth
    height: VisionRTSize.NodeHeight
    radius: 2
    antialiasing: true
    color: root.isMissing ? QuiColor.Background : QuiColor.Primary
    opacity: root.isMissing ? 0.78 : 1.0
    objectName: "flowNode_" + nodeId

    readonly property int nodeState: root.nodeStatus
    readonly property bool isMissing: root.nodeMissing === true
    readonly property bool isError: !root.isMissing && root.nodeState === NodeState.Failed
    readonly property bool runningNow: !root.isMissing && root.nodeState === NodeState.Running
    readonly property color headerBg: {
        if (root.isMissing)
            return VisionRTColor.NodeWarning;
        if (root.runningNow)
            return VisionRTColor.NodeGreen;
        if (root.isError)
            return VisionRTColor.NodeRed;
        return VisionRTColor.NodeGrey;
    }
    readonly property color headerText: root.runningNow || root.isError || root.isMissing ? QuiColor.White : QuiColor.FontPrimary

    // The running border is its own layer: selecting a node must not hide that it is running.
    // Missing-plugin nodes render a dashed warning border via Shape below instead of a solid frame.
    border.color: root.runningNow ? VisionRTColor.NodeGreen : (root.isError ? VisionRTColor.NodeRed : QuiColor.Border)
    border.width: root.isMissing ? 0 : (root.runningNow ? 2 : 1)

    function getIcon(title, category) {
        let t = (title + " " + category).toLowerCase();
        if (t.indexOf("裁") >= 0 || t.indexOf("crop") >= 0 || t.indexOf("roi") >= 0)
            return QuiFontIcon.Crop;
        if (t.indexOf("读") >= 0 || t.indexOf("图") >= 0 || t.indexOf("image") >= 0)
            return QuiFontIcon.Camera;
        if (t.indexOf("检") >= 0 || t.indexOf("detect") >= 0 || t.indexOf("yolo") >= 0)
            return QuiFontIcon.Search;
        if (t.indexOf("示") >= 0 || t.indexOf("result") >= 0 || t.indexOf("view") >= 0)
            return QuiFontIcon.AllApps;
        if (t.indexOf("转") >= 0 || t.indexOf("convert") >= 0)
            return QuiFontIcon.Refresh;
        return QuiFontIcon.Settings;
    }

    // Tool identity stays independent of the editable node title.
    Rectangle {
        id: nodeHeader
        anchors.top: parent.top
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.margins: 1
        height: 18
        radius: 1
        color: root.headerBg
        Rectangle {
            anchors.bottom: parent.bottom
            anchors.left: parent.left
            anchors.right: parent.right
            height: 2
            color: parent.color
        }
        RowLayout {
            anchors.fill: parent
            anchors.leftMargin: 6
            anchors.rightMargin: 6
            spacing: 4
            QuiText {
                objectName: "nodeToolName_" + root.nodeId
                text: root.nodeToolName
                Layout.fillWidth: true
                elide: Text.ElideRight
                renderType: Text.QtRendering
                font: QuiFont.Caption
                color: root.headerText
            }
            QuiText {
                text: root.isMissing ? "缺失" : (root.nodeVersion ? ("v" + String(root.nodeVersion).replace(/^v/i, "")) : "v1.0")
                renderType: Text.QtRendering
                font: QuiFont.Caption
                color: root.headerText
            }
        }
    }

    // Body content: Icon directly placed on left, Title on top, Timing/status below
    RowLayout {
        anchors.top: nodeHeader.bottom
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.bottom: parent.bottom
        anchors.leftMargin: 8
        anchors.rightMargin: 8
        anchors.topMargin: 2
        anchors.bottomMargin: 4
        spacing: 8
        QuiTextIcon {
            Layout.preferredWidth: 16
            Layout.preferredHeight: 16
            Layout.alignment: Qt.AlignVCenter
            iconSource: root.isMissing ? QuiFontIcon.Warning : root.getIcon(root.nodeToolName, root.nodeCategory || "")
            iconSize: VisionRTSize.IconSize
            iconColor: root.isMissing ? VisionRTColor.NodeWarning : (root.isError ? VisionRTColor.NodeRed : (root.runningNow ? VisionRTColor.NodeGreen : QuiColor.FontCaption))
            renderType: Text.QtRendering
        }
        ColumnLayout {
            Layout.fillWidth: true
            spacing: 1
            QuiText {
                text: root.nodeTitle
                Layout.fillWidth: true
                elide: Text.ElideRight
                renderType: Text.QtRendering
                font: QuiFont.Body
                color: root.isMissing ? QuiColor.FontCaption : QuiColor.FontPrimary
            }
            QuiText {
                text: root.isMissing ? "插件缺失，请删除" : root.nodeTimingText
                renderType: Text.QtRendering
                font: QuiFont.Caption
                color: root.isMissing ? VisionRTColor.NodeWarning : QuiColor.FontCaption
            }
        }
    }

    Shape {
        anchors.fill: parent
        visible: root.isMissing && !root.selected
        preferredRendererType: Shape.CurveRenderer
        ShapePath {
            id: missingBorderPath
            readonly property real inset: 0.75 / Math.max(0.01, root.viewScale)
            strokeColor: VisionRTColor.NodeWarning
            strokeWidth: 1.5 / Math.max(0.01, root.viewScale)
            strokeStyle: ShapePath.DashLine
            dashPattern: [5, 3]
            fillColor: "transparent"
            startX: missingBorderPath.inset
            startY: missingBorderPath.inset
            PathLine {
                x: root.width - missingBorderPath.inset
                y: missingBorderPath.inset
            }
            PathLine {
                x: root.width - missingBorderPath.inset
                y: root.height - missingBorderPath.inset
            }
            PathLine {
                x: missingBorderPath.inset
                y: root.height - missingBorderPath.inset
            }
            PathLine {
                x: missingBorderPath.inset
                y: missingBorderPath.inset
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
            strokeColor: QuiColor.Highlight
            strokeWidth: 2 / Math.max(0.01, root.viewScale)
            strokeStyle: ShapePath.DashLine
            dashPattern: [4, 4]
            fillColor: "transparent"
            startX: selectionPath.inset
            startY: selectionPath.inset
            PathLine {
                x: root.width - selectionPath.inset
                y: selectionPath.inset
            }
            PathLine {
                x: root.width - selectionPath.inset
                y: root.height - selectionPath.inset
            }
            PathLine {
                x: selectionPath.inset
                y: root.height - selectionPath.inset
            }
            PathLine {
                x: selectionPath.inset
                y: selectionPath.inset
            }
        }
    }

    MouseArea {
        id: pointer
        anchors.fill: parent
        hoverEnabled: true
        preventStealing: true
        acceptedButtons: Qt.LeftButton | Qt.RightButton | Qt.MiddleButton
        onPressed: function (mouse) {
            root.pointerPressed(mouse, root);
        }
        onPositionChanged: function (mouse) {
            if (pressed)
                root.pointerMoved(mouse, root);
            else
                root.hoverEntered(Qt.point(mouse.x, mouse.y));
        }
        onReleased: function (mouse) {
            root.pointerReleased(mouse, root);
        }
        onDoubleClicked: function (mouse) {
            if (mouse.button === Qt.LeftButton && app.workspaceEnabled && !root.isMissing)
                root.executeRequested();
        }
        onEntered: root.hoverEntered(Qt.point(mouseX, mouseY))
        onExited: root.hoverExited()
    }
}
