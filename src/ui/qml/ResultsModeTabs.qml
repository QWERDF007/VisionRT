import QtQuick
import QtQuick.Controls
import quickui
import VisionRT.Style 1.0

Rectangle {
    id: root
    required property QtObject controller

    visible: root.controller.trainingAvailable
    implicitWidth: 34
    color: QuiColor.Button
    Rectangle {
        anchors.top: parent.top
        anchors.bottom: parent.bottom
        anchors.right: parent.right
        width: 1
        color: QuiColor.Border
    }
    Column {
        anchors.top: parent.top
        anchors.topMargin: 5
        anchors.horizontalCenter: parent.horizontalCenter
        spacing: 5
        QuiTabButton {
            objectName: "trainingProcessTab"
            text: "处理"
            verticalText: true
            implicitWidth: 30
            implicitHeight: 56
            padding: 0
            font: Qt.font({
                pixelSize: 12
            })
            display: AbstractButton.TextOnly
            showIndicator: false
            radius: 3
            normalColor: "transparent"
            hoverColor: QuiColor.Hovered
            checkedColor: QuiColor.Highlight
            normalTextColor: QuiColor.FontPrimary
            checkedTextColor: QuiColor.White
            hoverTextColor: QuiColor.FontPrimary
            checked: !root.controller.trainingMode
            enabled: root.controller.workspaceEnabled
            onClicked: root.controller.trainingMode = false
            Accessible.name: "处理"
        }
        QuiTabButton {
            objectName: "trainingTrainTab"
            text: "训练"
            verticalText: true
            implicitWidth: 30
            implicitHeight: 56
            padding: 0
            font: Qt.font({
                pixelSize: 12
            })
            display: AbstractButton.TextOnly
            showIndicator: false
            radius: 3
            normalColor: "transparent"
            hoverColor: QuiColor.Hovered
            checkedColor: QuiColor.Highlight
            normalTextColor: QuiColor.FontPrimary
            checkedTextColor: QuiColor.White
            hoverTextColor: QuiColor.FontPrimary
            checked: root.controller.trainingMode
            enabled: root.controller.workspaceEnabled
            onClicked: root.controller.trainingMode = true
            Accessible.name: "训练"
        }
    }
}
