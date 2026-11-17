import QtQuick
import QtQuick.Controls
import quickui
QuiTextIconButton {
 implicitHeight: display === AbstractButton.IconOnly ? 28 : 32
 implicitWidth: display === AbstractButton.IconOnly ? implicitHeight : implicitContentWidth + leftPadding + rightPadding
 horizontalPadding: display === AbstractButton.IconOnly ? 0 : 5
 verticalPadding: 0
 radius: display === AbstractButton.IconOnly ? 3 : 4
 normalColor: QuiColor.Transparent
 hoverColor: QuiColor.Hovered
}
