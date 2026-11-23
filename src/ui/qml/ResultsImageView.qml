import QtQuick
import QtQuick.Controls
import quickui
import VisionRT.View 1.0
import VisionRT.Ui 1.0

Rectangle {
    id: root
    required property QtObject controller
    signal trainingMenuRequested

    function fitContain() {
        viewport.fitContain();
    }
    function zoomBy(factor: real) {
        viewport.zoomBy(factor);
    }
    function fitOneToOne() {
        viewport.fitOneToOne();
    }

    color: QuiColor.Background
    ImageViewport {
        id: viewport
        objectName: "imageViewport"
        anchors.fill: parent
        readOnly: root.controller.runtimeBusy
        // Zoom, pan and ROI gestures are user input; a run owns the interaction.
        enabled: root.controller.workspaceEnabled
        Component.onCompleted: root.controller.attachViewport(viewport)
        onAssistHover: function (point) {
            root.controller.assistHover(point.x, point.y);
        }
        onAssistLeave: root.controller.assistCancel()
        onAssistClicked: function (point, append) {
            root.controller.assistClick(point.x, point.y, append);
        }
        onAssistCanceled: root.controller.assistCancel()
        onContextMenuRequested: {
            if (root.controller.canAddTrainingImage)
                root.trainingMenuRequested();
        }
    }
    QuiText {
        id: probeAnchor
        x: viewport.pointerPosition.x
        y: viewport.pointerPosition.y
        width: 0
        height: 0
        QuiToolTip {
            objectName: "pixelTooltip"
            text: viewport.pixelText
            visible: text.length > 0
            delay: 0
            timeout: -1
            focus: false
            closePolicy: Popup.NoAutoClose
            x: probeAnchor.x + implicitWidth + 16 < root.width ? 16 : -implicitWidth - 16
            y: probeAnchor.y + implicitHeight + 16 < root.height ? 16 : -implicitHeight - 16
        }
    }
    QuiText {
        anchors.bottom: parent.bottom
        anchors.right: parent.right
        anchors.margins: 5
        text: viewport.hasImage ? Math.round(viewport.zoom * 100) + "%" : ""
        font: QuiFont.Body
        color: QuiColor.FontCaption
    }
    QuiText {
        anchors.bottom: parent.bottom
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.margins: 5
        text: viewport.statusText
        visible: text.length > 0
        wrapMode: Text.Wrap
        color: QuiColor.Error
    }
}
