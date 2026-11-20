import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import QtQuick.Window
import QtQml.Models
import quickui
import VisionRT.Ui 1.0

// Shared tool tree row for the library and tool editor.
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
        if (!model)
            return "";
        if (model.display !== undefined)
            return model.display;
        if (model.name !== undefined)
            return model.name;
        return "";
    }
    readonly property string stableKey: {
        if (!model)
            return "";
        if (model.stableKey !== undefined)
            return model.stableKey;
        if (model.key !== undefined)
            return model.key;
        return "";
    }
    readonly property string toolId: {
        if (!model)
            return "";
        if (model.toolId !== undefined)
            return model.toolId;
        return "";
    }
    readonly property bool leaf: {
        if (!model)
            return !root.hasChildren;
        if (model.leaf !== undefined)
            return model.leaf === true;
        return !root.hasChildren;
    }
    readonly property bool isSelected: root.selectedKey.length > 0 && root.selectedKey === root.stableKey
    property bool isHovered: false

    function resetHover() {
        isHovered = false;
        if (pointer && pointer.hoverEnabled) {
            pointer.hoverEnabled = false;
            pointer.hoverEnabled = true;
        }
    }

    onRowChanged: resetHover()
    onYChanged: resetHover()
    onModelChanged: resetHover()
    onVisibleChanged: if (!visible)
        resetHover()

    signal chosen(string toolId)
    signal selected(string key)
    signal toggleExpandedRequested
    signal contextMenuRequested(var mouse, var itemModel)

    implicitWidth: treeView ? treeView.width : 200
    implicitHeight: VisionRTSize.ContentSize

    Rectangle {
        anchors.fill: parent
        color: root.isSelected || root.isHovered ? QuiColor.Highlight : QuiColor.Primary
    }

    RowLayout {
        anchors.fill: parent
        anchors.leftMargin: 5 + root.depth * 20
        anchors.rightMargin: 5
        spacing: 5

        Item {
            Layout.preferredWidth: VisionRTSize.IconSize
            Layout.preferredHeight: VisionRTSize.ContentSize
            visible: root.hasChildren
            QuiTextIcon {
                anchors.centerIn: parent
                iconSource: root.expanded ? QuiFontIcon.ChevronDownSmall : QuiFontIcon.ChevronRightSmall
                iconSize: VisionRTSize.IconSize
                iconColor: QuiColor.FontPrimary
            }
            MouseArea {
                anchors.fill: parent
                acceptedButtons: Qt.LeftButton
                onClicked: function (mouse) {
                    if (root.treeView && root.treeView.toggleExpanded)
                        root.treeView.toggleExpanded(root.row);
                    root.toggleExpandedRequested();
                    mouse.accepted = true;
                }
            }
        }

        QuiText {
            text: root.displayName
            Layout.fillWidth: true
            elide: Text.ElideRight
            font: QuiFont.Body
            color: QuiColor.FontPrimary
        }
    }

    Item {
        id: dragPayload
        parent: root.Window.window ? root.Window.window.contentItem : root
        z: 1000
        width: 160
        height: 32
        visible: pointer.drag.active
        property string toolId: root.toolId
        Drag.active: !root.isEditor && pointer.drag.active && root.leaf && (typeof app !== "undefined" && app.workspaceEnabled)
        Drag.keys: ["visionrt/tool"]
        Drag.source: dragPayload
        Drag.hotSpot.x: 12
        Drag.hotSpot.y: 16
        Rectangle {
            anchors.fill: parent
            radius: 3
            color: QuiColor.Primary
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
        onPressed: function (mouse) {
            if (mouse.button === Qt.LeftButton && !root.isEditor) {
                let p = root.mapToItem(dragPayload.parent, mouse.x - 12, mouse.y - 16);
                dragPayload.x = p.x;
                dragPayload.y = p.y;
            }
        }
        onReleased: function (mouse) {
            if (mouse.button === Qt.LeftButton && root.leaf && !root.isEditor)
                dragPayload.Drag.drop();
        }
        onClicked: function (mouse) {
            if (mouse.button === Qt.RightButton) {
                root.contextMenuRequested(mouse, root.model);
                return;
            }
            if (root.stableKey.length > 0)
                root.selected(root.stableKey);
            if (root.treeView && root.treeView.selectionModel) {
                let idx = root.treeView.index ? root.treeView.index(root.row, root.column) : root.treeView.modelIndex(root.row, root.column);
                root.treeView.selectionModel.setCurrentIndex(idx, ItemSelectionModel.ClearAndSelect);
            }
            if (root.hasChildren) {
                if (root.treeView && root.treeView.toggleExpanded)
                    root.treeView.toggleExpanded(root.row);
                root.toggleExpandedRequested();
            } else {
                root.chosen(root.toolId);
            }
        }
        onDoubleClicked: function (mouse) {
            mouse.accepted = true;
        }
    }
    Accessible.name: root.displayName + (root.leaf ? "，拖动到流程图添加" : "")
}
