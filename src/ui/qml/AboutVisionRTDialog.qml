import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import QtQuick.Window
import quickui

WorkbenchDialog {
    id: root
    title: "关于 VisionRT"
    implicitWidth: Math.min(600, Window.window ? Window.window.width - 40 : 600)
    useNeutralButton: true
    neutralText: "复制全部"
    useNegativeButton: false
    positiveText: "关闭"
    onNeutralClickListener: function() {
        app.copyAboutText()
    }

    contentDelegate: Component {
        ScrollView {
            id: scrollView
            implicitHeight: Math.min(320, Window.window ? Window.window.height - 200 : 320)
            clip: true
            ScrollBar.horizontal.policy: ScrollBar.AsNeeded
            ScrollBar.vertical.policy: ScrollBar.AsNeeded

            TextArea {
                id: aboutTextArea
                objectName: "aboutTextArea"
                text: app.aboutText
                readOnly: true
                selectByMouse: true
                textFormat: TextEdit.PlainText
                wrapMode: TextEdit.NoWrap
                font.family: "Cascadia Code, Consolas, Courier New, monospace"
                font.pixelSize: 13
                color: QuiColor.FontPrimary
                background: null
                topPadding: 10
                leftPadding: 20
                rightPadding: 20
                bottomPadding: 10

                MouseArea {
                    anchors.fill: parent
                    acceptedButtons: Qt.RightButton
                    onClicked: function(mouse) {
                        aboutMenu.popup()
                    }
                }

                QuiMenu {
                    id: aboutMenu
                    objectName: "aboutMenu"
                    QuiMenuItem {
                        objectName: "aboutCopyAction"
                        text: "复制"
                        enabled: aboutTextArea.selectedText.length > 0
                        onTriggered: aboutTextArea.copy()
                    }
                    QuiMenuItem {
                        objectName: "aboutSelectAllAction"
                        text: "全选"
                        onTriggered: aboutTextArea.selectAll()
                    }
                }
            }
        }
    }
}
