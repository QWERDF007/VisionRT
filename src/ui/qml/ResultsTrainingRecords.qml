import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import quickui
import VisionRT.Ui 1.0

ColumnLayout {
    id: root
    required property QtObject controller
    enabled: controller.workspaceEnabled
    spacing: 0

    Item {
        Layout.fillWidth: true
        Layout.fillHeight: true
        ListView {
            id: trainingList
            objectName: "trainingRecords"
            anchors.fill: parent
            anchors.topMargin: VisionRTSize.Spacing
            anchors.bottomMargin: VisionRTSize.Spacing
            enabled: root.controller.workspaceEnabled
            clip: true
            spacing: VisionRTSize.Spacing
            boundsBehavior: Flickable.StopAtBounds
            model: root.controller.trainingRecords
            ScrollBar.vertical: WorkbenchScrollBar {
                id: trainingScrollBar
                parent: trainingList.parent
                anchors.top: parent.top
                anchors.bottom: parent.bottom
                anchors.right: parent.right
            }
            delegate: Item {
                id: recordRow
                required property var modelData
                required property int index
                width: trainingList.width
                height: content.implicitHeight
                ColumnLayout {
                    id: content
                    anchors.left: parent.left
                    anchors.right: parent.right
                    anchors.leftMargin: VisionRTSize.ThumbnailHorizontalMargin
                    anchors.rightMargin: VisionRTSize.ThumbnailHorizontalMargin
                    anchors.top: parent.top
                    spacing: VisionRTSize.Spacing
                    ResultsThumbnailCard {
                        id: thumbnailCard
                        Layout.fillWidth: true
                        index: recordRow.index
                        selected: recordRow.modelData.selected
                        Image {
                            anchors.fill: parent
                            source: recordRow.modelData.imageUrl
                            fillMode: Image.PreserveAspectFit
                            asynchronous: true
                        }
                        MouseArea {
                            parent: thumbnailCard
                            anchors.top: parent.top
                            anchors.left: parent.left
                            anchors.right: parent.right
                            height: VisionRTSize.ThumbnailHeight + 2 * VisionRTSize.Spacing
                            onClicked: root.controller.selectTrainingDraftRecord(recordRow.modelData.id)
                        }
                        WorkbenchActionButton {
                            parent: thumbnailCard
                            anchors.top: parent.top
                            anchors.right: parent.right
                            anchors.margins: VisionRTSize.Spacing
                            implicitWidth: VisionRTSize.SmallIconButtonSize
                            implicitHeight: VisionRTSize.SmallIconButtonSize
                            iconSize: VisionRTSize.SmallIconSize
                            normalColor: QuiColor.Transparent
                            hoverColor: QuiColor.Transparent
                            pressedColor: QuiColor.Transparent
                            iconColor: !enabled ? QuiColor.FontDark : hovered ? QuiColor.Highlight : QuiColor.FontPrimary
                            text: "删除训练记录"
                            iconSource: QuiFontIcon.ChromeClose
                            onClicked: root.controller.removeTrainingDraftRecord(recordRow.modelData.id)
                        }
                        details: Component {
                            ColumnLayout {
                                spacing: VisionRTSize.Spacing
                                Repeater {
                                    model: recordRow.modelData.fields
                                    delegate: TrainingValueEditor {
                                        required property var modelData
                                        Layout.fillWidth: true
                                        implicitHeight: VisionRTSize.SmallContentSize
                                        font: QuiFont.Caption
                                        field: modelData
                                        visible: !modelData.roi
                                        onCommitRequested: function(value) {
                                            root.controller.setTrainingField(recordRow.modelData.id, field.id, value);
                                        }
                                    }
                                }
                            }
                        }
                    }
                }
            }
        }
    }
    Rectangle {
        Layout.fillWidth: true
        Layout.preferredHeight: VisionRTSize.BarSize
        color: QuiColor.Button
        QuiButton {
            anchors.left: parent.left
            anchors.right: parent.right
            anchors.verticalCenter: parent.verticalCenter
            anchors.leftMargin: VisionRTSize.Spacing
            anchors.rightMargin: VisionRTSize.Spacing
            height: VisionRTSize.ButtonSize
            text: "应用训练数据"
            enabled: root.controller.trainingSavable && !root.controller.runtimeBusy && root.controller.workspaceEnabled
            onClicked: root.controller.applyTraining()
            Accessible.name: text
        }
    }
}
