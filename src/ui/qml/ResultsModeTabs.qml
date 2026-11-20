import QtQuick
import QtQuick.Controls
import quickui
import VisionRT.Ui 1.0

Rectangle {
    id: root
    required property QtObject controller

    visible: root.controller.trainingAvailable
    implicitWidth: VisionRTSize.BarSize
    color: QuiColor.Primary
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
            implicitWidth: VisionRTSize.BarSize
            implicitHeight: 2 * VisionRTSize.ContentSize
            padding: 0
            font: QuiFont.Body
            display: AbstractButton.TextOnly
            showIndicator: true
            radius: 3
            normalColor: QuiColor.Primary
            hoverColor: QuiColor.ItemHover
            checkedColor: QuiColor.Primary
            normalTextColor: QuiColor.FontPrimary
            checkedTextColor: QuiColor.FontPrimary
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
            implicitWidth: VisionRTSize.BarSize
            implicitHeight: 2 * VisionRTSize.ContentSize
            padding: 0
            font: QuiFont.Body
            display: AbstractButton.TextOnly
            showIndicator: true
            radius: 3
            normalColor: QuiColor.Primary
            hoverColor: QuiColor.ItemHover
            checkedColor: QuiColor.Primary
            normalTextColor: QuiColor.FontPrimary
            checkedTextColor: QuiColor.FontPrimary
            hoverTextColor: QuiColor.FontPrimary
            checked: root.controller.trainingMode
            enabled: root.controller.workspaceEnabled
            onClicked: root.controller.trainingMode = true
            Accessible.name: "训练"
        }
    }
}
