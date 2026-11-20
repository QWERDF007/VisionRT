import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import quickui
import VisionRT.Ui 1.0

ToolBar {
    id: root
    required property var fileMenuObject
    required property var settingsMenuObject
    required property var toolsMenuObject
    required property var helpMenuObject
    required property Action saveCommand
    required property Action runCommand
    required property Action loopCommand
    property bool workspaceEnabled: false
    property bool stopEnabled: false
    property bool stopping: false
    property bool busy: false
    property string statusText: ""
    signal resetRequested
    function openFileMenu() {
        root.fileMenuObject.popup(fileBtn, 0, fileBtn.height);
    }
    function openToolsMenu() {
        root.toolsMenuObject.popup(toolsBtn, 0, toolsBtn.height);
    }
    objectName: "mainToolbar"
    padding: 0
    height: VisionRTSize.HeaderSize
    background: Rectangle {
        color: QuiColor.Primary
        border.width: 1
        border.color: QuiColor.Border
    }
    RowLayout {
        anchors.fill: parent
        anchors.leftMargin: 5
        anchors.rightMargin: 5
        spacing: 4

        // Menus
        WorkbenchActionButton {
            id: fileBtn
            text: "文件"
            iconSource: QuiFontIcon.GlobalNavButton
            display: AbstractButton.TextBesideIcon
            enabled: root.workspaceEnabled
            onClicked: {
                if (root.fileMenuObject.visible)
                    root.fileMenuObject.dismiss();
                else
                    root.fileMenuObject.popup(fileBtn, 0, fileBtn.height);
            }
        }
        WorkbenchActionButton {
            id: settingsBtn
            text: "设置"
            iconSource: QuiFontIcon.Settings
            display: AbstractButton.TextBesideIcon
            enabled: root.workspaceEnabled
            onClicked: {
                if (root.settingsMenuObject.visible)
                    root.settingsMenuObject.dismiss();
                else
                    root.settingsMenuObject.popup(settingsBtn, 0, settingsBtn.height);
            }
        }
        WorkbenchActionButton {
            id: toolsBtn
            text: "工具"
            iconSource: QuiFontIcon.AllApps
            display: AbstractButton.TextBesideIcon
            enabled: root.workspaceEnabled
            onClicked: {
                if (root.toolsMenuObject.visible)
                    root.toolsMenuObject.dismiss();
                else
                    root.toolsMenuObject.popup(toolsBtn, 0, toolsBtn.height);
            }
        }
        WorkbenchActionButton {
            id: helpBtn
            text: "帮助"
            iconSource: QuiFontIcon.Help
            display: AbstractButton.TextBesideIcon
            onClicked: {
                if (root.helpMenuObject.visible)
                    root.helpMenuObject.dismiss();
                else
                    root.helpMenuObject.popup(helpBtn, 0, helpBtn.height);
            }
        }

        WorkbenchSeparator {
            Layout.leftMargin: 5
            Layout.rightMargin: 5
            Layout.alignment: Qt.AlignVCenter
        }

        // Actions & Execution
        WorkbenchActionButton {
            action: root.saveCommand
            text: "保存 (Ctrl+S)"
            iconSource: QuiFontIcon.Save
        }
        WorkbenchActionButton {
            action: root.runCommand
            text: "单次执行 (F5)"
            iconSource: QuiFontIcon.Play
        }
        WorkbenchActionButton {
            action: root.loopCommand
            text: root.loopCommand.text
            iconSource: root.stopEnabled || root.stopping ? QuiFontIcon.Stop : QuiFontIcon.RepeatAll
            iconColor: root.stopEnabled || root.stopping ? QuiColor.Error : QuiColor.FontPrimary
        }

        Item {
            Layout.fillWidth: true
        }

        QuiText {
            text: root.statusText
            visible: root.busy
            color: QuiColor.Highlight
            font: QuiFont.Body
        }

        WorkbenchActionButton {
            iconSource: QuiFontIcon.Refresh
            text: "复位"
            display: AbstractButton.TextBesideIcon
            textColor: QuiColor.FontPrimary
            enabled: root.workspaceEnabled
            focusPolicy: Qt.TabFocus
            onClicked: root.resetRequested()
        }
    }
}
