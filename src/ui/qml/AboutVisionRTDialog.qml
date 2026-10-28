import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import QtQuick.Window
import quickui
import VisionRT.Style 1.0

// The body is one plain-text block inside its own layered surface: the dialog background and the
// text surface must be visibly different, otherwise the selectable area has no boundary.
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
        Item {
            id: contentRoot
            implicitHeight: Math.min(380, Window.window ? Window.window.height - 240 : 380) + 32

            Rectangle {
                id: textFrame
                objectName: "aboutTextFrame"
                x: 16
                y: 16
                width: parent.width - 32
                height: parent.height - 32
                color: Theme.surfaceBg
                border.color: Theme.border
                border.width: 1
                radius: Theme.radiusSmall
                clip: true

                BoundedScrollView {
                    id: scrollView
                    anchors.fill: parent
                    anchors.margins: 1
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
                        font: Theme.monoFont
                        color: Theme.textPrimary
                        background: null
                        topPadding: 12
                        leftPadding: 14
                        rightPadding: 14
                        bottomPadding: 12

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
    }
}
