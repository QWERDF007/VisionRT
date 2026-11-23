import QtQuick
import quickui
import VisionRT.Ui 1.0

Item {
    id: root
    required property int index
    property bool selected: false
    property Component details
    default property alias imageContent: imageContainer.data

    implicitWidth: VisionRTSize.ThumbnailWidth - 2 * VisionRTSize.Spacing
    implicitHeight: imageCard.height + detailsLoader.anchors.topMargin + detailsLoader.height + indexLabel.implicitHeight

    Rectangle {
        id: imageCard
        width: parent.width
        height: VisionRTSize.ThumbnailHeight + 2 * VisionRTSize.Spacing
        color: QuiColor.Background
        border.color: QuiColor.Highlight
        border.width: root.selected ? 2 : 0

        Item {
            id: imageContainer
            anchors.fill: parent
            anchors.margins: VisionRTSize.Spacing
        }
    }
    Loader {
        id: detailsLoader
        anchors.top: imageCard.bottom
        anchors.topMargin: implicitHeight > 0 ? VisionRTSize.Spacing : 0
        width: parent.width
        sourceComponent: root.details
    }
    QuiText {
        id: indexLabel
        anchors.top: detailsLoader.bottom
        anchors.horizontalCenter: parent.horizontalCenter
        text: root.index
        font: QuiFont.Caption
        color: QuiColor.FontPrimary
    }
}
