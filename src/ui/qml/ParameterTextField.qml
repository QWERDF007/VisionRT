import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import quickui
import VisionRT.Style 1.0

Item {
    id: root

    property alias text: input.text
    property alias inputItem: input
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
    property color leadingActionColor: QuiColor.FontPrimary
    property bool leadingActionActive: false
    property color backgroundColor: QuiColor.Transparent

    signal commitRequested()
    signal cancelRequested()
    signal actionRequested()
    signal secondaryActionRequested()
    signal leadingActionRequested()

    implicitWidth: 200
    implicitHeight: fieldFrame.implicitHeight

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

    ParameterFieldFrame {
        id: fieldFrame
        anchors.fill: parent
        inputItem: input
        hovered: hover.hovered
        invalid: root.invalid
        border.width: root.flat ? 0 : 1
        radius: root.flat ? 0 : 4
        color: root.flat ? "transparent" : (root.backgroundColor !== QuiColor.Transparent ? root.backgroundColor : (hover.hovered ? QuiColor.ItemHover : QuiColor.CardBackground))

        RowLayout {
            anchors.fill: parent
            anchors.margins: 0
            spacing: 0

            ParameterActionButton {
                id: leadingBtn
                visible: root.leadingActionIcon > 0
                Layout.preferredWidth: implicitWidth
                Layout.fillHeight: true
                iconSource: root.leadingActionIcon
                iconColor: root.leadingActionActive ? QuiColor.Highlight : root.leadingActionColor
                normalColor: root.leadingActionActive ? ParameterStyle.paramActionActiveBg : "transparent"
                hoverColor: ParameterStyle.paramActionHoverBg
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
                color: QuiColor.CardBorder
            }

            Rectangle {
                Layout.fillWidth: true
                Layout.fillHeight: true
                color: root.backgroundColor !== QuiColor.Transparent ? root.backgroundColor
                     : (hover.hovered ? QuiColor.ItemHover : "transparent")

                TextInput {
                    id: input
                    anchors.fill: parent
                    leftPadding: leadingBtn.visible ? 6 : 8
                    rightPadding: root.actionIcon > 0 ? 6 : 4
                    verticalAlignment: Text.AlignVCenter
                    font: QuiFont.Body
                    // One text colour for every parameter kind; a rejected draft is reported to the
                    // log and keeps its text instead of painting the field red.
                    color: root.enabled ? QuiColor.FontPrimary : WorkbenchStyle.iconMuted
                    selectionColor: QuiColor.Highlight
                    selectedTextColor: QuiColor.White
                    selectByMouse: true
                    readOnly: root.readOnly
                    enabled: root.enabled
                    clip: true
                    autoScroll: true

                    onActiveFocusChanged: if (!activeFocus) cursorPosition = 0
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

            }

            Rectangle {
                visible: secondaryBtn.visible
                Layout.preferredWidth: 1
                Layout.fillHeight: true
                color: QuiColor.CardBorder
            }

            ParameterActionButton {
                id: secondaryBtn
                visible: root.secondaryActionIcon > 0
                Layout.preferredWidth: implicitWidth
                Layout.fillHeight: true
                iconSource: root.secondaryActionIcon
                normalColor: "transparent"
                hoverColor: QuiColor.ItemHover
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
                color: QuiColor.CardBorder
            }

            ParameterActionButton {
                id: trailingBtn
                visible: root.actionIcon > 0
                Layout.preferredWidth: implicitWidth
                Layout.fillHeight: true
                iconSource: root.actionIcon
                normalColor: "transparent"
                hoverColor: QuiColor.ItemHover
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
