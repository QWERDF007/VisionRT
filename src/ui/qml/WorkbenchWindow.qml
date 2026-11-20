import QtQuick
import QtQuick.Controls
import quickui
import VisionRT.Ui 1.0

Window {
    id: root
    flags: Qt.Dialog | Qt.WindowTitleHint | Qt.WindowSystemMenuHint | Qt.WindowCloseButtonHint
    modality: Qt.WindowModal
    color: QuiColor.Primary
    property Component body
    property Component footer
    readonly property alias bodyItem: bodyLoader.item
    property bool closeEnabled: true
    property bool applicationClosing: false
    function showAndRaise() {
        if (!visible && transientParent) {
            x = transientParent.x + (transientParent.width - width) / 2;
            y = transientParent.y + (transientParent.height - height) / 2;
        }
        if (visibility === Window.Minimized)
            showNormal();
        show();
        raise();
        requestActivate();
    }
    onApplicationClosingChanged: if (applicationClosing)
        close()
    onClosing: function (event) {
        if (!closeEnabled && !applicationClosing)
            event.accepted = false;
    }
    FocusScope {
        anchors.fill: parent
        focus: true
        Keys.onEscapePressed: if (root.closeEnabled)
            root.close()
        Loader {
            id: bodyLoader
            anchors.left: parent.left
            anchors.right: parent.right
            anchors.top: parent.top
            anchors.bottom: footerHost.top
            sourceComponent: root.body
            focus: true
        }
        Item {
            id: footerHost
            anchors.bottom: parent.bottom
            width: parent.width
            height: VisionRTSize.ButtonSize + 10
            Rectangle {
                width: parent.width
                height: 1
                color: QuiColor.Border
            }
            Loader {
                anchors.fill: parent
                anchors.margins: 5
                sourceComponent: root.footer
            }
        }
    }
}
