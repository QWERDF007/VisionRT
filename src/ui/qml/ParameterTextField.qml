import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import quickui
import VisionRT.Style 1.0

Item {
    id: root

    property alias text: input.text
    property alias inputItem: input
    property string placeholderText: ""
    property bool readOnly: false
    property bool invalid: false
    property bool flat: true
    property int actionIcon: 0
    property string actionText: ""
    // A second trailing action with the same shell, used by path parameters that need one direct
    // "choose file" and one direct "choose folder" button.
    property int secondaryActionIcon: 0
    property string secondaryActionText: ""
    property int leadingActionIcon: 0
    property string leadingActionText: ""
    property color leadingActionColor: Theme.textPrimary
    property bool leadingActionActive: false
    property Component leadingActionDelegate: null
    property color backgroundColor: QuiColor.Transparent

    signal commitRequested()
    signal cancelRequested()
    signal actionRequested()
    signal secondaryActionRequested()
    signal leadingActionRequested()

    implicitWidth: 200
    implicitHeight: Theme.paramEditorHeight

    function finishEditing() {
        if (root.enabled && !root.readOnly && !input.inputMethodComposing)
            root.commitRequested()
    }

    Timer {
        id: blurCommit
        interval: 0
        onTriggered: root.finishEditing()
    }

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

            QuiTextIconButton {
                id: leadingBtn
                visible: root.leadingActionIcon > 0 || root.leadingActionDelegate !== null
                Layout.preferredWidth: Theme.paramActionBtnWidth
                Layout.fillHeight: true
                iconSource: root.leadingActionIcon
                iconDelegate: root.leadingActionDelegate
                iconSize: Theme.paramRoiIconSize
                iconColor: root.leadingActionActive ? Theme.highlight : root.leadingActionColor
                normalColor: root.leadingActionActive ? Theme.paramActionActiveBg : "transparent"
                hoverColor: Theme.paramActionHoverBg
                focusPolicy: Qt.NoFocus
                text: root.leadingActionText
                enabled: root.enabled && !root.readOnly
                onClicked: root.leadingActionRequested()
                ToolTip.visible: hovered && text.length > 0
                ToolTip.text: text
            }

            Rectangle {
                visible: leadingBtn.visible
                Layout.preferredWidth: 1
                Layout.fillHeight: true
                color: Theme.paramEditorBorder
            }

            Rectangle {
                Layout.fillWidth: true
                Layout.fillHeight: true
                color: root.backgroundColor !== QuiColor.Transparent ? root.backgroundColor
                     : (hover.hovered ? QuiColor.ItemHover : "transparent")

                TextInput {
                    id: input
                    anchors.fill: parent
                    leftPadding: root.leadingActionIcon > 0 || root.leadingActionDelegate !== null ? 6 : 8
                    rightPadding: root.actionIcon > 0 ? 6 : 4
                    verticalAlignment: Text.AlignVCenter
                    font: Qt.font({ family: "Microsoft YaHei UI", pixelSize: Theme.paramEditorFontSize })
                    color: !root.enabled ? QuiColor.FontCaption : root.invalid ? QuiColor.Error : (input.text === "[]" ? QuiColor.FontCaption : QuiColor.FontPrimary)
                    selectionColor: Theme.highlight
                    selectedTextColor: "#ffffff"
                    selectByMouse: true
                    readOnly: root.readOnly
                    enabled: root.enabled
                    clip: true
                    autoScroll: true

                    onEditingFinished: {
                        if (activeFocus) root.finishEditing()
                        else blurCommit.restart()
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

                QuiText {
                    anchors.left: parent.left
                    anchors.leftMargin: root.leadingActionIcon > 0 || root.leadingActionDelegate !== null ? 6 : 8
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
                visible: secondaryBtn.visible
                Layout.preferredWidth: 1
                Layout.fillHeight: true
                color: Theme.paramEditorBorder
            }

            QuiTextIconButton {
                id: secondaryBtn
                visible: root.secondaryActionIcon > 0
                Layout.preferredWidth: Theme.paramActionBtnWidth
                Layout.fillHeight: true
                iconSource: root.secondaryActionIcon
                iconSize: Theme.paramActionIconSize
                normalColor: "transparent"
                hoverColor: Theme.paramRowHoverBg
                focusPolicy: Qt.NoFocus
                text: root.secondaryActionText
                enabled: root.enabled && !root.readOnly
                onClicked: root.secondaryActionRequested()
                ToolTip.visible: hovered && text.length > 0
                ToolTip.text: text
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
                normalColor: "transparent"
                hoverColor: Theme.paramRowHoverBg
                focusPolicy: Qt.NoFocus
                text: root.actionText
                enabled: root.enabled && !root.readOnly
                onClicked: root.actionRequested()
                ToolTip.visible: hovered && text.length > 0
                ToolTip.text: text
            }
        }
    }

    QuiToolTip {
        visible: hover.hovered && (!leadingBtn.visible || !leadingBtn.hovered)
                 && (!secondaryBtn.visible || !secondaryBtn.hovered)
                 && (!trailingBtn.visible || !trailingBtn.hovered) && input.text.length > 0
        text: input.text
        delay: 500
    }
}
