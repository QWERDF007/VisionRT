import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import quickui
import VisionRT.Style 1.0

// One non-modal window, not a Popup: the log keeps living while it is hidden, can be minimized and
// restored from the main window, and never blocks the workbench. Closing only hides it.
Window {
    id: root
    objectName: "logWindow"
    title: "日志"
    flags: Qt.Dialog | Qt.WindowTitleHint | Qt.WindowSystemMenuHint | Qt.WindowMinMaxButtonsHint
           | Qt.WindowCloseButtonHint
    modality: Qt.NonModal
    color: Theme.surfaceBg
    width: 880
    height: 420
    minimumWidth: 520
    minimumHeight: 240

    property bool followTail: true

    function syncActive() {
        // Hidden or minimized windows do not touch the model at all; the service keeps a bounded
        // mailbox instead and hands it over in batches once the window is usable again.
        app.setLogViewActive(root.visible && root.visibility !== Window.Minimized)
    }
    function showAndRaise() {
        if (root.visibility === Window.Minimized)
            root.showNormal()
        root.show()
        root.raise()
        root.requestActivate()
        syncActive()
    }
    onClosing: function(close) {
        // Closing the window hides it; writing continues and the single instance stays available.
        close.accepted = false
        root.hide()
    }
    onVisibleChanged: syncActive()
    onVisibilityChanged: syncActive()

    ColumnLayout {
        anchors.fill: parent
        spacing: 0
        Rectangle {
            Layout.fillWidth: true
            Layout.preferredHeight: 30
            color: Theme.paramGroupHeaderBg
            Rectangle {
                anchors.left: parent.left
                anchors.right: parent.right
                anchors.bottom: parent.bottom
                height: 1
                color: Theme.border
            }
            RowLayout {
                anchors.fill: parent
                anchors.leftMargin: 10
                anchors.rightMargin: 12
                spacing: 8
                QuiText { Layout.preferredWidth: 90; text: "时间"; font: Theme.captionFont; color: Theme.textSecondary }
                QuiText { Layout.preferredWidth: 44; text: "级别"; font: Theme.captionFont; color: Theme.textSecondary }
                QuiText { Layout.preferredWidth: 72; text: "来源"; font: Theme.captionFont; color: Theme.textSecondary }
                QuiText { Layout.fillWidth: true; text: "消息"; font: Theme.captionFont; color: Theme.textSecondary }
            }
        }
        ListView {
            id: list
            objectName: "logList"
            Layout.fillWidth: true
            Layout.fillHeight: true
            clip: true
            model: app.logModel
            boundsBehavior: Flickable.StopAtBounds
            ScrollBar.vertical: QuiScrollBar {}
            onContentYChanged: followTail = atYEnd
            onCountChanged: if (followTail) Qt.callLater(list.positionViewAtEnd)
            delegate: Item {
                id: rowItem
                required property string time
                required property string level
                required property string source
                required property string message
                width: list.width
                height: row.implicitHeight + 2
                RowLayout {
                    id: row
                    x: 10
                    y: 1
                    width: parent.width - 22
                    height: implicitHeight
                    spacing: 8
                    QuiText {
                        Layout.preferredWidth: 90
                        text: rowItem.time
                        font: Theme.monoFont
                        color: Theme.textSecondary
                        renderType: Text.QtRendering
                    }
                    QuiText {
                        Layout.preferredWidth: 44
                        text: rowItem.level
                        font: Theme.captionFont
                        color: rowItem.level === "错误" || rowItem.level === "致命" ? Theme.error
                             : rowItem.level === "警告" ? Theme.warning : Theme.textSecondary
                        renderType: Text.QtRendering
                    }
                    QuiText {
                        Layout.preferredWidth: 72
                        text: rowItem.source
                        font: Theme.captionFont
                        color: Theme.textSecondary
                        elide: Text.ElideRight
                        renderType: Text.QtRendering
                    }
                    // Read-only selectable text: the window only ever shows time, level, source and
                    // message, and every line can be selected and copied with the normal shortcuts.
                    TextEdit {
                        Layout.fillWidth: true
                        text: rowItem.message
                        readOnly: true
                        selectByMouse: true
                        wrapMode: TextEdit.Wrap
                        color: Theme.textPrimary
                        selectionColor: Theme.highlight
                        selectedTextColor: "#ffffff"
                        font: Theme.parameterFont
                        renderType: Text.QtRendering
                    }
                }
            }
        }
    }
}
