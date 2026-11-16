import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import quickui
import VisionRT.View 1.0
import VisionRT.Style 1.0

ColumnLayout {
    id: root
    required property QtObject controller
    signal trainingMenuRequested

    spacing: 0
    Rectangle {
        Layout.fillWidth: true
        Layout.preferredHeight: Theme.tabBarHeight
        color: Theme.tabBarBg
        Rectangle {
            anchors.left: parent.left
            anchors.right: parent.right
            anchors.bottom: parent.bottom
            height: Theme.separatorWidth
            color: QuiColor.Border
        }
        QuiComboBox {
            id: outputChoice
            anchors.fill: parent
            anchors.margins: 4
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
            anchors.margins: 4
            clip: true
            spacing: 6
            boundsBehavior: Flickable.StopAtBounds
            boundsMovement: Flickable.StopAtBounds
            model: root.controller.imageCount
            currentIndex: root.controller.imageIndex
            ScrollBar.vertical: QuiScrollBar {
            }
            delegate: Rectangle {
                required property int index
                width: thumbnails.width - 8
                height: Theme.thumbnailRowHeight
                anchors.horizontalCenter: parent ? parent.horizontalCenter : undefined
                color: index === root.controller.imageIndex ? Theme.treeItemSelectedBg : Theme.surfaceBg
                border.color: index === root.controller.imageIndex ? Theme.highlight : QuiColor.Border
                border.width: index === root.controller.imageIndex ? 2 : 1
                radius: Theme.radiusSmall
                ImageViewport {
                    id: thumbnail
                    property int thumbnailIndex: index
                    anchors.top: parent.top
                    anchors.left: parent.left
                    anchors.right: parent.right
                    anchors.margins: 4
                    height: Theme.thumbnailImageHeight
                    readOnly: true
                    enabled: false
                    inputRoiColor: Theme.inputRoiColor
                    outputRoiColor: Theme.outputRoiColor
                    assistRoiColor: Theme.assistRoiColor
                    roiStrokeWidth: Theme.roiStrokeWidth
                    roiHandleSize: Theme.roiHandleSize
                    Component.onCompleted: root.controller.attachViewport(thumbnail)
                    onThumbnailIndexChanged: root.controller.attachViewport(thumbnail)
                }
                QuiText {
                    anchors.bottom: parent.bottom
                    anchors.bottomMargin: 3
                    anchors.horizontalCenter: parent.horizontalCenter
                    text: index
                    font: QuiFont.Caption
                    color: Theme.textPrimary
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
        Layout.preferredHeight: 36
        visible: root.controller.trainingAvailable
        color: Theme.surfaceBg
        QuiButton {
            anchors.fill: parent
            anchors.margins: 4
            text: "加入训练"
            font: Theme.parameterFont
            normalColor: Theme.surfaceBg
            hoverColor: Theme.buttonHoverBg
            pressedColor: Theme.buttonPressedBg
            textColor: enabled ? Theme.highlight : Theme.disabledText
            opacity: 1
            enabled: root.controller.canAddTrainingImage
            onClicked: root.controller.addTrainingImage()
            Accessible.name: "加入训练"
        }
    }
}
