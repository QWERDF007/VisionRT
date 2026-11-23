import QtQuick
import VisionRT.Ui 1.0
import QtQuick.Controls
import quickui

QuiTextIconButton {
    iconSize: VisionRTSize.IconSize
    implicitHeight: display === AbstractButton.IconOnly ? VisionRTSize.IconButtonSize : VisionRTSize.ButtonSize
    implicitWidth: display === AbstractButton.IconOnly ? implicitHeight : implicitContentWidth + leftPadding + rightPadding
    horizontalPadding: display === AbstractButton.IconOnly ? 0 : 5
    verticalPadding: 0
    radius: display === AbstractButton.IconOnly ? 3 : 4
}
