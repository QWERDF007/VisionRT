import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import quickui
import VisionRT.Style 1.0

// One non-modal window, not a Popup: the log keeps living while it is hidden, can be minimized and
// restored from the main window, and never blocks the workbench. Closing only hides it.
//
// The body is a single read-only text area, so a selection can start in the middle of one line and
// end in the middle of another. Every record is one logical line of that text; wrapping is a visual
// detail and never adds a record.
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

    // Character count of each logical line, used only to trim the text when the model evicts rows.
    property var lineLengths: []

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

    function atBottom() {
        const view = scroll.contentItem
        return !view || view.contentY >= view.contentHeight - view.height - 2
    }
    function scrollToBottom() {
        const view = scroll.contentItem
        if (view)
            view.contentY = Math.max(0, view.contentHeight - view.height)
    }
    function appendRows(first, last) {
        const parts = []
        for (let row = first; row <= last; ++row) {
            const line = app.logModel.text(row)
            parts.push(line)
            lineLengths.push(line.length)
        }
        const chunk = parts.join("\n")
        if (logText.length === 0)
            logText.insert(0, chunk)
        else
            logText.insert(logText.length, "\n" + chunk)
    }
    function removeLeadingRows(count) {
        let chars = 0
        for (let row = 0; row < count && row < lineLengths.length; ++row)
            chars += lineLengths[row] + 1
        lineLengths.splice(0, count)
        if (chars > 0)
            logText.remove(0, Math.min(chars, logText.length))
    }
    function rebuild() {
        lineLengths = []
        logText.text = app.logModel.allText()
        for (let row = 0; row < app.logModel.count; ++row)
            lineLengths.push(app.logModel.text(row).length)
        if (atBottom())
            Qt.callLater(scrollToBottom)
    }

    Component.onCompleted: {
        app.attachLogHighlighter(logText.textDocument, Theme.warning, Theme.error)
        rebuild()
    }

    Connections {
        target: app.logModel
        function onRowsInserted(parent, first, last) {
            const follow = root.atBottom()
            root.appendRows(first, last)
            if (follow)
                Qt.callLater(root.scrollToBottom)
        }
        function onRowsRemoved(parent, first, last) {
            const follow = root.atBottom()
            root.removeLeadingRows(last - first + 1)
            if (follow)
                Qt.callLater(root.scrollToBottom)
        }
        function onModelReset() {
            root.rebuild()
        }
    }

    ColumnLayout {
        anchors.fill: parent
        anchors.margins: 6
        spacing: 6

        RowLayout {
            id: toolbar
            Layout.fillWidth: true
            spacing: 6
            Item { Layout.fillWidth: true }
            QuiButton {
                objectName: "logCopyAll"
                text: "复制全部"
                enabled: app.logModel.count > 0
                onClicked: app.copyAllLogs()
            }
            QuiButton {
                objectName: "logClear"
                text: "清空"
                enabled: app.logModel.count > 0
                onClicked: app.logModel.clear()
            }
        }

        ScrollView {
            id: scroll
            Layout.fillWidth: true
            Layout.fillHeight: true
            clip: true
            ScrollBar.vertical: QuiScrollBar {}
            ScrollBar.horizontal.policy: ScrollBar.AlwaysOff

            TextArea {
                id: logText
                objectName: "logText"
                readOnly: true
                selectByMouse: true
                textFormat: TextEdit.PlainText
                wrapMode: TextEdit.Wrap
                width: scroll.availableWidth
                font: QuiFont.Body
                color: Theme.textPrimary
                selectionColor: Theme.highlight
                selectedTextColor: "#ffffff"
                background: null
            }
        }
    }
}
