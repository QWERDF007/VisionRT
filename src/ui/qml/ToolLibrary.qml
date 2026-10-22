import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import quickui

Rectangle {
    id: root
    color: Theme.surfaceBg
    border.color: Theme.border
    border.width: 1
    property bool expanded: false
    property string selectedKey: ""
    property string selectedTool: ""
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
                    color: Theme.paramEditorBg; radius: Theme.radiusSmall
                    border.color: filterInput.activeFocus ? Theme.paramEditorFocusBorder : Theme.paramEditorBorder; border.width: 1
                    RowLayout {
                        anchors.fill: parent; anchors.leftMargin: 6; anchors.rightMargin: 6; spacing: 4
                        QuiTextIcon {
                            iconSource: QuiFontIcon.Search; iconSize: Theme.iconSizeSmall; iconColor: Theme.textSecondary
                        }
                        TextInput {
                            id: filterInput
                            Layout.fillWidth: true
                            font: QuiFont.Body; color: QuiColor.FontPrimary; selectByMouse: true
                            verticalAlignment: Text.AlignVCenter
                            onTextChanged: {
                                app.filterTools(text)
                                Qt.callLater(function() {
                                    if (filterInput.text.length === 0) tree.allCollapse()
                                    else tree.allExpand()
                                })
                            }
                            Text {
                                anchors.fill: parent; text: "搜索工具名"
                                color: Theme.iconMuted; font: QuiFont.Body
                                visible: !filterInput.text && !filterInput.activeFocus
                                verticalAlignment: Text.AlignVCenter
                            }
                        }
                        QuiTextIcon {
                            iconSource: QuiFontIcon.Clear; iconSize: Theme.iconSizeTiny; iconColor: Theme.iconMuted
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
            radius: 0; border.width: 0; cellHeight: Theme.treeRowHeight
            view.delegate: ToolTreeDelegate {
                treeView: tree.view
                selectedKey: root.selectedKey
                onSelected: function(key) { root.selectedKey = key }
                onChosen: function(toolId) { root.selectedTool = toolId }
            }
        }
    }
}
