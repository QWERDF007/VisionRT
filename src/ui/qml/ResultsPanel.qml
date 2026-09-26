import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import quickui
import VisionRT.View 1.0

Rectangle {
    id: root
    color: "#eaebef"
    border.color: "#d0d7de"
    border.width: 1
    signal roiRequested()

    RowLayout {
        anchors.fill: parent; anchors.margins: 1; spacing: 0

        // Column 1: Thumbnail column with top dropdown
        Rectangle {
            Layout.preferredWidth: 116
            Layout.fillHeight: true
            color: "#ffffff"
            Rectangle {
                anchors.top: parent.top; anchors.bottom: parent.bottom; anchors.right: parent.right
                width: 1; color: "#d0d7de"
            }
            ColumnLayout {
                anchors.fill: parent; spacing: 0
                Rectangle {
                    Layout.fillWidth: true; Layout.preferredHeight: 34; color: "#f6f8fa"
                    Rectangle {
                        anchors.left: parent.left; anchors.right: parent.right; anchors.bottom: parent.bottom
                        height: 1; color: "#d0d7de"
                    }
                    QuiComboBox {
                        id: outputChoice
                        anchors.fill: parent; anchors.margins: 4
                        model: app.imageOutputs; textRole: "name"; valueRole: "id"
                        currentIndex: { for (let i = 0; i < app.imageOutputs.length; ++i) if (app.imageOutputs[i].id === app.selectedImageOutput) return i; return -1 }
                        displayText: currentIndex < 0 ? (app.imageOutputs.length > 0 ? "图像输出" : "图像输入") : currentText
                        onActivated: app.setImageOutput(currentValue)
                        Accessible.name: "图像输出"
                    }
                }
                Item {
                    Layout.fillWidth: true; Layout.fillHeight: true
                    ListView {
                        id: thumbnails
                        anchors.fill: parent; anchors.margins: 4; clip: true; spacing: 6
                        model: app.imageCount
                        currentIndex: app.imageIndex
                        ScrollBar.vertical: QuiScrollBar {}
                        delegate: Rectangle {
                            required property int index
                            width: thumbnails.width - 8; height: 86
                            anchors.horizontalCenter: parent ? parent.horizontalCenter : undefined
                            color: index === app.imageIndex ? "#dbeefe" : "#ffffff"
                            border.color: index === app.imageIndex ? QuiColor.Highlight : "#d0d7de"
                            border.width: index === app.imageIndex ? 2 : 1
                            radius: 3
                            ImageViewport {
                                id: thumbnail
                                property int thumbnailIndex: index
                                anchors.top: parent.top; anchors.left: parent.left; anchors.right: parent.right
                                anchors.margins: 4; height: 60
                                readOnly: true; enabled: false
                                Component.onCompleted: app.attachViewport(thumbnail)
                                onThumbnailIndexChanged: app.attachViewport(thumbnail)
                            }
                            QuiText { anchors.bottom: parent.bottom; anchors.bottomMargin: 3; anchors.horizontalCenter: parent.horizontalCenter; text: index; font: QuiFont.Caption; color: "#24292f" }
                            MouseArea { anchors.fill: parent; onClicked: app.setImageIndex(index) }
                        }
                    }
                }
            }
        }

        // Column 2: Vertical toolbar
        Rectangle {
            Layout.preferredWidth: 32
            Layout.fillHeight: true
            color: "#ffffff"
            Rectangle {
                anchors.top: parent.top; anchors.bottom: parent.bottom; anchors.right: parent.right
                width: 1; color: "#d0d7de"
            }
            ColumnLayout {
                anchors.top: parent.top; anchors.topMargin: 4
                anchors.horizontalCenter: parent.horizontalCenter
                spacing: 4
                QuiTextIconButton {
                    iconSource: QuiFontIcon.FullScreen; implicitWidth: 26; implicitHeight: 26
                    text: "适应窗口"; onClicked: viewport.fitContain()
                }
                QuiTextIconButton {
                    iconSource: QuiFontIcon.ZoomIn; implicitWidth: 26; implicitHeight: 26
                    text: "放大"; onClicked: viewport.zoomBy(1.25)
                }
                QuiTextIconButton {
                    iconSource: QuiFontIcon.ZoomOut; implicitWidth: 26; implicitHeight: 26
                    text: "缩小"; onClicked: viewport.zoomBy(0.8)
                }
                Rectangle {
                    Layout.preferredWidth: 20; Layout.preferredHeight: 1; color: "#d0d7de"
                    Layout.alignment: Qt.AlignHCenter
                }
                QuiTextIconButton {
                    iconSource: QuiFontIcon.Crop; implicitWidth: 26; implicitHeight: 26
                    text: "编辑输入 ROI"; enabled: !app.busy && app.selectedNode.length > 0
                    onClicked: root.roiRequested()
                }
                QuiTextIconButton {
                    iconSource: QuiFontIcon.Refresh; implicitWidth: 26; implicitHeight: 26
                    text: "1:1"; onClicked: viewport.fitOneToOne()
                }
            }
        }

        // Column 3: Viewport area
        ColumnLayout {
            Layout.fillWidth: true
            Layout.fillHeight: true
            spacing: 0

            Rectangle {
                visible: viewport.editing
                Layout.fillWidth: true; Layout.preferredHeight: visible ? 34 : 0
                color: "#fff8e6"
                Rectangle {
                    anchors.left: parent.left; anchors.right: parent.right; anchors.bottom: parent.bottom
                    height: 1; color: "#f0d59e"
                }
                RowLayout {
                    anchors.fill: parent; anchors.leftMargin: 8; anchors.rightMargin: 8; spacing: 5
                    QuiText { text: "输入预览"; color: "#a27b13"; font: QuiFont.BodyStrong }
                    QuiComboBox {
                        model: ["选择 / 调整", "线", "矩形", "圆", "多边形", "点集"]
                        currentIndex: viewport.drawShape; enabled: !app.busy
                        Layout.preferredWidth: 130; implicitHeight: 24
                        onActivated: viewport.drawShape = currentIndex
                    }
                    QuiButton { text: "完成绘制"; implicitHeight: 24; enabled: !app.busy; focusPolicy: Qt.TabFocus; onClicked: viewport.finishDraft() }
                    QuiButton { text: "取消草稿"; implicitHeight: 24; enabled: !app.busy; focusPolicy: Qt.TabFocus; onClicked: viewport.cancelDraft() }
                    Item { Layout.fillWidth: true }
                    QuiButton { text: "返回结果"; implicitHeight: 24; focusPolicy: Qt.TabFocus; onClicked: app.endRoiPreview() }
                }
            }

            Rectangle {
                Layout.fillWidth: true; Layout.fillHeight: true; color: "#eaebef"
                ImageViewport {
                    id: viewport; objectName: "imageViewport"
                    anchors.fill: parent; readOnly: app.busy
                    Component.onCompleted: app.attachViewport(viewport)
                }
                QuiText {
                    anchors.bottom: parent.bottom; anchors.right: parent.right; anchors.margins: 8
                    text: viewport.hasImage ? Math.round(viewport.zoom * 100) + "%" : ""
                    font: QuiFont.Caption; color: "#57606a"
                }
                QuiText {
                    anchors.bottom: parent.bottom; anchors.left: parent.left; anchors.right: parent.right; anchors.margins: 8
                    text: viewport.statusText; visible: text.length > 0
                    wrapMode: Text.Wrap; color: "#e2b340"
                }
            }
        }
    }
}
