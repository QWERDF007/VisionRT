import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import quickui
import VisionRT.View 1.0
import VisionRT.Style 1.0

Rectangle {
    id: root
    color: Theme.windowBg
    border.color: QuiColor.Border
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
                width: Theme.separatorWidth; color: QuiColor.Border
            }
            ColumnLayout {
                anchors.fill: parent; spacing: 0
                Rectangle {
                    Layout.fillWidth: true; Layout.preferredHeight: Theme.tabBarHeight; color: Theme.tabBarBg
                    Rectangle {
                        anchors.left: parent.left; anchors.right: parent.right; anchors.bottom: parent.bottom
                        height: Theme.separatorWidth; color: QuiColor.Border
                    }
                    QuiComboBox {
                        id: outputChoice
                        anchors.fill: parent; anchors.margins: 4
                        model: app.imageOutputs; textRole: "name"; valueRole: "id"
                        enabled: app.workspaceEnabled
                        currentIndex: { for (let i = 0; i < app.imageOutputs.length; ++i) if (app.imageOutputs[i].id === app.selectedImageOutput) return i; return -1 }
                        displayText: currentIndex < 0 ? (app.imageOutputs.length > 0 ? "选择图像" : "") : currentText
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
                            border.color: index === app.imageIndex ? Theme.highlight : QuiColor.Border
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
                            MouseArea { anchors.fill: parent; enabled: app.workspaceEnabled; onClicked: app.setImageIndex(index) }
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
                width: Theme.separatorWidth; color: QuiColor.Border
            }
            ColumnLayout {
                anchors.top: parent.top; anchors.topMargin: 4
                anchors.horizontalCenter: parent.horizontalCenter
                spacing: 4
                // Every button here changes only the view, but a run owns all user interaction.
                enabled: app.workspaceEnabled
                QuiTextIconButton {
                    text: "适应窗口"
                    iconSource: QuiFontIcon.FitPage
                    implicitWidth: Theme.toolbarButtonSize; implicitHeight: Theme.toolbarButtonSize
                    radius: Theme.radiusSmall
                    display: AbstractButton.IconOnly
                    normalColor: Theme.menuButtonBg
                    hoverColor: Theme.buttonHoverBg
                    pressedColor: Theme.buttonPressedBg
                    iconColor: hovered ? Theme.highlight : Theme.menuButtonIcon
                    onClicked: viewport.fitContain()
                }
                QuiTextIconButton {
                    text: "放大"
                    iconSource: Theme.iconZoomIn
                    implicitWidth: Theme.toolbarButtonSize; implicitHeight: Theme.toolbarButtonSize
                    radius: Theme.radiusSmall
                    display: AbstractButton.IconOnly
                    normalColor: Theme.menuButtonBg
                    hoverColor: Theme.buttonHoverBg
                    pressedColor: Theme.buttonPressedBg
                    iconColor: hovered ? Theme.highlight : Theme.menuButtonIcon
                    onClicked: viewport.zoomBy(1.25)
                }
                QuiTextIconButton {
                    text: "缩小"
                    iconSource: Theme.iconZoomOut
                    implicitWidth: Theme.toolbarButtonSize; implicitHeight: Theme.toolbarButtonSize
                    radius: Theme.radiusSmall
                    display: AbstractButton.IconOnly
                    normalColor: Theme.menuButtonBg
                    hoverColor: Theme.buttonHoverBg
                    pressedColor: Theme.buttonPressedBg
                    iconColor: hovered ? Theme.highlight : Theme.menuButtonIcon
                    onClicked: viewport.zoomBy(0.8)
                }
                Rectangle {
                    Layout.preferredWidth: 20; Layout.preferredHeight: Theme.separatorWidth; color: QuiColor.Border
                    Layout.alignment: Qt.AlignHCenter
                }
                // Smart point selection: the button reports the user preference only. Availability
                // and pausing are explained in the tooltip, never by silently unchecking it.
                QuiTextIconButton {
                    objectName: "easyTouchToggle"
                    text: app.easyTouchEnabled
                          ? (app.assistAvailable ? "智能点选（EasyTouch）：已开启"
                                                 : "智能点选（EasyTouch）：已开启，" + (app.assistDisabledReason || "当前不可用"))
                          : "智能点选（EasyTouch）：已关闭"
                    iconSource: Theme.iconEasyTouch
                    implicitWidth: Theme.toolbarButtonSize
                    implicitHeight: Theme.toolbarButtonSize
                    display: AbstractButton.IconOnly
                    radius: Theme.radiusSmall
                    checkable: false
                    checked: app.easyTouchEnabled
                    normalColor: checked ? Theme.sourceButtonActiveBackground : Theme.menuButtonBg
                    hoverColor: checked ? Qt.lighter(Theme.sourceButtonActiveBackground, 1.15) : Theme.buttonHoverBg
                    pressedColor: checked ? Qt.darker(Theme.sourceButtonActiveBackground, 1.15) : Theme.buttonPressedBg
                    iconColor: checked ? Theme.sourceButtonActiveIcon : (hovered ? Theme.highlight : Theme.menuButtonIcon)
                    Accessible.name: "智能点选（EasyTouch）"
                    Accessible.role: Accessible.CheckBox
                    Accessible.checkable: true
                    Accessible.checked: app.easyTouchEnabled
                    onClicked: app.setEasyTouchEnabled(!app.easyTouchEnabled)
                }
                Rectangle {
                    Layout.preferredWidth: 20; Layout.preferredHeight: Theme.separatorWidth; color: QuiColor.Border
                    Layout.alignment: Qt.AlignHCenter
                }
                QuiTextIconButton {
                    text: "ROI"
                    implicitWidth: Theme.toolbarButtonSize
                    implicitHeight: Theme.toolbarButtonSize
                    radius: Theme.radiusSmall
                    display: AbstractButton.TextOnly
                    font: Qt.font({ pixelSize: 11, bold: true })
                    normalColor: Theme.menuButtonBg
                    hoverColor: Theme.buttonHoverBg
                    pressedColor: Theme.buttonPressedBg
                    textColor: enabled ? (hovered ? Qt.darker(Theme.highlight, 1.15) : Theme.highlight) : Theme.disabledText
                    enabled: !app.runtimeBusy && app.selectedNode.length > 0
                    opacity: enabled ? 1.0 : 0.4
                    ToolTip.visible: hovered
                    ToolTip.delay: 200
                    ToolTip.text: "编辑输入 ROI"
                    Accessible.name: "编辑输入 ROI"
                    onClicked: root.roiRequested()
                }
                QuiTextIconButton {
                    text: "1:1 像素显示"
                    iconSource: Theme.iconOneToOne
                    implicitWidth: Theme.toolbarButtonSize; implicitHeight: Theme.toolbarButtonSize
                    radius: Theme.radiusSmall
                    display: AbstractButton.IconOnly
                    normalColor: Theme.menuButtonBg
                    hoverColor: Theme.buttonHoverBg
                    pressedColor: Theme.buttonPressedBg
                    iconColor: hovered ? Theme.highlight : Theme.menuButtonIcon
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
                    // Zoom, pan and ROI gestures are user input; a run owns the interaction.
                    enabled: app.workspaceEnabled
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
                    font: QuiFont.Body
                    color: Theme.assistHintText
                }
                QuiText {
                    anchors.bottom: parent.bottom; anchors.right: parent.right; anchors.margins: 8
                    text: viewport.hasImage ? Math.round(viewport.zoom * 100) + "%" : ""
                    font: QuiFont.Body; color: Theme.zoomText
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
