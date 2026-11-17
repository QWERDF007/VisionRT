import QtQuick
import quickui

Rectangle {
    property int orientation: Qt.Vertical
    implicitWidth: orientation === Qt.Vertical ? 2 : 32
    implicitHeight: orientation === Qt.Vertical ? 32 : 2
    color: QuiColor.Border
}
