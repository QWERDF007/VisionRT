import QtQuick
import quickui
QuiTextBoxBackground {
 id: control
 Rectangle {
  anchors.fill: parent
  z: 10
  color: QuiColor.Transparent
  radius: control.radius
  border.width: control.border.width
  border.color: control.border.color
 }
}
