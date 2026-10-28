import QtQuick
import QtQuick.Controls

// ScrollView whose content stops exactly at the edges instead of bouncing past them.
// Qt's Flickable defaults to DragAndOvershootBounds; the workbench wants one behaviour everywhere,
// so every scrolling surface that is not a bare ListView/Flickable uses this component instead of
// repeating the same two properties at each call site.
ScrollView {
    id: root

    function pinContent() {
        if (!contentItem)
            return
        contentItem.boundsBehavior = Flickable.StopAtBounds
        contentItem.boundsMovement = Flickable.StopAtBounds
    }

    Component.onCompleted: pinContent()
    onContentItemChanged: pinContent()
}
