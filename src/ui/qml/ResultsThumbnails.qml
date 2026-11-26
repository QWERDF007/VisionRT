import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import quickui
import VisionRT.Ui 1.0

ColumnLayout {
    id: root
    required property QtObject controller
    signal trainingMenuRequested
    readonly property bool _providersReady: root.controller.ensureImageProviders(root)

    spacing: 0
    Rectangle {
        Layout.fillWidth: true
        Layout.preferredHeight: VisionRTSize.BarSize
        color: QuiColor.Primary
        Rectangle {
            anchors.left: parent.left
            anchors.right: parent.right
            anchors.bottom: parent.bottom
            height: 1
            color: QuiColor.Border
        }
        QuiComboBox {
            id: outputChoice
            indicatorIconSize: VisionRTSize.IconSize
            anchors.left: parent.left
            anchors.right: parent.right
            anchors.verticalCenter: parent.verticalCenter
            anchors.leftMargin: VisionRTSize.Spacing
            anchors.rightMargin: VisionRTSize.Spacing
            height: VisionRTSize.ContentSize
            model: root.controller.imageOutputs
            textRole: "name"
            valueRole: "id"
            enabled: root.controller.workspaceEnabled
            currentIndex: {
                for (let i = 0; i < root.controller.imageOutputs.length; ++i)
                    if (root.controller.imageOutputs[i].id === root.controller.selectedImageOutput)
                        return i;
                return -1;
            }
            displayText: currentIndex < 0 ? (root.controller.imageOutputs.length > 0 ? "选择图像" : "") : currentText
            onActivated: root.controller.setImageOutput(currentValue)
            Accessible.name: "图像输出"
        }
    }
    Item {
        Layout.fillWidth: true
        Layout.fillHeight: true
        ListView {
            id: thumbnails
            visible: root.controller.imageCount > 0
            anchors.fill: parent
            anchors.topMargin: VisionRTSize.Spacing
            anchors.bottomMargin: VisionRTSize.Spacing
            clip: true
            spacing: VisionRTSize.Spacing
            boundsBehavior: Flickable.StopAtBounds
            boundsMovement: Flickable.StopAtBounds
            model: root.controller.imageCount
            currentIndex: root.controller.imageIndex
            ScrollBar.vertical: WorkbenchScrollBar {
                id: thumbnailScrollBar
                parent: thumbnails.parent
                anchors.top: parent.top
                anchors.bottom: parent.bottom
                anchors.right: parent.right
            }
            delegate: Item {
                id: thumbnailRow
                required property int index
                width: thumbnails.width
                height: thumbnailCard.implicitHeight
                ResultsThumbnailCard {
                    id: thumbnailCard
                    index: thumbnailRow.index
                    x: VisionRTSize.ThumbnailHorizontalMargin
                    width: thumbnails.width - 2 * VisionRTSize.ThumbnailHorizontalMargin
                    height: implicitHeight
                    selected: index === root.controller.imageIndex
                    Image {
                        id: thumbnail
                        anchors.fill: parent
                        fillMode: Image.PreserveAspectFit
                        asynchronous: true
                        cache: false
                        source: root._providersReady && root.controller.resultThumbnailVersion > 0
                                ? root.controller.resultThumbnailUrl(thumbnailCard.index)
                                : ""
                    }
                    MouseArea {
                        parent: thumbnailCard
                        anchors.fill: parent
                        enabled: root.controller.workspaceEnabled
                        onClicked: root.controller.setImageIndex(thumbnailCard.index)
                    }
                    TapHandler {
                        parent: thumbnailCard
                        acceptedButtons: Qt.RightButton
                        onTapped: {
                            root.controller.setImageIndex(thumbnailCard.index);
                            if (root.controller.canAddTrainingImage)
                                root.trainingMenuRequested();
                        }
                    }
                }
            }
        }
    }
    Rectangle {
        Layout.fillWidth: true
        Layout.preferredHeight: VisionRTSize.BarSize
        visible: root.controller.trainingAvailable
        color: QuiColor.Button
        QuiButton {
            anchors.left: parent.left
            anchors.right: parent.right
            anchors.verticalCenter: parent.verticalCenter
            anchors.leftMargin: VisionRTSize.Spacing
            anchors.rightMargin: VisionRTSize.Spacing
            height: VisionRTSize.ButtonSize
            text: "加入训练"
            font: QuiFont.Body
            normalColor: QuiColor.Button
            hoverColor: QuiColor.Hovered
            pressedColor: QuiColor.ItemPress
            textColor: enabled ? QuiColor.Highlight : QuiColor.FontCaption
            opacity: 1
            enabled: root.controller.canAddTrainingImage
            onClicked: root.controller.addTrainingImage()
            Accessible.name: "加入训练"
        }
    }
}
