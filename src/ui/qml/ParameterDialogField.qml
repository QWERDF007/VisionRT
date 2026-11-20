import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import quickui
import VisionRT.Ui 1.0

Item {
    id: root

    property alias text: input.text
    property alias inputItem: input
    property alias font: input.font
    property bool readOnly: false
    property bool invalid: false
    property bool flat: true
    property int actionIcon: QuiFontIcon.Edit
    property string actionText: "编辑…"
    property color backgroundColor: QuiColor.Transparent

    signal commitRequested(string commitText)
    signal cancelRequested
    signal actionRequested

    implicitWidth: 200
    implicitHeight: fieldFrame.implicitHeight

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
        color: root.flat ? "transparent" : (root.backgroundColor !== QuiColor.Transparent ? root.backgroundColor : (hover.hovered ? QuiColor.ItemHover : QuiColor.Primary))

        RowLayout {
            anchors.fill: parent
            anchors.margins: 0
            spacing: 0

            Rectangle {
                Layout.fillWidth: true
                Layout.fillHeight: true
                color: root.backgroundColor !== QuiColor.Transparent ? root.backgroundColor : (hover.hovered ? QuiColor.ItemHover : "transparent")

                TextInput {
                    id: input
                    anchors.fill: parent
                    leftPadding: 8
                    rightPadding: root.actionIcon > 0 ? 6 : 4
                    verticalAlignment: Text.AlignVCenter
                    font: QuiFont.Body
                    color: root.enabled ? QuiColor.FontPrimary : QuiColor.FontCaption
                    selectionColor: QuiColor.Highlight
                    selectedTextColor: QuiColor.Button
                    selectByMouse: true
                    readOnly: root.readOnly
                    enabled: root.enabled
                    clip: true
                    autoScroll: true

                    // Enter commits single formula, Esc restores, blur does NOT auto commit!
                    Keys.onReturnPressed: function (event) {
                        if (inputMethodComposing) {
                            event.accepted = false;
                            return;
                        }
                        if (!root.readOnly && root.enabled) {
                            root.commitRequested(input.text);
                        }
                        event.accepted = true;
                    }
                    Keys.onEnterPressed: function (event) {
                        if (inputMethodComposing) {
                            event.accepted = false;
                            return;
                        }
                        if (!root.readOnly && root.enabled) {
                            root.commitRequested(input.text);
                        }
                        event.accepted = true;
                    }
                    Keys.onEscapePressed: function (event) {
                        if (inputMethodComposing) {
                            event.accepted = false;
                            return;
                        }
                        root.cancelRequested();
                        event.accepted = true;
                    }
                }

                MouseArea {
                    anchors.fill: parent
                    visible: root.readOnly
                    cursorShape: Qt.ArrowCursor
                    acceptedButtons: Qt.LeftButton
                    onDoubleClicked: if (root.enabled)
                        root.actionRequested()
                    propagateComposedEvents: true
                    onPressed: function (mouse) {
                        mouse.accepted = false;
                    }
                }
            }

            Rectangle {
                visible: trailingBtn.visible
                Layout.preferredWidth: 1
                Layout.fillHeight: true
                color: QuiColor.Border
            }

            ParameterActionButton {
                id: trailingBtn
                visible: root.actionIcon > 0
                Layout.preferredWidth: implicitWidth
                Layout.fillHeight: true
                iconSource: root.actionIcon
                iconColor: QuiColor.FontCaption
                hoverColor: QuiColor.ItemHover
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
