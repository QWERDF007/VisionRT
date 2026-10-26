import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import quickui

Item {
    id: root

    property alias text: input.text
    property alias inputItem: input
    property string placeholderText: ""
    property bool readOnly: false
    property bool invalid: false
    property bool flat: true
    property int actionIcon: QuiFontIcon.Edit
    property string actionText: "编辑…"
    property color backgroundColor: QuiColor.Transparent

    signal commitRequested(string commitText)
    signal cancelRequested()
    signal actionRequested()

    implicitWidth: 200
    implicitHeight: Theme.paramEditorHeight

    HoverHandler {
        id: hover
    }

    QuiTextBoxBackground {
        anchors.fill: parent
        inputItem: input
        hovered: hover.hovered
        invalid: root.invalid
        border.width: root.flat ? 0 : 1
        radius: root.flat ? 0 : Theme.radiusBase
        color: root.flat ? "transparent" : (root.backgroundColor !== QuiColor.Transparent ? root.backgroundColor : (hover.hovered ? Theme.paramRowHoverBg : Theme.paramEditorBg))

        RowLayout {
            anchors.fill: parent
            anchors.margins: root.flat ? 0 : 1
            spacing: 0

            Rectangle {
                Layout.fillWidth: true
                Layout.fillHeight: true
                color: root.backgroundColor !== QuiColor.Transparent ? root.backgroundColor
                     : (hover.hovered ? QuiColor.ItemHover : "transparent")

                TextInput {
                    id: input
                    anchors.fill: parent
                    leftPadding: 8
                    rightPadding: root.actionIcon > 0 ? 6 : 4
                    verticalAlignment: Text.AlignVCenter
                    font: Qt.font({ family: "Microsoft YaHei UI", pixelSize: Theme.paramEditorFontSize })
                    color: !root.enabled ? QuiColor.FontCaption : root.invalid ? QuiColor.Error : QuiColor.FontPrimary
                    selectionColor: Theme.highlight
                    selectedTextColor: "#ffffff"
                    selectByMouse: true
                    readOnly: root.readOnly
                    enabled: root.enabled
                    clip: true
                    autoScroll: true

                    // Enter commits single formula, Esc restores, blur does NOT auto commit!
                    Keys.onReturnPressed: function(event) {
                        if (inputMethodComposing) {
                            event.accepted = false
                            return
                        }
                        if (!root.readOnly && root.enabled) {
                            root.commitRequested(input.text)
                        }
                        event.accepted = true
                    }
                    Keys.onEnterPressed: function(event) {
                        if (inputMethodComposing) {
                            event.accepted = false
                            return
                        }
                        if (!root.readOnly && root.enabled) {
                            root.commitRequested(input.text)
                        }
                        event.accepted = true
                    }
                    Keys.onEscapePressed: function(event) {
                        if (inputMethodComposing) {
                            event.accepted = false
                            return
                        }
                        root.cancelRequested()
                        event.accepted = true
                    }
                }

                MouseArea {
                    anchors.fill: parent
                    visible: root.readOnly
                    cursorShape: Qt.ArrowCursor
                    acceptedButtons: Qt.LeftButton
                    onDoubleClicked: if (root.enabled) root.actionRequested()
                    propagateComposedEvents: true
                    onPressed: function(mouse) { mouse.accepted = false }
                }

                QuiText {
                    anchors.left: parent.left
                    anchors.leftMargin: 8
                    anchors.right: parent.right
                    anchors.rightMargin: root.actionIcon > 0 ? 30 : 6
                    anchors.verticalCenter: parent.verticalCenter
                    visible: input.text.length === 0 && !input.activeFocus
                    text: root.placeholderText
                    elide: Text.ElideRight
                    color: QuiColor.FontCaption
                    font: input.font
                }
            }

            Rectangle {
                visible: trailingBtn.visible
                Layout.preferredWidth: 1
                Layout.fillHeight: true
                color: Theme.paramEditorBorder
            }

            QuiTextIconButton {
                id: trailingBtn
                visible: root.actionIcon > 0
                Layout.preferredWidth: Theme.paramActionBtnWidth
                Layout.fillHeight: true
                iconSource: root.actionIcon
                iconSize: Theme.paramActionIconSize
                iconColor: Theme.textSecondary
                hoverColor: Theme.paramActionHoverBg
                focusPolicy: Qt.NoFocus
                text: root.actionText
                enabled: root.enabled
                onClicked: root.actionRequested()
                ToolTip.visible: hovered && text.length > 0
                ToolTip.text: text
            }
        }
    }
}
