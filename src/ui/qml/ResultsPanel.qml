import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import quickui
import VisionRT.View 1.0
import VisionRT.Style 1.0

Rectangle {
    id: root
    color: Theme.windowBg
    border.color: Theme.border
    border.width: 1
    signal roiRequested()

    RowLayout {
        anchors.fill: parent; anchors.margins: 1; spacing: 0

        // Column 1: Thumbnail column with top dropdown
        Rectangle {
            Layout.preferredWidth: Theme.thumbnailPanelWidth
            Layout.fillHeight: true
            color: Theme.surfaceBg
            Rectangle {
                anchors.top: parent.top; anchors.bottom: parent.bottom; anchors.right: parent.right
                width: 1; color: Theme.border
            }
            ColumnLayout {
                anchors.fill: parent; spacing: 0
                Rectangle {
                    Layout.fillWidth: true; Layout.preferredHeight: Theme.tabBarHeight; color: Theme.tabBarBg
                    Rectangle {
                        anchors.left: parent.left; anchors.right: parent.right; anchors.bottom: parent.bottom
                        height: 1; color: Theme.tabBorder
                    }
                    QuiComboBox {
                        id: outputChoice
                        anchors.fill: parent; anchors.margins: 4
                        model: app.imageOutputs; textRole: "name"; valueRole: "id"
                        currentIndex: { for (let i = 0; i < app.imageOutputs.length; ++i) if (app.imageOutputs[i].id === app.selectedImageOutput) return i; return -1 }
                        displayText: currentIndex < 0 ? (app.imageOutputs.length > 0 ? "图像输出" : "") : (currentText === "images" ? "图像输出" : currentText)
                        onActivated: app.setImageOutput(currentValue)
                        Accessible.name: "图像输出"
                    }
                }
                Item {
                    Layout.fillWidth: true; Layout.fillHeight: true
                    ListView {
                        id: thumbnails
                        visible: app.imageCount > 0
                        anchors.fill: parent; anchors.margins: 4; clip: true; spacing: 6
                        boundsBehavior: Flickable.StopAtBounds
                        boundsMovement: Flickable.StopAtBounds
                        model: app.imageCount
                        currentIndex: app.imageIndex
                        ScrollBar.vertical: QuiScrollBar {}
                        delegate: Rectangle {
                            required property int index
                            width: thumbnails.width - 8; height: Theme.thumbnailRowHeight
                            anchors.horizontalCenter: parent ? parent.horizontalCenter : undefined
                            color: index === app.imageIndex ? Theme.treeItemSelectedBg : Theme.surfaceBg
                            border.color: index === app.imageIndex ? Theme.highlight : Theme.border
                            border.width: index === app.imageIndex ? 2 : 1
                            radius: Theme.radiusSmall
                            ImageViewport {
                                id: thumbnail
                                property int thumbnailIndex: index
                                anchors.top: parent.top; anchors.left: parent.left; anchors.right: parent.right
                                anchors.margins: 4; height: Theme.thumbnailImageHeight
                                readOnly: true; enabled: false
                                inputRoiColor: Theme.inputRoiColor
                                outputRoiColor: Theme.outputRoiColor
                                assistRoiColor: Theme.assistRoiColor
                                roiStrokeWidth: Theme.roiStrokeWidth
                                roiHandleSize: Theme.roiHandleSize
                                Component.onCompleted: app.attachViewport(thumbnail)
                                onThumbnailIndexChanged: app.attachViewport(thumbnail)
                            }
                            QuiText { anchors.bottom: parent.bottom; anchors.bottomMargin: 3; anchors.horizontalCenter: parent.horizontalCenter; text: index; font: QuiFont.Caption; color: Theme.textPrimary }
                            MouseArea { anchors.fill: parent; onClicked: app.setImageIndex(index) }
                        }
                    }
                }
            }
        }

        // Column 2: Vertical toolbar
        Rectangle {
            Layout.preferredWidth: Theme.toolbarWidth
            Layout.fillHeight: true
            color: Theme.surfaceBg
            Rectangle {
                anchors.top: parent.top; anchors.bottom: parent.bottom; anchors.right: parent.right
                width: 1; color: Theme.border
            }
            ColumnLayout {
                anchors.top: parent.top; anchors.topMargin: 4
                anchors.horizontalCenter: parent.horizontalCenter
                spacing: 4
                QuiTextIconButton {
                    iconSource: Theme.iconFitWindow
                    implicitWidth: Theme.toolbarButtonSize; implicitHeight: Theme.toolbarButtonSize
                    display: AbstractButton.IconOnly
                    ToolTip.visible: hovered; ToolTip.text: "适应窗口"
                    onClicked: viewport.fitContain()
                }
                QuiTextIconButton {
                    iconSource: Theme.iconZoomIn
                    implicitWidth: Theme.toolbarButtonSize; implicitHeight: Theme.toolbarButtonSize
                    display: AbstractButton.IconOnly
                    ToolTip.visible: hovered; ToolTip.text: "放大"
                    onClicked: viewport.zoomBy(1.25)
                }
                QuiTextIconButton {
                    iconSource: Theme.iconZoomOut
                    implicitWidth: Theme.toolbarButtonSize; implicitHeight: Theme.toolbarButtonSize
                    display: AbstractButton.IconOnly
                    ToolTip.visible: hovered; ToolTip.text: "缩小"
                    onClicked: viewport.zoomBy(0.8)
                }
                Rectangle {
                    Layout.preferredWidth: 20; Layout.preferredHeight: 1; color: Theme.border
                    Layout.alignment: Qt.AlignHCenter
                }
                // Smart point selection: the button reports the user preference only. Availability
                // and pausing are explained in the tooltip, never by silently unchecking it.
                QuiTextIconButton {
                    objectName: "easyTouchToggle"
                    iconSource: Theme.iconEasyTouch
                    implicitWidth: Theme.toolbarButtonSize
                    implicitHeight: Theme.toolbarButtonSize
                    display: AbstractButton.IconOnly
                    radius: Theme.radiusSmall
                    checkable: false
                    checked: app.easyTouchEnabled
                    normalColor: checked ? Theme.sourceButtonActiveBackground : Theme.surfaceBg
                    hoverColor: checked ? Theme.sourceButtonActiveBackground : Theme.menuButtonHoverBg
                    pressedColor: checked ? Theme.sourceButtonActiveBackground : Theme.menuButtonPressedBg
                    iconColor: checked ? Theme.sourceButtonActiveIcon : Theme.iconMuted
                    Accessible.name: "智能点选（EasyTouch）"
                    Accessible.role: Accessible.CheckBox
                    Accessible.checkable: true
                    Accessible.checked: app.easyTouchEnabled
                    ToolTip.visible: hovered
                    ToolTip.text: app.easyTouchEnabled
                                  ? (app.assistAvailable ? "智能点选（EasyTouch）：已开启"
                                                         : "智能点选（EasyTouch）：已开启，" + (app.assistDisabledReason || "当前不可用"))
                                  : "智能点选（EasyTouch）：已关闭"
                    ToolTip.delay: 300
                    onClicked: app.setEasyTouchEnabled(!app.easyTouchEnabled)
                }
                Rectangle {
                    Layout.preferredWidth: 20; Layout.preferredHeight: 1; color: Theme.border
                    Layout.alignment: Qt.AlignHCenter
                }
                Rectangle {
                    Layout.preferredWidth: Theme.toolbarButtonSize; Layout.preferredHeight: 22
                    radius: Theme.radiusSmall
                    color: roiMouse.containsMouse ? Theme.menuButtonHoverBg : Theme.surfaceBg
                    border.color: Theme.border; border.width: 1
                    enabled: !app.runtimeBusy && app.selectedNode.length > 0
                    opacity: enabled ? 1.0 : 0.5
                    QuiText {
                        anchors.centerIn: parent
                        text: "ROI"
                        font: Qt.font({ family: Theme.bodyFont.family, pixelSize: Theme.fontSizeCaption, bold: true })
                        color: Theme.highlight
                    }
                    MouseArea {
                        id: roiMouse; anchors.fill: parent; hoverEnabled: true
                        cursorShape: Qt.PointingHandCursor
                        onClicked: root.roiRequested()
                    }
                    Accessible.name: "编辑输入 ROI"
                }
                QuiTextIconButton {
                    iconSource: Theme.iconOneToOne
                    implicitWidth: Theme.toolbarButtonSize; implicitHeight: Theme.toolbarButtonSize
                    display: AbstractButton.IconOnly
                    ToolTip.visible: hovered; ToolTip.text: "1:1 像素显示"
                    onClicked: viewport.fitOneToOne()
                }
            }
        }

        // Column 3: Viewport area
        ColumnLayout {
            Layout.fillWidth: true
            Layout.fillHeight: true
            spacing: 0

            Rectangle {
                Layout.fillWidth: true; Layout.fillHeight: true; color: Theme.viewportBg
                ImageViewport {
                    id: viewport; objectName: "imageViewport"
                    anchors.fill: parent; readOnly: app.runtimeBusy
                    inputRoiColor: Theme.inputRoiColor
                    outputRoiColor: Theme.outputRoiColor
                    assistRoiColor: Theme.assistRoiColor
                    roiStrokeWidth: Theme.roiStrokeWidth
                    roiHandleSize: Theme.roiHandleSize
                    Component.onCompleted: app.attachViewport(viewport)
                    onAssistHover: function(point) { app.assistHover(point.x, point.y) }
                    onAssistLeave: app.assistCancel()
                    onAssistClicked: function(point, append) { app.assistClick(point.x, point.y, append) }
                    onAssistCanceled: app.assistCancel()
                }
                QuiText {
                    anchors.top: parent.top; anchors.left: parent.left; anchors.margins: 8
                    text: app.assistHint
                    visible: app.assistActive && text.length > 0
                    font: Theme.captionFont
                    color: Theme.assistHintText
                }
                QuiText {
                    anchors.bottom: parent.bottom; anchors.right: parent.right; anchors.margins: 8
                    text: viewport.hasImage ? Math.round(viewport.zoom * 100) + "%" : ""
                    font: Theme.captionFont; color: Theme.zoomText
                }
                QuiText {
                    anchors.bottom: parent.bottom; anchors.left: parent.left; anchors.right: parent.right; anchors.margins: 8
                    text: viewport.statusText; visible: text.length > 0
                    wrapMode: Text.Wrap; color: Theme.warning
                }
            }
        }
    }
}
