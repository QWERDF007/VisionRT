import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import quickui
import VisionRT.Ui 1.0

Rectangle {
    id: root
    property var flows: []
    property string currentFlow: ""
    property bool workspaceEnabled: false
    signal addRequested
    signal selectRequested(string id)
    signal contextRequested(string id)
    signal deleteRequested(string id)
    implicitHeight: VisionRTSize.BarSize
    color: QuiColor.Primary
    Rectangle {
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.bottom: parent.bottom
        height: 1
        color: QuiColor.Border
    }
    RowLayout {
        anchors.fill: parent
        anchors.leftMargin: 5
        anchors.rightMargin: 5
        spacing: 5
        WorkbenchActionButton {
            objectName: "flowAddButton"
            Layout.preferredWidth: VisionRTSize.IconButtonSize
            Layout.preferredHeight: VisionRTSize.IconButtonSize
            Layout.alignment: Qt.AlignVCenter
            text: "添加流程"
            iconSource: QuiFontIcon.Add
            iconColor: QuiColor.FontCaption
            normalColor: QuiColor.Button
            enabled: root.workspaceEnabled
            onClicked: root.addRequested()
        }
        Flickable {
            Layout.fillWidth: true
            Layout.fillHeight: true
            clip: true
            boundsBehavior: Flickable.StopAtBounds
            boundsMovement: Flickable.StopAtBounds
            contentWidth: flowTabs.width
            contentHeight: height
            Row {
                id: flowTabs
                height: parent.height
                spacing: 5
                Repeater {
                    model: root.flows
                    Rectangle {
                        id: flowTabItem
                        objectName: "flowTab_" + modelData.id
                        required property var modelData
                        readonly property bool isActive: root.currentFlow === modelData.id
                        anchors.top: parent.top
                        anchors.topMargin: 5
                        anchors.bottom: parent.bottom
                        width: tabRow.width + 16
                        radius: 3
                        color: isActive ? QuiColor.Button : QuiColor.Primary
                        border.color: QuiColor.Border
                        border.width: 1
                        Rectangle {
                            visible: flowTabItem.isActive
                            anchors.left: parent.left
                            anchors.right: parent.right
                            anchors.bottom: parent.bottom
                            height: 2
                            color: QuiColor.Highlight
                        }
                        MouseArea {
                            anchors.fill: parent
                            acceptedButtons: Qt.LeftButton | Qt.RightButton
                            onClicked: function (mouse) {
                                if (mouse.button === Qt.LeftButton) {
                                    root.selectRequested(flowTabItem.modelData.id);
                                } else if (mouse.button === Qt.RightButton) {
                                    root.contextRequested(flowTabItem.modelData.id);
                                }
                            }
                        }
                        RowLayout {
                            id: tabRow
                            anchors.centerIn: parent
                            spacing: 5
                            Rectangle {
                                objectName: "flowTabIndicator_" + flowTabItem.modelData.id
                                width: 7
                                height: 7
                                radius: 3.5
                                color: flowTabItem.modelData.selected ? VisionRTColor.NodeGreen : "transparent"
                                border.color: flowTabItem.modelData.selected ? VisionRTColor.NodeGreen : QuiColor.FontCaption
                                border.width: 1
                            }
                            QuiText {
                                text: flowTabItem.modelData.name
                                font: QuiFont.Body
                                color: flowTabItem.isActive ? QuiColor.Highlight : QuiColor.FontCaption
                            }
                            WorkbenchActionButton {
                                objectName: "flowTabClose_" + flowTabItem.modelData.id
                                text: "删除流程"
                                implicitWidth: VisionRTSize.IconButtonSize
                                implicitHeight: VisionRTSize.IconButtonSize
                                iconSource: QuiFontIcon.Cancel
                                iconSize: VisionRTSize.IconSize
                                iconColor: hovered ? QuiColor.Error : QuiColor.FontCaption
                                normalColor: "transparent"
                                opacity: root.flows.length > 1 ? 1.0 : 0.4
                                enabled: root.workspaceEnabled && root.flows.length > 1
                                onClicked: {
                                    root.deleteRequested(flowTabItem.modelData.id);
                                }
                            }
                        }
                    }
                }
            }
        }
    }
}
