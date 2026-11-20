import QtQuick
import VisionRT.Ui 1.0
import quickui

QuiTextField {
    id: control
    implicitHeight: VisionRTSize.ContentSize
    leftPadding: 5
    rightPadding: 5
    topPadding: 0
    bottomPadding: 0
    background: ParameterFieldFrame {
        inputItem: control
        hovered: control.hovered
    }
}
