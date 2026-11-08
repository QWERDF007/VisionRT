import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import QtQuick.Window
import quickui
import VisionRT.Style 1.0

// Shared tool tree delegate used by both ToolLibrary and CustomToolDialog.
// Provides identical semantic category icons, selection styling, and interaction.
Item {
    id: root

    required property int row
    required property int column
    required property var model
    required property bool isTreeNode
    required property bool expanded
    required property bool hasChildren
    required property int depth

    property var treeView: null
    property string selectedKey: ""
    property bool isEditor: false

    readonly property string displayName: {
        if (!model) return ""
        if (model.display !== undefined) return model.display
        if (model.name !== undefined) return model.name
        return ""
    }
    readonly property string stableKey: {
        if (!model) return ""
        if (model.stableKey !== undefined) return model.stableKey
        if (model.key !== undefined) return model.key
        return ""
    }
    readonly property string toolId: {
        if (!model) return ""
        if (model.toolId !== undefined) return model.toolId
        return ""
    }
    readonly property bool leaf: {
        if (!model) return !root.hasChildren
        if (model.leaf !== undefined) return model.leaf === true
        return !root.hasChildren
    }
    readonly property bool available: {
        if (!model) return true
        if (model.available !== undefined) return model.available !== false
        return true
    }
    readonly property string status: {
        if (!model) return ""
        if (model.status !== undefined) return model.status
        return ""
    }
    readonly property bool isSelected: root.selectedKey.length > 0 && root.selectedKey === root.stableKey

    signal chosen(string toolId)
    signal selected(string key)
    signal toggleExpandedRequested()
    signal contextMenuRequested(var mouse, var itemModel)

    ToolTip {
        visible: hoverHandler.hovered && root.leaf && !root.available && root.status.length > 0
        text: root.status
        delay: 300
    }

    implicitWidth: treeView ? treeView.width : 200
    implicitHeight: Theme.treeRowHeight

    HoverHandler {
        id: hoverHandler
    }

    Rectangle {
        anchors.fill: parent
        color: root.isSelected
               ? Theme.treeItemSelectedBg
               : hoverHandler.hovered
                 ? Theme.treeItemHoverBg
                 : "transparent"
        Rectangle {
            anchors.left: parent.left; anchors.top: parent.top; anchors.bottom: parent.bottom
            width: 2
            color: Theme.treeItemSelectedIndicator
            visible: root.isSelected && Theme.treeItemSelectedIndicator.a > 0
        }
    }

    RowLayout {
        anchors.fill: parent
        anchors.leftMargin: 7 + root.depth * Theme.treeIndent
        anchors.rightMargin: 8
        spacing: 6

        Item {
            Layout.preferredWidth: Theme.treeArrowWidth
            Layout.preferredHeight: Theme.treeRowHeight
            visible: root.hasChildren
            QuiTextIcon {
                anchors.centerIn: parent
                iconSource: root.expanded ? QuiFontIcon.ChevronDownSmall : QuiFontIcon.ChevronRightSmall
                iconSize: Theme.treeArrowSize
                iconColor: root.isSelected
                           ? Theme.treeItemSelectedText
                           : hoverHandler.hovered
                             ? Theme.treeArrowHoverColor
                             : Theme.treeArrowColor
            }
            MouseArea {
                anchors.fill: parent
                acceptedButtons: Qt.LeftButton
                onClicked: function(mouse) {
                    if (root.treeView && root.treeView.toggleExpanded)
                        root.treeView.toggleExpanded(root.row)
                    root.toggleExpandedRequested()
                    mouse.accepted = true
                }
            }
        }

        // Semantic category icon matching main interface tool library
        QuiTextIcon {
            visible: root.hasChildren
            iconSource: {
                let d = (root.displayName || "").toLowerCase()
                if (d.indexOf("图") >= 0 || d.indexOf("image") >= 0 || d.indexOf("采") >= 0) return QuiFontIcon.Camera
                if (d.indexOf("流程") >= 0 || d.indexOf("flow") >= 0) return QuiFontIcon.FolderOpen
                if (d.indexOf("标") >= 0 || d.indexOf("基础") >= 0) return QuiFontIcon.AllApps
                if (d.indexOf("检") >= 0 || d.indexOf("深") >= 0) return QuiFontIcon.Search
                if (d.indexOf("扩") >= 0 || d.indexOf("应用") >= 0 || d.indexOf("app") >= 0) return QuiFontIcon.AllApps
                if (d.indexOf("示例") >= 0 || d.indexOf("demo") >= 0 || d.indexOf("例") >= 0) return QuiFontIcon.FolderOpen
                return QuiFontIcon.Settings
            }
            iconSize: Theme.treeFolderIconSize
            iconColor: root.isSelected ? Theme.treeItemSelectedIcon : Theme.treeFolderIconColor
        }

        QuiText {
            visible: !root.hasChildren
            text: "◇"
            Layout.preferredWidth: Theme.treeArrowWidth
            horizontalAlignment: Text.AlignHCenter
            font.pixelSize: Theme.treeLeafIconSize
            color: root.isSelected ? Theme.treeItemSelectedIcon : Theme.treeLeafIconColor
        }

        QuiText {
            text: root.displayName
            Layout.fillWidth: true
            elide: Text.ElideRight
            font: QuiFont.Body
            color: root.isSelected
                   ? Theme.treeItemSelectedText
                   : root.available === false && root.leaf
                     ? Theme.treeItemUnavailableText
                     : Theme.treeItemText
        }
    }

    Item {
        id: dragPayload
        parent: root.Window.window ? root.Window.window.contentItem : root
        z: 1000
        width: 160; height: 32; visible: pointer.drag.active
        property string toolId: root.toolId
        Drag.active: !root.isEditor && pointer.drag.active && root.leaf && root.available && (typeof app !== "undefined" && app.workspaceEnabled)
        Drag.keys: ["visionrt/tool"]
        Drag.source: dragPayload
        Drag.hotSpot.x: 12; Drag.hotSpot.y: 16
        Rectangle {
            anchors.fill: parent
            radius: Theme.radiusSmall
            color: Theme.treeItemSelectedBg
            border.color: Theme.highlight
            opacity: 0.95
        }
        QuiText {
            anchors.centerIn: parent
            text: root.displayName
            font: QuiFont.Body
            color: Theme.treeItemSelectedText
        }
    }

    MouseArea {
        id: pointer
        anchors.fill: parent
        hoverEnabled: false
        acceptedButtons: Qt.LeftButton | Qt.RightButton
        preventStealing: root.leaf && !root.isEditor
        drag.target: !root.isEditor && root.leaf && root.available && (typeof app !== "undefined" && app.workspaceEnabled) ? dragPayload : null
        drag.threshold: Qt.styleHints.startDragDistance
        onPressed: function(mouse) {
            if (mouse.button === Qt.LeftButton && !root.isEditor) {
                let p = root.mapToItem(dragPayload.parent, mouse.x - 12, mouse.y - 16)
                dragPayload.x = p.x; dragPayload.y = p.y
            }
        }
        onReleased: function(mouse) {
            if (mouse.button === Qt.LeftButton && root.leaf && !root.isEditor)
                dragPayload.Drag.drop()
        }
        onClicked: function(mouse) {
            if (mouse.button === Qt.RightButton) {
                root.contextMenuRequested(mouse, root.model)
                return
            }
            if (root.hasChildren) {
                if (root.treeView && root.treeView.toggleExpanded)
                    root.treeView.toggleExpanded(root.row)
                root.toggleExpandedRequested()
            } else {
                root.selected(root.stableKey)
                if (root.available)
                    root.chosen(root.toolId)
            }
        }
        onDoubleClicked: function(mouse) { mouse.accepted = true }
    }
    Accessible.name: root.displayName + (root.leaf ? "，拖动到流程图添加" : "")
}
