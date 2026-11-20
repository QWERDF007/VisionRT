import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import quickui
import VisionRT.Ui 1.0

ColumnLayout {
    id: root
    required property QtObject controller
    spacing: 0
    ListView {
        id: trainingList
        objectName: "trainingRecords"
        Layout.fillWidth: true
        Layout.fillHeight: true
        enabled: root.controller.workspaceEnabled
        clip: true
        spacing: 5
        boundsBehavior: Flickable.StopAtBounds
        model: root.controller.trainingRecords
        ScrollBar.vertical: QuiScrollBar {
        }
        delegate: Rectangle {
            id: recordRow
            required property var modelData
            required property int index
            readonly property bool labelled: root.controller.trainingKind === TrainingKind.ColorSamples
            width: trainingList.width - 10
            x: 5
            height: content.implicitHeight + 10
            color: QuiColor.Primary
            border.color: modelData.selected ? QuiColor.Highlight : QuiColor.Border
            border.width: modelData.selected ? 2 : 1
            radius: 3
            ColumnLayout {
                id: content
                anchors.left: parent.left
                anchors.right: parent.right
                anchors.top: parent.top
                anchors.margins: 5
                spacing: 5
                Image {
                    Layout.fillWidth: true
                    Layout.preferredHeight: VisionRTSize.ThumbnailHeight
                    source: recordRow.modelData.imageUrl
                    fillMode: Image.PreserveAspectFit
                    asynchronous: true
                    MouseArea {
                        anchors.fill: parent
                        onClicked: root.controller.selectTrainingRecord(recordRow.modelData.id)
                    }
                }
                WorkbenchTextField {
                    Layout.fillWidth: true
                    visible: recordRow.labelled
                    text: recordRow.modelData.label
                    onEditingFinished: root.controller.setTrainingLabel(recordRow.modelData.id, text)
                    onActiveFocusChanged: if (activeFocus)
                        root.controller.selectTrainingRecord(recordRow.modelData.id)
                    Accessible.name: "颜色名称"
                }
                QuiText {
                    Layout.alignment: Qt.AlignHCenter
                    text: recordRow.index
                    font: QuiFont.Caption
                    color: QuiColor.FontCaption
                }
            }
            WorkbenchActionButton {
                anchors.top: parent.top
                anchors.right: parent.right
                anchors.margins: 2
                visible: recordRow.labelled
                text: "删除训练记录"
                iconSource: QuiFontIcon.Delete
                onClicked: root.controller.removeTrainingRecord(recordRow.modelData.id)
            }
        }
    }
    QuiButton {
        Layout.fillWidth: true
        text: "应用训练数据"
        enabled: root.controller.trainingSavable && !root.controller.runtimeBusy && root.controller.workspaceEnabled
        onClicked: root.controller.applyTraining()
        Accessible.name: text
    }
}
