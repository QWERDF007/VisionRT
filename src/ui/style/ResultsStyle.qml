pragma Singleton
import QtQuick
import quickui

QtObject {
    readonly property color viewportBg: "#eaebef"
    readonly property color inputRoiColor: Qt.rgba(1, 0.8627450980392157, 0, 1)
    readonly property color outputRoiColor: Qt.rgba(0, 0.47058823529411764, 1, 1)
    readonly property color assistRoiColor: Qt.rgba(0, 0.7450980392156863, 0.7450980392156863, 1)
    readonly property int roiStrokeWidth: 2
    readonly property int roiHandleSize: 4
    readonly property int trainingPanelWidth: 220
    readonly property int thumbnailPanelWidth: 116
    readonly property int thumbnailRowHeight: 86
    readonly property int thumbnailImageHeight: 60
}
