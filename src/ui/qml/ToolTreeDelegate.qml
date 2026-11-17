import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import QtQuick.Window
import QtQml.Models
import quickui
import VisionRT.Style 1.0

// Shared tool tree delegate used by both ToolLibrary and CustomToolWindow.
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
    readonly property bool isSelected: root.selectedKey.length > 0 && root.selectedKey === root.stableKey
    property bool isHovered: false

    function resetHover() {
        isHovered = false
        if (pointer && pointer.hoverEnabled) {
            pointer.hoverEnabled = false
            pointer.hoverEnabled = true
        }
    }

    onRowChanged: resetHover()
    onYChanged: resetHover()
    onModelChanged: resetHover()
    onVisibleChanged: if (!visible) resetHover()

    signal chosen(string toolId)
    signal selected(string key)
    signal toggleExpandedRequested()
    signal contextMenuRequested(var mouse, var itemModel)

    implicitWidth: treeView ? treeView.width : 200
    implicitHeight: ToolTreeStyle.treeRowHeight

    Rectangle {
        anchors.fill: parent
        color: root.isSelected
               ? ToolTreeStyle.treeItemSelectedBg
               : root.isHovered
                 ? ToolTreeStyle.treeItemHoverBg
                 : "transparent"
        Rectangle {
            anchors.left: parent.left; anchors.top: parent.top; anchors.bottom: parent.bottom
            width: 2
            color: QuiColor.Transparent
            visible: root.isSelected && QuiColor.Transparent.a > 0
        }
    }

    RowLayout {
        anchors.fill: parent
        anchors.leftMargin: 5 + root.depth * ToolTreeStyle.treeIndent
        anchors.rightMargin: 5
        spacing: 5

        Item {
            Layout.preferredWidth: ToolTreeStyle.treeArrowWidth
            Layout.preferredHeight: ToolTreeStyle.treeRowHeight
            visible: root.hasChildren
            QuiTextIcon {
                anchors.centerIn: parent
                iconSource: root.expanded ? QuiFontIcon.ChevronDownSmall : QuiFontIcon.ChevronRightSmall
                iconSize: ToolTreeStyle.treeArrowSize
                iconColor: root.isSelected
                           ? QuiColor.Highlight
                           : root.isHovered
                             ? QuiColor.Highlight
                             : ToolTreeStyle.treeArrowColor
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
            iconSize: ToolTreeStyle.treeFolderIconSize
            iconColor: root.isSelected ? QuiColor.Highlight : QuiColor.Highlight
        }

        QuiText {
            visible: !root.hasChildren
            text: "◇"
            Layout.preferredWidth: ToolTreeStyle.treeArrowWidth
            horizontalAlignment: Text.AlignHCenter
            font.pixelSize: ToolTreeStyle.treeLeafIconSize
            color: root.isSelected ? QuiColor.Highlight : QuiColor.Highlight
        }

        QuiText {
            text: root.displayName
            Layout.fillWidth: true
            elide: Text.ElideRight
            font: QuiFont.Body
            color: root.isSelected ? QuiColor.Highlight : QuiColor.FontPrimary
        }
    }

    Item {
        id: dragPayload
        parent: root.Window.window ? root.Window.window.contentItem : root
        z: 1000
        width: 160; height: 32; visible: pointer.drag.active
        property string toolId: root.toolId
        Drag.active: !root.isEditor && pointer.drag.active && root.leaf && (typeof app !== "undefined" && app.workspaceEnabled)
        Drag.keys: ["visionrt/tool"]
        Drag.source: dragPayload
        Drag.hotSpot.x: 12; Drag.hotSpot.y: 16
        Rectangle {
            anchors.fill: parent
            radius: 3
            color: ToolTreeStyle.treeItemSelectedBg
            border.color: QuiColor.Highlight
            opacity: 0.95
        }
        QuiText {
            anchors.centerIn: parent
            text: root.displayName
            font: QuiFont.Body
            color: QuiColor.Highlight
        }
    }

    MouseArea {
        id: pointer
        anchors.fill: parent
        hoverEnabled: true
        acceptedButtons: Qt.LeftButton | Qt.RightButton
        onEntered: root.isHovered = true
        onExited: root.isHovered = false
        onCanceled: root.isHovered = false
        preventStealing: root.leaf && !root.isEditor
        drag.target: !root.isEditor && root.leaf && (typeof app !== "undefined" && app.workspaceEnabled) ? dragPayload : null
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
            if (root.stableKey.length > 0)
                root.selected(root.stableKey)
            if (root.treeView && root.treeView.selectionModel) {
                let idx = root.treeView.index ? root.treeView.index(root.row, root.column) : root.treeView.modelIndex(root.row, root.column)
                root.treeView.selectionModel.setCurrentIndex(idx, ItemSelectionModel.ClearAndSelect)
            }
            if (root.hasChildren) {
                if (root.treeView && root.treeView.toggleExpanded)
                    root.treeView.toggleExpanded(root.row)
                root.toggleExpandedRequested()
            } else {
                root.chosen(root.toolId)
            }
        }
        onDoubleClicked: function(mouse) { mouse.accepted = true }
    }
    Accessible.name: root.displayName + (root.leaf ? "，拖动到流程图添加" : "")
}
