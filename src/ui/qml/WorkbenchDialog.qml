import QtQuick
import QtQuick.Controls
import quickui

QuiContentDialog {
    id: root
    titleFont: QuiFont.BodyStrong
    closePolicy: Popup.CloseOnEscape
    function prepareButtons(item) {
        if (!item) return
        if (item instanceof AbstractButton) item.focusPolicy = Qt.TabFocus
        for (let child of item.children) prepareButtons(child)
    }
    onOpened: Qt.callLater(function() { root.prepareButtons(root.contentItem) })
}
