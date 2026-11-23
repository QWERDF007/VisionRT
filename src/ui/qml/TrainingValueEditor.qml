import QtQuick
import QtQuick.Layouts
import quickui
import VisionRT.Ui 1.0

Item {
    id: root
    required property var field
    property font font: QuiFont.Body
    signal commitRequested(var value)
    implicitHeight: VisionRTSize.ContentSize

    WorkbenchTextField {
        anchors.fill: parent
        font: root.font
        visible: root.field.editor !== EditorKind.Switch && root.field.editor !== EditorKind.Bool
        text: root.field.value === undefined ? "" : String(root.field.value)
        Accessible.name: root.field.name
        onEditingFinished: root.commitRequested(text)
    }
    QuiToggleSwitch {
        anchors.left: parent.left
        anchors.verticalCenter: parent.verticalCenter
        visible: root.field.editor === EditorKind.Switch || root.field.editor === EditorKind.Bool
        checked: !!root.field.value
        Accessible.name: root.field.name
        clickListener: function () { root.commitRequested(!root.field.value); }
    }
}
