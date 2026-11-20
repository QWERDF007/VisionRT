import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import quickui
import VisionRT.Ui 1.0

Control {
    id: root

    required property real labelWidth
    property alias label: labelText.text
    readonly property real implicitLabelWidth: labelText.implicitWidth
    default property alias editorContent: editorLayout.data

    Layout.fillWidth: true
    implicitWidth: implicitContentWidth
    implicitHeight: Math.max(VisionRTSize.ContentSize, implicitContentHeight)
    padding: 0

    contentItem: RowLayout {
        spacing: 5

        QuiText {
            id: labelText
            Layout.preferredWidth: root.labelWidth
            Layout.minimumWidth: root.labelWidth
            Layout.maximumWidth: root.labelWidth
            horizontalAlignment: Text.AlignRight
        }

        RowLayout {
            id: editorLayout
            Layout.fillWidth: true
            Layout.maximumWidth: Number.POSITIVE_INFINITY
            spacing: 0
        }
    }
}
