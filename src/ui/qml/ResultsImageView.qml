import QtQuick
import quickui
import VisionRT.View 1.0
import VisionRT.Style 1.0

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

    color: ResultsStyle.viewportBg
    ImageViewport {
        id: viewport
        objectName: "imageViewport"
        anchors.fill: parent
        readOnly: root.controller.runtimeBusy
        // Zoom, pan and ROI gestures are user input; a run owns the interaction.
        enabled: root.controller.workspaceEnabled
        inputRoiColor: ResultsStyle.inputRoiColor
        outputRoiColor: ResultsStyle.outputRoiColor
        assistRoiColor: ResultsStyle.assistRoiColor
        roiStrokeWidth: ResultsStyle.roiStrokeWidth
        roiHandleSize: ResultsStyle.roiHandleSize
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
        anchors.bottom: parent.bottom
        anchors.right: parent.right
        anchors.margins: 5
        text: viewport.hasImage ? Math.round(viewport.zoom * 100) + "%" : ""
        font: QuiFont.Body
        color: QuiColor.FontDark
    }
    QuiText {
        anchors.bottom: parent.bottom
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.margins: 5
        text: viewport.statusText
        visible: text.length > 0
        wrapMode: Text.Wrap
        color: WorkbenchStyle.warning
    }
}
