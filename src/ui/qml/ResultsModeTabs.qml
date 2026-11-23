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
        width: parent.width
        Repeater {
            model: ["处理", "训练"]
            WorkbenchTabButton {
                required property string modelData
                required property int index
                objectName: index === 0 ? "trainingProcessTab" : "trainingTrainTab"
                text: modelData
                orientation: Qt.Vertical
                width: root.width
                implicitHeight: 2 * VisionRTSize.ContentSize
                padding: 0
                display: AbstractButton.TextOnly
                checked: root.controller.trainingMode === (index === 1)
                enabled: root.controller.workspaceEnabled
                onClicked: root.controller.trainingMode = index === 1
                Accessible.name: text
            }
        }
    }
}
