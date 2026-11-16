import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import quickui
import VisionRT.Style 1.0

ColumnLayout {
    id: root
    required property QtObject controller

    spacing: 0
    Item {
        Layout.fillWidth: true
        Layout.fillHeight: true
        ListView {
            id: trainingList
            objectName: "trainingRecords"
            enabled: root.controller.workspaceEnabled
            anchors.fill: parent
            anchors.margins: 4
            clip: true
            spacing: 4
            boundsBehavior: Flickable.StopAtBounds
            boundsMovement: Flickable.StopAtBounds
            model: root.controller.trainingRecords
            ScrollBar.vertical: QuiScrollBar {
            }
            delegate: Rectangle {
                id: recordRow
                required property var modelData
                required property int index
                readonly property bool labelled: root.controller.trainingKind === "color_samples"
                width: trainingList.width - 8
                height: 58
                anchors.horizontalCenter: parent ? parent.horizontalCenter : undefined
                color: modelData.selected ? Theme.treeItemSelectedBg : Theme.surfaceBg
                border.color: modelData.selected ? Theme.highlight : QuiColor.Border
                border.width: modelData.selected ? 2 : 1
                radius: Theme.radiusSmall
                RowLayout {
                    anchors.fill: parent
                    anchors.margins: 4
                    spacing: 4
                    QuiText {
                        text: String(recordRow.index).padStart(2, "0")
                        font: QuiFont.Caption
                        color: Theme.textSecondary
                    }
                    Item {
                        Layout.preferredWidth: 44
                        Layout.fillHeight: true
                        Image {
                            anchors.fill: parent
                            source: modelData.imageUrl
                            fillMode: Image.PreserveAspectFit
                            asynchronous: true
                            sourceSize.width: 88
                            sourceSize.height: 88
                        }
                        MouseArea {
                            anchors.fill: parent
                            onClicked: root.controller.selectTrainingRecord(modelData.id)
                        }
                    }
                    ColumnLayout {
                        Layout.fillWidth: true
                        spacing: 0
                        QuiTextField {
                            Layout.fillWidth: true
                            visible: recordRow.labelled
                            text: modelData.label
                            font: QuiFont.Caption
                            onEditingFinished: root.controller.setTrainingLabel(modelData.id, text)
                            onActiveFocusChanged: if (activeFocus)
                                root.controller.selectTrainingRecord(modelData.id)
                            Accessible.name: "颜色名称"
                        }
                        QuiText {
                            Layout.fillWidth: true
                            visible: !recordRow.labelled
                            text: modelData.label
                            elide: Text.ElideRight
                            font: QuiFont.Caption
                            color: Theme.textPrimary
                        }
                        QuiText {
                            text: modelData.regions + (recordRow.labelled ? " 个区域" : " 条参考边")
                            font: QuiFont.Caption
                            color: Theme.textSecondary
                        }
                    }
                    QuiTextIconButton {
                        text: "删除训练记录"
                        iconSource: QuiFontIcon.Delete
                        iconSize: Theme.iconSizeSmall
                        implicitWidth: 20
                        implicitHeight: 20
                        display: AbstractButton.IconOnly
                        normalColor: "transparent"
                        hoverColor: Theme.buttonHoverBg
                        iconColor: hovered ? Theme.error : Theme.textSecondary
                        onClicked: root.controller.removeTrainingRecord(modelData.id)
                        Accessible.name: "删除训练记录"
                    }
                }
            }
        }
        QuiText {
            anchors.centerIn: parent
            visible: trainingList.count === 0
            text: "训练集为空"
            font: QuiFont.Body
            color: Theme.textSecondary
        }
    }
    Rectangle {
        Layout.fillWidth: true
        Layout.preferredHeight: applyTrainingButton.implicitHeight + 12
        color: Theme.surfaceBg
        QuiButton {
            id: applyTrainingButton
            anchors.left: parent.left
            anchors.right: parent.right
            anchors.top: parent.top
            anchors.margins: 6
            text: "应用训练数据"
            font: Theme.parameterFont
            normalColor: Theme.surfaceBg
            hoverColor: Theme.buttonHoverBg
            pressedColor: Theme.buttonPressedBg
            textColor: enabled ? Theme.highlight : Theme.disabledText
            opacity: 1
            enabled: root.controller.trainingSavable && !root.controller.runtimeBusy && root.controller.workspaceEnabled
            onClicked: root.controller.applyTraining()
            Accessible.name: "应用训练数据"
        }
    }
}
