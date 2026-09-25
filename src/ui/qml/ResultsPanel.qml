import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import quickui
import VisionRT.View 1.0

Rectangle {
    id: root
    color: QuiColor.Primary
    border.color: "#d0d7de"
    border.width: 1
    signal roiRequested()
    ColumnLayout {
        anchors.fill: parent; anchors.margins: 1; spacing: 0
        Rectangle {
            Layout.fillWidth: true; Layout.preferredHeight: 34; color: "#f6f8fa"
            Rectangle {
                anchors.left: parent.left; anchors.right: parent.right; anchors.bottom: parent.bottom
                height: 1; color: "#d0d7de"
            }
            RowLayout {
                anchors.fill: parent; anchors.leftMargin: 8; anchors.rightMargin: 8; spacing: 5
                QuiComboBox {
                    id: outputChoice
                    Layout.preferredWidth: Math.min(180, root.width * 0.25)
                    implicitHeight: 24
                    model: app.imageOutputs; textRole: "name"; valueRole: "id"
                    currentIndex: { for (let i = 0; i < app.imageOutputs.length; ++i) if (app.imageOutputs[i].id === app.selectedImageOutput) return i; return -1 }
                    displayText: currentIndex < 0 ? "图像输出" : currentText
                    onActivated: app.setImageOutput(currentValue)
                    Accessible.name: "图像输出"
                }
                Rectangle { Layout.preferredWidth: 1; Layout.preferredHeight: 18; color: "#d0d7de" }
                QuiTextIconButton { text: "适应窗口"; iconSource: QuiFontIcon.FullScreen; implicitHeight: 24; onClicked: viewport.fitContain() }
                QuiTextIconButton { text: "铺满"; iconSource: QuiFontIcon.FitPage; implicitHeight: 24; onClicked: viewport.fitCover() }
                QuiTextIconButton { text: "放大"; iconSource: QuiFontIcon.ZoomIn; implicitHeight: 24; onClicked: viewport.zoomBy(1.25) }
                QuiTextIconButton { text: "缩小"; iconSource: QuiFontIcon.ZoomOut; implicitHeight: 24; onClicked: viewport.zoomBy(0.8) }
                QuiButton { text: "1:1"; implicitWidth: 38; implicitHeight: 24; focusPolicy: Qt.TabFocus; onClicked: viewport.fitOneToOne() }
                Rectangle { Layout.preferredWidth: 1; Layout.preferredHeight: 18; color: "#d0d7de" }
                QuiTextIconButton { text: "编辑输入 ROI"; iconSource: QuiFontIcon.Crop; implicitHeight: 24; enabled: !app.busy && app.selectedNode.length > 0; onClicked: root.roiRequested() }
                Item { Layout.fillWidth: true }
                QuiText { text: viewport.hasImage ? Math.round(viewport.zoom * 100) + "%" : ""; font: QuiFont.Caption; color: "#57606a" }
            }
        }
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
        RowLayout {
            Layout.fillWidth: true; Layout.fillHeight: true; spacing: 0
            Rectangle {
                visible: app.imageCount > 0
                Layout.preferredWidth: visible ? 100 : 0
                Layout.fillHeight: true; color: "#f6f8fa"
                Rectangle {
                    anchors.top: parent.top; anchors.bottom: parent.bottom; anchors.right: parent.right
                    width: 1; color: "#d0d7de"
                }
                ListView {
                    id: thumbnails
                    anchors.fill: parent; anchors.margins: 5; clip: true; spacing: 6
                    model: app.imageCount
                    currentIndex: app.imageIndex
                    ScrollBar.vertical: QuiScrollBar {}
                    delegate: Rectangle {
                        required property int index
                        width: thumbnails.width; height: 86
                        color: index === app.imageIndex ? "#dbeefe" : "#ffffff"
                        border.color: index === app.imageIndex ? QuiColor.Highlight : "#d0d7de"
                        border.width: index === app.imageIndex ? 2 : 1
                        radius: 3
                        ImageViewport {
                            id: thumbnail
                            property int thumbnailIndex: index
                            anchors.top: parent.top; anchors.left: parent.left; anchors.right: parent.right
                            anchors.margins: 4; height: 62
                            readOnly: true; enabled: false
                            Component.onCompleted: app.attachViewport(thumbnail)
                            onThumbnailIndexChanged: app.attachViewport(thumbnail)
                        }
                        QuiText { anchors.bottom: parent.bottom; anchors.bottomMargin: 3; anchors.horizontalCenter: parent.horizontalCenter; text: index; font: QuiFont.Caption; color: "#24292f" }
                        MouseArea { anchors.fill: parent; onClicked: app.setImageIndex(index) }
                    }
                }
            }
            Rectangle {
                Layout.fillWidth: true; Layout.fillHeight: true; color: "#1c1e22"
                ImageViewport {
                    id: viewport; objectName: "imageViewport"
                    anchors.fill: parent; readOnly: app.busy
                    Component.onCompleted: app.attachViewport(viewport)
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
