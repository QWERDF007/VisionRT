import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import quickui
import VisionRT.View 1.0

Rectangle {
    id: root
    color: QuiColor.Primary
    signal roiRequested()
    ColumnLayout {
        anchors.fill: parent; spacing: 0
        RowLayout {
            Layout.fillWidth: true; Layout.preferredHeight: 36; Layout.leftMargin: 6; Layout.rightMargin: 6; spacing: 5
            QuiComboBox {
                id: outputChoice
                Layout.preferredWidth: Math.min(200, root.width * 0.28)
                model: app.imageOutputs; textRole: "name"; valueRole: "id"
                currentIndex: { for (let i = 0; i < app.imageOutputs.length; ++i) if (app.imageOutputs[i].id === app.selectedImageOutput) return i; return -1 }
                displayText: currentIndex < 0 ? "图像输出" : currentText
                onActivated: app.setImageOutput(currentValue)
                Accessible.name: "图像输出"
            }
            QuiTextIconButton { text: "适应窗口"; iconSource: QuiFontIcon.FullScreen; onClicked: viewport.fitContain() }
            QuiTextIconButton { text: "铺满"; iconSource: QuiFontIcon.FitPage; onClicked: viewport.fitCover() }
            QuiTextIconButton { text: "放大"; iconSource: QuiFontIcon.ZoomIn; onClicked: viewport.zoomBy(1.25) }
            QuiTextIconButton { text: "缩小"; iconSource: QuiFontIcon.ZoomOut; onClicked: viewport.zoomBy(0.8) }
            QuiButton { text: "1:1"; implicitWidth: 42; focusPolicy: Qt.TabFocus; onClicked: viewport.fitOneToOne() }
            QuiTextIconButton { text: "编辑输入 ROI"; iconSource: QuiFontIcon.Crop; enabled: !app.busy && app.selectedNode.length > 0; onClicked: root.roiRequested() }
            Item { Layout.fillWidth: true }
            QuiText { text: viewport.hasImage ? Math.round(viewport.zoom * 100) + "%" : ""; font: QuiFont.Caption }
        }
        RowLayout {
            visible: viewport.editing
            Layout.fillWidth: true; Layout.preferredHeight: visible ? 36 : 0
            Layout.leftMargin: 8; Layout.rightMargin: 8; spacing: 5
            QuiText { text: "输入预览"; color: "#a27b13"; font: QuiFont.BodyStrong }
            QuiComboBox {
                model: ["选择 / 调整", "线", "矩形", "圆", "多边形", "点集"]
                currentIndex: viewport.drawShape; enabled: !app.busy
                Layout.preferredWidth: 130
                onActivated: viewport.drawShape = currentIndex
            }
            QuiButton { text: "完成绘制"; enabled: !app.busy; focusPolicy: Qt.TabFocus; onClicked: viewport.finishDraft() }
            QuiButton { text: "取消草稿"; enabled: !app.busy; focusPolicy: Qt.TabFocus; onClicked: viewport.cancelDraft() }
            Item { Layout.fillWidth: true }
            QuiButton { text: "返回结果"; focusPolicy: Qt.TabFocus; onClicked: app.endRoiPreview() }
        }
        RowLayout {
            Layout.fillWidth: true; Layout.fillHeight: true; spacing: 0
            Rectangle {
                Layout.preferredWidth: 100; Layout.fillHeight: true; color: QuiColor.Primary
                ListView {
                    id: thumbnails
                    anchors.fill: parent; anchors.margins: 5; clip: true; spacing: 6
                    model: app.imageCount
                    currentIndex: app.imageIndex
                    ScrollBar.vertical: QuiScrollBar {}
                    delegate: Rectangle {
                        required property int index
                        width: thumbnails.width; height: 86
                        color: index === app.imageIndex ? "#dbeefe" : "#f4f6f9"
                        border.color: index === app.imageIndex ? QuiColor.Highlight : QuiColor.Border
                        border.width: index === app.imageIndex ? 2 : 1
                        ImageViewport {
                            id: thumbnail
                            property int thumbnailIndex: index
                            anchors.top: parent.top; anchors.left: parent.left; anchors.right: parent.right
                            anchors.margins: 4; height: 62
                            readOnly: true; enabled: false
                            Component.onCompleted: app.attachViewport(thumbnail)
                            onThumbnailIndexChanged: app.attachViewport(thumbnail)
                        }
                        QuiText { anchors.bottom: parent.bottom; anchors.bottomMargin: 3; anchors.horizontalCenter: parent.horizontalCenter; text: index; font: QuiFont.Caption }
                        MouseArea { anchors.fill: parent; onClicked: app.setImageIndex(index) }
                    }
                }
            }
            Rectangle {
                Layout.fillWidth: true; Layout.fillHeight: true; color: "#e9edf2"
                ImageViewport {
                    id: viewport; objectName: "imageViewport"
                    anchors.fill: parent; readOnly: app.busy
                    Component.onCompleted: app.attachViewport(viewport)
                }
                QuiText {
                    anchors.bottom: parent.bottom; anchors.left: parent.left; anchors.right: parent.right; anchors.margins: 8
                    text: viewport.statusText; visible: text.length > 0
                    wrapMode: Text.Wrap; color: "#a36a23"
                }
            }
        }
    }
}
