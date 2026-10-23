import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import QtQuick.Window
import quickui

Item {
    id: root
    required property int row
    required property int column
    required property var model
    required property bool isTreeNode
    required property bool expanded
    required property bool hasChildren
    required property int depth
    property var treeView
    property string selectedKey: ""
    readonly property string stableKey: model.stableKey || ""
    readonly property string toolId: model.toolId || ""
    readonly property bool leaf: model.leaf === true
    readonly property bool available: model.available !== false
    readonly property string status: model.status || ""
    readonly property bool isSelected: root.selectedKey.length > 0 && root.selectedKey === root.stableKey
    signal chosen(string toolId)
    signal selected(string key)

    ToolTip {
        visible: hoverHandler.hovered && root.leaf && !root.available && root.status.length > 0
        text: root.status
        delay: 300
    }

    implicitWidth: treeView.width
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
                    root.treeView.toggleExpanded(root.row)
                    mouse.accepted = true
                }
            }
        }

        QuiTextIcon {
            visible: root.hasChildren
            iconSource: {
                let d = (root.model.display || "").toLowerCase()
                if (d.indexOf("图") >= 0 || d.indexOf("image") >= 0 || d.indexOf("采") >= 0) return QuiFontIcon.Camera
                if (d.indexOf("流程") >= 0 || d.indexOf("flow") >= 0) return QuiFontIcon.FolderOpen
                if (d.indexOf("标") >= 0 || d.indexOf("基础") >= 0) return QuiFontIcon.AllApps
                if (d.indexOf("检") >= 0 || d.indexOf("深") >= 0) return QuiFontIcon.Search
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
            text: root.model.display || ""
            Layout.fillWidth: true
            elide: Text.ElideRight
            font.pixelSize: Theme.treeItemFontSize
            color: root.isSelected
                   ? Theme.treeItemSelectedText
                   : root.model.available === false && root.leaf
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
        Drag.active: pointer.drag.active && root.leaf && root.available && !app.busy
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
            text: root.model.display || ""
            font.pixelSize: Theme.treeItemFontSize
            color: Theme.treeItemSelectedText
        }
    }

    MouseArea {
        id: pointer
        anchors.fill: parent
        hoverEnabled: false
        acceptedButtons: Qt.LeftButton
        preventStealing: root.leaf
        drag.target: root.leaf && root.available && !app.busy ? dragPayload : null
        drag.threshold: Qt.styleHints.startDragDistance
        onPressed: function(mouse) {
            let p = root.mapToItem(dragPayload.parent, mouse.x - 12, mouse.y - 16)
            dragPayload.x = p.x; dragPayload.y = p.y
        }
        onReleased: { if (root.leaf) dragPayload.Drag.drop() }
        onClicked: {
            root.selected(root.stableKey)
            if (root.hasChildren) {
                root.treeView.toggleExpanded(root.row)
            } else if (root.available) {
                root.chosen(root.toolId)
            }
        }
        onDoubleClicked: function(mouse) { mouse.accepted = true }
    }
    Accessible.name: (model.display || "") + (leaf ? "，拖动到流程图添加" : "")
}
