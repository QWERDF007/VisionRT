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
            anchors.margins: 5
            clip: true
            spacing: 5
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
                color: modelData.selected ? ToolTreeStyle.treeItemSelectedBg : QuiColor.Button
                border.color: modelData.selected ? QuiColor.Highlight : QuiColor.Border
                border.width: modelData.selected ? 2 : 1
                radius: 3
                RowLayout {
                    anchors.fill: parent
                    anchors.margins: 5
                    spacing: 5
                    QuiText {
                        text: String(recordRow.index).padStart(2, "0")
                        font: QuiFont.Caption
                        color: QuiColor.FontDark
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
                        WorkbenchTextField {
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
                            color: QuiColor.FontPrimary
                        }
                        QuiText {
                            text: modelData.regions + (recordRow.labelled ? " 个区域" : " 条参考边")
                            font: QuiFont.Caption
                            color: QuiColor.FontDark
                        }
                    }
                    WorkbenchActionButton {
                        text: "删除训练记录"
                        iconSource: QuiFontIcon.Delete
                        implicitWidth: 20
                        implicitHeight: 20
                        normalColor: "transparent"
                        iconColor: hovered ? QuiColor.Error : QuiColor.FontDark
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
            color: QuiColor.FontDark
        }
    }
    Rectangle {
        Layout.fillWidth: true
        Layout.preferredHeight: applyTrainingButton.implicitHeight + 12
        color: QuiColor.Button
        QuiButton {
            id: applyTrainingButton
            anchors.left: parent.left
            anchors.right: parent.right
            anchors.top: parent.top
            anchors.margins: 5
            text: "应用训练数据"
            font: QuiFont.Body
            normalColor: QuiColor.Button
            hoverColor: QuiColor.Hovered
            pressedColor: QuiColor.ItemPress
            textColor: enabled ? QuiColor.Highlight : WorkbenchStyle.iconMuted
            opacity: 1
            enabled: root.controller.trainingSavable && !root.controller.runtimeBusy && root.controller.workspaceEnabled
            onClicked: root.controller.applyTraining()
            Accessible.name: "应用训练数据"
        }
    }
}
