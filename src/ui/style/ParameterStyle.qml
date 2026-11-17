pragma Singleton
import QtQuick
import quickui

QtObject {
    readonly property int segmentButtonWidth: 92
    readonly property color paramActionHoverBg: Qt.alpha(QuiColor.Highlight, 0.15)
    readonly property int parameterLabelWidth: 96
    readonly property real sourceButtonDisabledOpacity: 0.45
    readonly property color paramActionActiveBg: Qt.alpha(QuiColor.Highlight, 0.1)
    readonly property int cardLabelWidth: 64
}
