import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import quickui
import VisionRT.Style 1.0

// One non-modal window, not a Popup: the log keeps living while it is hidden, can be minimized and
// restored from the main window, and never blocks the workbench. Closing only hides it.
// Every row is a finished line produced by the same formatter that writes the file, so the window
// has no columns, no header and no second serialisation of a record.
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

    ListView {
        id: list
        objectName: "logList"
        anchors.fill: parent
        anchors.margins: 6
        clip: true
        model: app.logModel
        boundsBehavior: Flickable.StopAtBounds
        boundsMovement: Flickable.StopAtBounds
        ScrollBar.vertical: QuiScrollBar {}
        onContentYChanged: followTail = atYEnd
        onCountChanged: if (followTail) Qt.callLater(list.positionViewAtEnd)
        delegate: TextEdit {
            id: row
            required property string line
            required property string level
            width: list.width - 12
            x: 6
            text: line
            readOnly: true
            selectByMouse: true
            wrapMode: TextEdit.Wrap
            color: level === "错误" || level === "致命" ? Theme.error
                 : level === "警告" ? Theme.warning : Theme.textPrimary
            selectionColor: Theme.highlight
            selectedTextColor: "#ffffff"
            font: Theme.monoFont
            renderType: Text.QtRendering
        }
    }
}
