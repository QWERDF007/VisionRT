import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import quickui
import VisionRT.Ui 1.0

Control {
    id: root
    property alias label: labelText.text
    readonly property real editorX: editorHost.x
    default property alias editorContent: editorHost.data
    implicitWidth: contentItem.implicitWidth
    implicitHeight: VisionRTSize.ContentSize
    padding: 0
    font: QuiFont.Body

    contentItem: RowLayout {
        spacing: 5
        QuiText {
            id: labelText
            Layout.preferredWidth: VisionRTSize.ParamLabelWidth
            Layout.fillHeight: true
            horizontalAlignment: Text.AlignLeft
            verticalAlignment: Text.AlignVCenter
            font: root.font
        }
        Item {
            id: editorHost
            Layout.preferredWidth: 420
            Layout.fillHeight: true
        }
        Item {
            Layout.fillWidth: true
        }
    }
}
