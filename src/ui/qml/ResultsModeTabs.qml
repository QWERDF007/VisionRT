import QtQuick
import QtQuick.Controls
import quickui
import VisionRT.Style 1.0

Rectangle {
    id: root
    required property QtObject controller

    visible: root.controller.trainingAvailable
    implicitWidth: 34
    color: Theme.surfaceBg
    Rectangle {
        anchors.top: parent.top
        anchors.bottom: parent.bottom
        anchors.right: parent.right
        width: Theme.separatorWidth
        color: QuiColor.Border
    }
    Column {
        anchors.top: parent.top
        anchors.topMargin: 4
        anchors.horizontalCenter: parent.horizontalCenter
        spacing: 2
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
            radius: Theme.radiusSmall
            normalColor: "transparent"
            hoverColor: Theme.buttonHoverBg
            checkedColor: Theme.sourceButtonActiveBackground
            normalTextColor: Theme.textPrimary
            checkedTextColor: Theme.sourceButtonActiveIcon
            hoverTextColor: Theme.textPrimary
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
            radius: Theme.radiusSmall
            normalColor: "transparent"
            hoverColor: Theme.buttonHoverBg
            checkedColor: Theme.sourceButtonActiveBackground
            normalTextColor: Theme.textPrimary
            checkedTextColor: Theme.sourceButtonActiveIcon
            hoverTextColor: Theme.textPrimary
            checked: root.controller.trainingMode
            enabled: root.controller.workspaceEnabled
            onClicked: root.controller.trainingMode = true
            Accessible.name: "训练"
        }
    }
}
