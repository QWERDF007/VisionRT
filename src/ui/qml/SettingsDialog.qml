import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import quickui
import VisionRT.Style 1.0

// The platform settings window. It owns one draft for the three pages: switching pages never
// commits, 应用 writes every page and stays open, 确定 writes and closes, 取消 / Esc / the close
// button discard everything changed since the last apply.
//
// It is a QuiPopup rather than a WorkbenchDialog because the shared dialog owns a fixed button bar
// (neutral, cancel, confirm) with no close affordance and no Esc handling, while this window needs
// the confirm/cancel/apply order of the design. Theme, input controls and the dialog surface are
// the same ones the rest of the application uses.
QuiPopup {
    id: root
    objectName: "settingsDialog"
    modal: true
    closePolicy: Popup.NoAutoClose
    padding: 0
    implicitWidth: 760
    implicitHeight: 512
    bgColor: Theme.surfaceBg

    // Applied state, reloaded whenever the window opens or an apply succeeds. The draft below is the
    // only thing the controls edit.
    property var applied: ({ "editBuiltins": false, "loggingLevel": "info", "pythonExecutable": "" })
    property bool draftBuiltins: false
    property string draftLevel: "info"
    property string draftPython: ""
    property string page: "runtime"
    property string errorText: ""
    property string errorField: ""

    readonly property var levelKeys: ["trace", "debug", "info", "warn", "error", "critical", "off"]
    readonly property var levelNames: ["跟踪", "调试", "信息", "警告", "错误", "严重", "关闭"]
    readonly property string appliedPython: applied.pythonExecutable ? applied.pythonExecutable : ""
    readonly property bool dirty: draftBuiltins !== applied.editBuiltins
                                  || draftLevel !== applied.loggingLevel
                                  || draftPython !== appliedPython
    readonly property bool pythonPending: draftPython !== appliedPython

    function load() {
        applied = app.settingsSnapshot()
        draftBuiltins = applied.editBuiltins
        draftLevel = applied.loggingLevel
        draftPython = appliedPython
        errorText = app.settingsError
        errorField = app.settingsErrorField
        page = errorField.length > 0 ? errorField : "runtime"
    }

    function openDialog() {
        load()
        open()
    }

    function apply(closeAfter) {
        const values = {
            "editBuiltins": draftBuiltins,
            "loggingLevel": draftLevel,
            "pythonExecutable": draftPython
        }
        if (!app.applySettings(values)) {
            errorText = app.settingsError
            if (app.settingsErrorField.length > 0)
                page = app.settingsErrorField
            return false
        }
        applied = app.settingsSnapshot()
        draftPython = appliedPython
        pythonField.text = draftPython
        errorText = ""
        errorField = ""
        if (closeAfter)
            close()
        return true
    }

    Shortcut {
        enabled: root.opened
        sequence: StandardKey.Cancel
        onActivated: root.close()
    }

    Item {
        anchors.fill: parent

        // Header: brand mark, title and the close button.
        Item {
            id: header
            anchors.left: parent.left
            anchors.right: parent.right
            anchors.top: parent.top
            height: 44

            Rectangle {
                anchors.left: parent.left
                anchors.right: parent.right
                anchors.bottom: parent.bottom
                height: Theme.separatorWidth
                color: QuiColor.Border
            }
            RowLayout {
                anchors.fill: parent
                anchors.leftMargin: 18
                anchors.rightMargin: 8
                spacing: 10
                Rectangle {
                    Layout.preferredWidth: 22
                    Layout.preferredHeight: 22
                    radius: 11
                    color: Theme.highlight
                    QuiText {
                        anchors.centerIn: parent
                        text: "V"
                        color: "#ffffff"
                        font: QuiFont.BodyStrong
                    }
                }
                QuiText {
                    text: "平台设置"
                    color: Theme.textPrimary
                    font: QuiFont.BodyStrong
                }
                Item { Layout.fillWidth: true }
                QuiTextIconButton {
                    objectName: "settingsClose"
                    iconSource: QuiFontIcon.ChromeClose
                    iconSize: 12
                    normalColor: "transparent"
                    hoverColor: Theme.paramRowHoverBg
                    iconColor: Theme.iconSecondary
                    implicitWidth: 30
                    implicitHeight: 30
                    onClicked: root.close()
                }
            }
        }

        // Footer: one commit path, in the order of the design.
        Item {
            id: footer
            anchors.left: parent.left
            anchors.right: parent.right
            anchors.bottom: parent.bottom
            height: 60

            Rectangle {
                anchors.left: parent.left
                anchors.right: parent.right
                anchors.top: parent.top
                height: Theme.separatorWidth
                color: QuiColor.Border
            }
            RowLayout {
                anchors.fill: parent
                anchors.rightMargin: 18
                anchors.leftMargin: 18
                spacing: 8
                Item { Layout.fillWidth: true }
                QuiButton {
                    objectName: "settingsConfirm"
                    text: "确定"
                    onClicked: root.apply(true)
                }
                QuiButton {
                    objectName: "settingsCancel"
                    text: "取消"
                    onClicked: root.close()
                }
                QuiButton {
                    objectName: "settingsApply"
                    text: "应用"
                    enabled: root.dirty
                    normalColor: root.dirty ? Theme.highlight : QuiColor.Border
                    textColor: root.dirty ? "#ffffff" : Theme.textSecondary
                    // The shared button fades to 0.3 when disabled, which erases white text on the
                    // accent colour; the apply button states its disabled look here instead.
                    opacity: enabled ? 1 : 0.55
                    onClicked: root.apply(false)
                }
            }
        }

        // Navigation and the single form area.
        RowLayout {
            anchors.left: parent.left
            anchors.right: parent.right
            anchors.top: header.bottom
            anchors.bottom: footer.top
            spacing: 0

            Item {
                Layout.preferredWidth: 150
                Layout.fillHeight: true

                Rectangle {
                    anchors.right: parent.right
                    width: Theme.separatorWidth
                    height: parent.height
                    color: QuiColor.Border
                }
                Column {
                    anchors.fill: parent
                    anchors.margins: 8
                    spacing: 2
                    Repeater {
                        model: [
                            { "key": "builtins", "name": "工具设置", "icon": QuiFontIcon.DeveloperTools },
                            { "key": "runtime", "name": "运行设置", "icon": QuiFontIcon.Equalizer },
                            { "key": "logging", "name": "日志设置", "icon": QuiFontIcon.List }
                        ]
                        delegate: Rectangle {
                            required property var modelData
                            width: parent.width
                            height: 40
                            radius: Theme.radiusBase
                            color: root.page === modelData.key ? Theme.navItemActiveBg : "transparent"
                            RowLayout {
                                anchors.fill: parent
                                anchors.leftMargin: 10
                                anchors.rightMargin: 8
                                spacing: 8
                                QuiTextIcon {
                                    iconSource: modelData.icon
                                    iconSize: 15
                                    iconColor: root.page === modelData.key ? Theme.navItemActiveText : Theme.iconSecondary
                                }
                                QuiText {
                                    Layout.fillWidth: true
                                    text: modelData.name
                                    elide: Text.ElideRight
                                    color: root.page === modelData.key ? Theme.navItemActiveText : Theme.textPrimary
                                    font: QuiFont.Body
                                }
                            }
                            MouseArea {
                                anchors.fill: parent
                                cursorShape: Qt.PointingHandCursor
                                onClicked: root.page = modelData.key
                            }
                        }
                    }
                }
            }

            Item {
                Layout.fillWidth: true
                Layout.fillHeight: true
                clip: true

                Column {
                    id: form
                    anchors.left: parent.left
                    anchors.right: parent.right
                    anchors.top: parent.top
                    anchors.leftMargin: 22
                    anchors.rightMargin: 22
                    anchors.topMargin: 16
                    spacing: 14

                    Row {
                        spacing: 9
                        Rectangle {
                            width: 3
                            height: 16
                            color: Theme.highlight
                            anchors.verticalCenter: parent.verticalCenter
                        }
                        QuiText {
                            text: root.page === "builtins" ? "工具设置" : root.page === "logging" ? "日志设置" : "运行设置"
                            color: Theme.textPrimary
                            font: QuiFont.Title
                        }
                    }

                    // Tool settings: whether the declaration shipped with the installation is listed
                    // by the custom tool window.
                    RowLayout {
                        width: parent.width
                        visible: root.page === "builtins"
                        spacing: 10
                        QuiText {
                            Layout.preferredWidth: 92
                            horizontalAlignment: Text.AlignRight
                            text: "显示内置工具"
                            color: Theme.textSecondary
                            font: Theme.parameterFont
                        }
                        QuiToggleSwitch {
                            checked: root.draftBuiltins
                            clickListener: function() { root.draftBuiltins = !root.draftBuiltins }
                        }
                        Item { Layout.fillWidth: true }
                    }

                    // Runtime settings: the external interpreter, chosen through one file-or-folder
                    // picker or typed. Empty means the next start runs without Python.
                    Column {
                        width: parent.width
                        visible: root.page === "runtime"
                        spacing: 6
                        RowLayout {
                            width: parent.width
                            spacing: 10
                            QuiText {
                                Layout.preferredWidth: 92
                                Layout.alignment: Qt.AlignVCenter
                                horizontalAlignment: Text.AlignRight
                                text: "Python 环境"
                                color: Theme.textSecondary
                                font: Theme.parameterFont
                            }
                            ParameterTextField {
                                id: pythonField
                                Layout.fillWidth: true
                                Layout.maximumWidth: 420
                                flat: false
                                actionIcon: QuiFontIcon.Folder
                                onActionRequested: pythonPicker.open()
                                Connections {
                                    target: pythonField.inputItem
                                    function onTextEdited() {
                                        root.draftPython = pythonField.text
                                        if (root.errorField === "runtime")
                                            root.errorText = ""
                                    }
                                }
                            }
                            Item { Layout.fillWidth: true }
                        }
                        QuiText {
                            x: 102
                            visible: root.pythonPending
                            text: "Python 环境在下次启动时生效。"
                            color: Theme.textSecondary
                            font: Theme.parameterFont
                        }
                    }

                    // Log settings: one production threshold for the file and the window.
                    RowLayout {
                        width: parent.width
                        visible: root.page === "logging"
                        spacing: 10
                        QuiText {
                            Layout.preferredWidth: 92
                            horizontalAlignment: Text.AlignRight
                            text: "日志等级"
                            color: Theme.textSecondary
                            font: Theme.parameterFont
                        }
                        QuiComboBox {
                            id: levelBox
                            Layout.preferredWidth: 200
                            model: root.levelNames
                            currentIndex: Math.max(0, root.levelKeys.indexOf(root.draftLevel))
                            onActivated: root.draftLevel = root.levelKeys[currentIndex]
                        }
                        Item { Layout.fillWidth: true }
                    }

                    QuiText {
                        width: parent.width
                        visible: root.errorText.length > 0
                        text: root.errorText
                        color: Theme.error
                        wrapMode: Text.WordWrap
                        font: Theme.parameterFont
                    }
                }
            }
        }
    }

    onOpened: pythonField.text = root.draftPython

    QuiFileFolderDialog {
        id: pythonPicker
        title: "选择 Python 解释器"
        dialogMode: QuiFileFolderDialog.DialogMode.FileOrFolder
        onAccepted: function(path) {
            root.draftPython = path
            pythonField.text = path
            if (root.errorField === "runtime")
                root.errorText = ""
        }
    }
}
