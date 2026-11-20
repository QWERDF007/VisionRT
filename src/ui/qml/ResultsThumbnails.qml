import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import quickui
import VisionRT.View 1.0
import VisionRT.Ui 1.0

ColumnLayout {
    id: root
    required property QtObject controller
    signal trainingMenuRequested

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
            anchors.left: parent.left
            anchors.right: parent.right
            anchors.verticalCenter: parent.verticalCenter
            anchors.leftMargin: 5
            anchors.rightMargin: 5
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
            anchors.margins: 5
            clip: true
            spacing: 5
            boundsBehavior: Flickable.StopAtBounds
            boundsMovement: Flickable.StopAtBounds
            model: root.controller.imageCount
            currentIndex: root.controller.imageIndex
            ScrollBar.vertical: QuiScrollBar {
            }
            delegate: Item {
                required property int index
                width: thumbnails.width - 8
                height: imageCard.height + indexLabel.implicitHeight
                anchors.horizontalCenter: parent ? parent.horizontalCenter : undefined
                Rectangle {
                    id: imageCard
                    width: parent.width
                    height: thumbnail.height + 2 * thumbnail.anchors.margins
                    color: QuiColor.Background
                    border.color: QuiColor.Highlight
                    border.width: index === root.controller.imageIndex ? 2 : 0
                    ImageViewport {
                        id: thumbnail
                        property int thumbnailIndex: index
                        anchors.top: parent.top
                        anchors.left: parent.left
                        anchors.right: parent.right
                        anchors.margins: 5
                        height: VisionRTSize.ThumbnailHeight
                        readOnly: true
                        enabled: false
                        inputRoiColor: VisionRTColor.InputROI
                        outputRoiColor: VisionRTColor.OutputROI
                        assistRoiColor: VisionRTColor.InputROI
                        roiStrokeWidth: 2
                        roiHandleSize: 4
                        Component.onCompleted: root.controller.attachViewport(thumbnail)
                        onThumbnailIndexChanged: root.controller.attachViewport(thumbnail)
                    }
                }
                QuiText {
                    id: indexLabel
                    anchors.top: imageCard.bottom
                    anchors.horizontalCenter: parent.horizontalCenter
                    text: index
                    font: QuiFont.Caption
                    color: QuiColor.FontPrimary
                }
                MouseArea {
                    anchors.fill: parent
                    enabled: root.controller.workspaceEnabled
                    onClicked: root.controller.setImageIndex(index)
                }
                TapHandler {
                    acceptedButtons: Qt.RightButton
                    onTapped: {
                        root.controller.setImageIndex(index);
                        if (root.controller.canAddTrainingImage)
                            root.trainingMenuRequested();
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
            anchors.leftMargin: 5
            anchors.rightMargin: 5
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
