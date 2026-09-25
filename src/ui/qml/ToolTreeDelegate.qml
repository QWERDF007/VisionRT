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
    property string selectedTool: ""
    property bool filtering: false
    property var expandedBranches: ({})
    readonly property string stableKey: model.stableKey || ""
    readonly property string toolId: model.toolId || ""
    readonly property bool leaf: model.leaf === true
    signal chosen(string toolId)
    signal branchToggled(string key, bool expanded)
    function restoreExpansion() {
        if (!hasChildren) return
        let wanted = filtering || expandedBranches[stableKey] !== false
        if (wanted && !expanded) treeView.expand(row)
        else if (!wanted && expanded) treeView.collapse(row)
    }
    Component.onCompleted: Qt.callLater(restoreExpansion)
    onStableKeyChanged: Qt.callLater(restoreExpansion)
    onFilteringChanged: Qt.callLater(restoreExpansion)
    implicitWidth: treeView.width
    implicitHeight: 32
    Rectangle {
        anchors.fill: parent
        color: root.leaf && root.selectedTool === root.toolId ? "#d9edfc" : pointer.containsMouse ? "#edf4fa" : "transparent"
    }
    RowLayout {
        anchors.fill: parent; anchors.leftMargin: 7 + root.depth * 16; anchors.rightMargin: 8; spacing: 6
        QuiText { text: root.hasChildren ? (root.expanded ? "⌄" : "›") : ""; Layout.preferredWidth: 14; color: QuiColor.FontCaption }
        QuiText { text: root.model.display || ""; Layout.fillWidth: true; elide: Text.ElideRight; color: root.model.available === false && root.leaf ? "#a34938" : QuiColor.FontPrimary }
        QuiText { text: root.leaf ? root.model.version || "" : ""; font: QuiFont.Caption; color: QuiColor.FontCaption }
    }
    Item {
        id: dragPayload
        parent: root.Window.window ? root.Window.window.contentItem : root
        z: 1000
        width: 160; height: 32; visible: pointer.drag.active
        property string toolId: root.toolId
        Drag.active: pointer.drag.active && root.leaf && !app.busy
        Drag.keys: ["visionrt/tool"]
        Drag.source: dragPayload
        Drag.hotSpot.x: 12; Drag.hotSpot.y: 16
        Rectangle { anchors.fill: parent; radius: 4; color: "#deeffd"; border.color: QuiColor.Highlight; opacity: 0.95 }
        QuiText { anchors.centerIn: parent; text: root.model.display || "" }
    }
    MouseArea {
        id: pointer
        anchors.fill: parent; hoverEnabled: true
        acceptedButtons: Qt.LeftButton
        preventStealing: root.leaf
        drag.target: root.leaf && !app.busy ? dragPayload : null
        drag.threshold: Qt.styleHints.startDragDistance
        onPressed: function(mouse) {
            let p = root.mapToItem(dragPayload.parent, mouse.x - 12, mouse.y - 16)
            dragPayload.x = p.x; dragPayload.y = p.y
        }
        onReleased: { if (root.leaf) dragPayload.Drag.drop() }
        onClicked: {
            if (root.hasChildren) {
                root.branchToggled(root.stableKey, !root.expanded)
                root.treeView.toggleExpanded(root.row)
            }
            else root.chosen(root.toolId)
        }
        onDoubleClicked: function(mouse) { mouse.accepted = true }
    }
    Accessible.name: (model.display || "") + (leaf ? "，拖动到流程图添加" : "")
}
