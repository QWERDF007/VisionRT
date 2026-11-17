import QtQuick
import quickui
QuiTextField {
 id: control
 implicitHeight: 32
 leftPadding: 5
 rightPadding: 5
 topPadding: 0
 bottomPadding: 0
 background: ParameterFieldFrame { inputItem: control; hovered: control.hovered }
}
