import QtQuick
import VisionRT.Ui 1.0
import quickui

QuiComboBox {
    id: control
    implicitHeight: VisionRTSize.ContentSize
    content.topPadding: 0
    content.bottomPadding: 0
    indicator: QuiTextIcon {
        x: control.mirrored ? control.padding : control.width - width - control.padding
        y: (control.height - height) / 2
        width: control.height
        height: control.height
        iconSource: QuiFontIcon.ChevronDown
        iconSize: VisionRTSize.IconSize
        opacity: control.enabled ? 1 : 0.3
    }
}
