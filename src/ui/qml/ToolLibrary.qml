import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import quickui

Rectangle {
    id: root
    color: QuiColor.Primary
    property bool expanded: true
    property string selectedTool: ""
    property var expandedBranches: ({})
    signal toggleRequested()
    ColumnLayout {
        anchors.fill: parent; spacing: 0
        RowLayout {
            Layout.fillWidth: true; Layout.preferredHeight: 30; spacing: 3
            QuiTextIconButton {
                text: "工具库"; iconSource: root.expanded ? QuiFontIcon.ChevronDown : QuiFontIcon.ChevronRight
                display: AbstractButton.TextBesideIcon
                onClicked: root.toggleRequested()
            }
            Item { Layout.fillWidth: true }
            QuiTextField {
                id: filter; visible: root.expanded; Layout.fillWidth: true; Layout.maximumWidth: 240
                implicitHeight: 28; placeholderText: "搜索工具名称"; Accessible.name: "搜索工具"
                onTextChanged: { app.filterTools(text); if (text.length) Qt.callLater(tree.allExpand) }
            }
            QuiTextIconButton {
                visible: root.expanded; text: "刷新 Python 工具"; iconSource: QuiFontIcon.Refresh
                enabled: !app.busy; onClicked: app.refreshPython()
            }
        }
        QuiTreeView {
            id: tree
            objectName: "toolTree"
            visible: root.expanded
            Layout.fillWidth: true; Layout.fillHeight: true
            model: app.toolModel; displayRole: "display"
            radius: 0; border.width: 0; cellHeight: 32
            view.delegate: ToolTreeDelegate {
                treeView: tree.view
                selectedTool: root.selectedTool
                filtering: filter.text.trim().length > 0
                expandedBranches: root.expandedBranches
                onBranchToggled: function(key, expanded) {
                    if (!filter.text.trim().length) {
                        let saved = Object.assign({}, root.expandedBranches)
                        saved[key] = expanded
                        root.expandedBranches = saved
                    }
                }
                onChosen: function(toolId) { root.selectedTool = toolId }
            }
        }
    }
}
