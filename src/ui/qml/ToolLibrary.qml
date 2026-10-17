import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import quickui

Rectangle {
    id: root
    color: Theme.surfaceBg
    border.color: Theme.border
    border.width: 1
    property bool expanded: true
    property string selectedTool: ""
    property var expandedBranches: ({})
    signal toggleRequested()
    ColumnLayout {
        anchors.fill: parent; anchors.margins: 1; spacing: 0
        Rectangle {
            Layout.fillWidth: true; Layout.preferredHeight: 34; color: Theme.tabBarBg
            Rectangle {
                anchors.left: parent.left; anchors.right: parent.right; anchors.bottom: parent.bottom
                height: 1; color: Theme.tabBorder
            }
            RowLayout {
                anchors.fill: parent; anchors.leftMargin: 8; anchors.rightMargin: 8; spacing: 6
                QuiText {
                    text: "工具库"
                    font: Qt.font({ family: "Microsoft YaHei UI", pixelSize: 12, bold: true })
                    color: Theme.textPrimary
                }
                Item { Layout.fillWidth: true }
                Rectangle {
                    visible: root.expanded
                    Layout.preferredWidth: Math.min(200, root.width - 120)
                    Layout.preferredHeight: 24
                    color: "#ffffff"; radius: 3
                    border.color: filterInput.activeFocus ? QuiColor.Highlight : "#c5ccd5"; border.width: 1
                    RowLayout {
                        anchors.fill: parent; anchors.leftMargin: 6; anchors.rightMargin: 6; spacing: 4
                        QuiTextIcon {
                            iconSource: QuiFontIcon.Search; iconSize: 12; iconColor: "#8c959f"
                        }
                        TextInput {
                            id: filterInput
                            Layout.fillWidth: true
                            font: QuiFont.Body; color: QuiColor.FontPrimary; selectByMouse: true
                            verticalAlignment: Text.AlignVCenter
                            onTextChanged: { app.filterTools(text); if (text.length) Qt.callLater(tree.allExpand) }
                            Text {
                                anchors.fill: parent; text: "搜索工具名"
                                color: "#8c959f"; font: QuiFont.Body
                                visible: !filterInput.text && !filterInput.activeFocus
                                verticalAlignment: Text.AlignVCenter
                            }
                        }
                        QuiTextIcon {
                            iconSource: QuiFontIcon.Clear; iconSize: 11; iconColor: "#8c959f"
                            visible: filterInput.text.length > 0
                            MouseArea {
                                anchors.fill: parent
                                onClicked: filterInput.text = ""
                            }
                        }
                    }
                }
                QuiTextIconButton {
                    visible: root.expanded
                    iconSource: QuiFontIcon.Refresh
                    implicitWidth: 24; implicitHeight: 24
                    text: "刷新 Python 工具"
                    enabled: !app.busy; onClicked: app.refreshPython()
                }
                QuiTextIconButton {
                    iconSource: root.expanded ? QuiFontIcon.ChevronDown : QuiFontIcon.ChevronRight
                    implicitWidth: 24; implicitHeight: 24
                    text: root.expanded ? "折叠工具库" : "展开工具库"
                    onClicked: root.toggleRequested()
                }
            }
        }
        QuiTreeView {
            id: tree
            objectName: "toolTree"
            visible: root.expanded
            Layout.fillWidth: true; Layout.fillHeight: true
            model: app.toolModel; displayRole: "display"
            radius: 0; border.width: 0; cellHeight: 26
            view.delegate: ToolTreeDelegate {
                treeView: tree.view
                selectedTool: root.selectedTool
                filtering: filterInput.text.trim().length > 0
                expandedBranches: root.expandedBranches
                onBranchToggled: function(key, expanded) {
                    if (!filterInput.text.trim().length) {
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
