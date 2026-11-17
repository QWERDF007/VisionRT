import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import quickui

Window {
    id: root
    flags: Qt.Dialog | Qt.FramelessWindowHint
    modality: Qt.WindowModal
    color: QuiColor.Transparent
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

    Rectangle {
        anchors.fill: parent
        color: QuiColor.Button
        border.color: QuiColor.Border
        radius: 8
        FocusScope {
            anchors.fill: parent
            anchors.margins: 1
            focus: true
            Keys.onEscapePressed: function (event) {
                if (root.closeEnabled)
                    root.close();
                event.accepted = true;
            }
            Item {
                id: header
                anchors.left: parent.left
                anchors.right: parent.right
                height: 48
                DragHandler {
                    target: null
                    property point origin
                    onActiveChanged: if (active)
                        origin = Qt.point(root.x, root.y)
                    onActiveTranslationChanged: {
                        root.x = origin.x + activeTranslation.x;
                        root.y = origin.y + activeTranslation.y;
                    }
                }
                RowLayout {
                    anchors.fill: parent
                    anchors.leftMargin: 5
                    anchors.rightMargin: 5
                    QuiText {
                        text: root.title
                        font: QuiFont.BodyStrong
                        color: QuiColor.FontPrimary
                    }
                    Item {
                        Layout.fillWidth: true
                    }
                    WorkbenchActionButton {
                        objectName: "windowClose"
                        enabled: root.closeEnabled
                        iconSource: QuiFontIcon.ChromeClose
                        text: "关闭"
                        onClicked: root.close()
                    }
                }
                Rectangle {
                    anchors.bottom: parent.bottom
                    width: parent.width
                    height: 1
                    color: QuiColor.Border
                }
            }
            Loader {
                id: bodyLoader
                anchors.left: parent.left
                anchors.right: parent.right
                anchors.top: header.bottom
                anchors.bottom: footerHost.top
                sourceComponent: root.body
                focus: true
            }
            Item {
                id: footerHost
                anchors.bottom: parent.bottom
                width: parent.width
                height: 56
                Rectangle {
                    objectName: "footerSeparator"
                    width: parent.width
                    height: 1
                    color: QuiColor.Border
                }
                Loader {
                    anchors.fill: parent
                    anchors.leftMargin: 5
                    anchors.rightMargin: 5
                    anchors.topMargin: 5
                    anchors.bottomMargin: 5
                    sourceComponent: root.footer
                }
            }
        }
    }
}
