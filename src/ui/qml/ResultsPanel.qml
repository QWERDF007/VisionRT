import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import quickui
import VisionRT.Ui 1.0

Rectangle {
    id: root
    color: QuiColor.Primary
    signal roiRequested

    QuiMenu {
        id: trainingMenu
        objectName: "trainingMenu"
        QuiMenuItem {
            text: "加入训练"
            enabled: app.canAddTrainingImage
            onTriggered: app.addTrainingImage()
        }
    }

    Connections {
        target: app
        function onSelectionChanged() {
            trainingMenu.close();
        }
        function onTrainingChanged() {
            trainingMenu.close();
        }
        function onRuntimeChanged() {
            trainingMenu.close();
        }
    }

    RowLayout {
        anchors.fill: parent
        spacing: 0
        ResultsModeTabs {
            controller: app
            Layout.preferredWidth: implicitWidth
            Layout.fillHeight: true
        }
        Rectangle {
            Layout.preferredWidth: VisionRTSize.ThumbnailWidth
            Layout.fillHeight: true
            color: QuiColor.Primary
            ResultsThumbnails {
                anchors.fill: parent
                controller: app
                visible: !app.trainingMode
                onTrainingMenuRequested: trainingMenu.popup()
            }
            ResultsTrainingRecords {
                anchors.fill: parent
                controller: app
                visible: app.trainingMode
            }
            Rectangle {
                anchors.top: parent.top
                anchors.bottom: parent.bottom
                anchors.right: parent.right
                width: 1
                color: QuiColor.Border
            }
        }
        ResultsImageToolbar {
            controller: app
            Layout.preferredWidth: implicitWidth
            Layout.fillHeight: true
            onFitRequested: imageView.fitContain()
            onZoomRequested: function (factor) {
                imageView.zoomBy(factor);
            }
            onOneToOneRequested: imageView.fitOneToOne()
            onRoiRequested: root.roiRequested()
        }
        ResultsImageView {
            id: imageView
            controller: app
            Layout.fillWidth: true
            Layout.fillHeight: true
            onTrainingMenuRequested: trainingMenu.popup()
        }
    }
}
