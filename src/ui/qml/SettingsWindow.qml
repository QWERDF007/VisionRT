import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import quickui
import VisionRT.Style 1.0

WorkbenchWindow {
    id: root
    objectName: "settingsWindow"
    title: "平台设置"
    width: 760
    height: 512
    minimumWidth: 640
    minimumHeight: 420
    closeEnabled: !applying
    // Applied state, reloaded whenever the window opens or an apply succeeds. The draft below is the
    // only thing the controls edit.
    property var applied: ({
            "editBuiltins": false,
            "loggingLevel": "info",
            "pythonExecutable": ""
        })
    property bool draftBuiltins: false
    property string draftLevel: "info"
    property string draftPython: ""
    property string page: "runtime"
    property string errorText: ""
    property string errorField: ""
    // The one commit asks the controller for an answer instead of receiving it: a changed
    // interpreter is checked off the GUI thread, so the outcome arrives as settingsApplied.
    property bool closeAfterApply: true

    readonly property var levelKeys: ["trace", "debug", "info", "warn", "error", "critical", "off"]
    readonly property var levelNames: ["跟踪", "调试", "信息", "警告", "错误", "严重", "关闭"]
    readonly property string appliedPython: applied.pythonExecutable ? applied.pythonExecutable : ""
    readonly property bool dirty: draftBuiltins !== applied.editBuiltins || draftLevel !== applied.loggingLevel || draftPython !== appliedPython
    readonly property bool pythonPending: draftPython !== appliedPython
    readonly property bool applying: app.settingsApplying
    readonly property bool environmentRejected: app.pythonEnvironmentPhase === "invalid" || app.pythonEnvironmentPhase === "backendFailed"

    function load() {
        applied = app.settingsSnapshot();
        draftBuiltins = applied.editBuiltins;
        draftLevel = applied.loggingLevel;
        draftPython = appliedPython;
        errorText = app.settingsError;
        errorField = app.settingsErrorField;
        page = errorField.length > 0 ? errorField : "runtime";
    }

    function openDialog() {
        if (visible) {
            showAndRaise();
            return;
        }
        load();
        bodyItem.pythonText = draftPython;
        showAndRaise();
    }

    function settle(accepted) {
        if (!accepted) {
            errorText = app.settingsError;
            errorField = app.settingsErrorField;
            if (errorField.length > 0)
                page = errorField;
            return;
        }
        applied = app.settingsSnapshot();
        draftPython = appliedPython;
        bodyItem.pythonText = draftPython;
        errorText = "";
        errorField = "";
        if (closeAfterApply)
            close();
    }

    function apply(closeAfter) {
        if (applying)
            return;
        closeAfterApply = closeAfter;
        // false means the draft never reached a commit; an accepted draft answers later through
        // settingsApplied, which also covers a refused check.
        const accepted = app.applySettings({
            "editBuiltins": draftBuiltins,
            "loggingLevel": draftLevel,
            "pythonExecutable": draftPython
        });
        if (!accepted) {
            errorText = app.settingsError;
            errorField = app.settingsErrorField;
            if (errorField.length > 0)
                page = errorField;
        }
    }

    Connections {
        target: app
        function onSettingsApplied(accepted) {
            root.settle(accepted);
        }
    }

    body: Component {
        RowLayout {
            property alias pythonText: pythonField.text
            anchors.fill: parent
            spacing: 0

            Item {
                Layout.preferredWidth: 150
                Layout.fillHeight: true

                Rectangle {
                    anchors.right: parent.right
                    width: 1
                    height: parent.height
                    color: QuiColor.Border
                }
                Column {
                    anchors.fill: parent
                    anchors.margins: 5
                    spacing: 5
                    Repeater {
                        model: [
                            {
                                "key": "builtins",
                                "name": "工具设置",
                                "icon": QuiFontIcon.DeveloperTools
                            },
                            {
                                "key": "runtime",
                                "name": "运行设置",
                                "icon": QuiFontIcon.Equalizer
                            },
                            {
                                "key": "logging",
                                "name": "日志设置",
                                "icon": QuiFontIcon.List
                            }
                        ]
                        delegate: Rectangle {
                            required property var modelData
                            width: parent.width
                            height: 40
                            radius: 4
                            color: root.page === modelData.key ? WorkbenchStyle.navItemActiveBg : "transparent"
                            RowLayout {
                                anchors.fill: parent
                                anchors.leftMargin: 5
                                anchors.rightMargin: 5
                                spacing: 5
                                QuiTextIcon {
                                    iconSource: modelData.icon
                                    iconSize: 15
                                    iconColor: root.page === modelData.key ? WorkbenchStyle.navItemActiveText : QuiColor.FontDark
                                }
                                QuiText {
                                    Layout.fillWidth: true
                                    text: modelData.name
                                    elide: Text.ElideRight
                                    color: root.page === modelData.key ? WorkbenchStyle.navItemActiveText : QuiColor.FontPrimary
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
                    anchors.leftMargin: 5
                    anchors.rightMargin: 5
                    anchors.topMargin: 5
                    spacing: 5

                    Row {
                        spacing: 5
                        Rectangle {
                            width: 3
                            height: 16
                            color: QuiColor.Highlight
                            anchors.verticalCenter: parent.verticalCenter
                        }
                        QuiText {
                            text: root.page === "builtins" ? "工具设置" : root.page === "logging" ? "日志设置" : "运行设置"
                            color: QuiColor.FontPrimary
                            font: QuiFont.BodyStrong
                        }
                    }

                    // Tool settings: whether the declaration shipped with the installation is listed
                    // by the custom tool window.
                    RowLayout {
                        width: parent.width
                        visible: root.page === "builtins"
                        spacing: 5
                        QuiText {
                            Layout.preferredWidth: ParameterStyle.parameterLabelWidth
                            horizontalAlignment: Text.AlignRight
                verticalAlignment: Text.AlignVCenter
                Layout.fillHeight: true
                            text: "显示内置工具"
                            color: QuiColor.FontDark
                            font: QuiFont.Body
                        }
                        QuiToggleSwitch {
                            checked: root.draftBuiltins
                            clickListener: function () {
                                root.draftBuiltins = !root.draftBuiltins;
                            }
                        }
                        Item {
                            Layout.fillWidth: true
                        }
                    }

                    // Runtime settings: the external interpreter, chosen through one file-or-folder
                    // picker or typed. Empty means the next start runs without Python.
                    Column {
                        width: parent.width
                        visible: root.page === "runtime"
                        spacing: 5
                        RowLayout {
                            width: parent.width
                            spacing: 5
                            QuiText {
                                Layout.preferredWidth: ParameterStyle.parameterLabelWidth
                                Layout.alignment: Qt.AlignVCenter
                                horizontalAlignment: Text.AlignRight
                verticalAlignment: Text.AlignVCenter
                Layout.fillHeight: true
                                text: "Python 环境"
                                color: QuiColor.FontDark
                                font: QuiFont.Body
                            }
                            ParameterTextField {
                                id: pythonField
                                Layout.fillWidth: true
                                Layout.maximumWidth: 420
                                flat: false
                                readOnly: root.applying
                                actionIcon: QuiFontIcon.Folder
                                onActionRequested: pythonPicker.open()
                                Connections {
                                    target: pythonField.inputItem
                                    function onTextEdited() {
                                        root.draftPython = pythonField.text;
                                        if (root.errorField === "runtime")
                                            root.errorText = "";
                                    }
                                }
                            }
                            Item {
                                Layout.fillWidth: true
                            }
                        }
                        QuiText {
                            x: pythonField.x
                            visible: root.pythonPending && !root.applying
                            text: "Python 环境在下次启动时生效。"
                            color: QuiColor.FontDark
                            font: QuiFont.Caption
                        }
                        QuiText {
                            x: pythonField.x
                            visible: root.applying
                            text: "正在检查所选 Python 环境..."
                            color: QuiColor.FontDark
                            font: QuiFont.Caption
                        }
                        // One status, read from the host: the check result is never re-derived here.
                        QuiText {
                            x: pythonField.x
                            width: parent.width - pythonField.x
                            visible: !root.applying && !root.pythonPending
                            text: "当前环境：" + app.pythonEnvironmentText
                            color: root.environmentRejected ? QuiColor.Error : QuiColor.FontDark
                            wrapMode: Text.WordWrap
                            font: QuiFont.Caption
                        }
                    }

                    // Log settings: one production threshold for the file and the window.
                    RowLayout {
                        width: parent.width
                        visible: root.page === "logging"
                        spacing: 5
                        QuiText {
                            Layout.preferredWidth: ParameterStyle.parameterLabelWidth
                            horizontalAlignment: Text.AlignRight
                verticalAlignment: Text.AlignVCenter
                Layout.fillHeight: true
                            text: "日志等级"
                            color: QuiColor.FontDark
                            font: QuiFont.Body
                        }
                        ParameterComboBox {
                            id: levelBox
                            Layout.preferredWidth: 200
                            model: root.levelNames
                            currentIndex: Math.max(0, root.levelKeys.indexOf(root.draftLevel))
                            onActivated: root.draftLevel = root.levelKeys[currentIndex]
                        }
                        Item {
                            Layout.fillWidth: true
                        }
                    }

                    QuiText {
                        width: parent.width
                        visible: root.errorText.length > 0
                        text: root.errorText
                        color: QuiColor.Error
                        wrapMode: Text.WordWrap
                        font: QuiFont.Body
                    }
                }
            }
        }
    }
    footer: Component {
        RowLayout {
            anchors.fill: parent
            spacing: 5
            Item {
                Layout.fillWidth: true
            }
            QuiButton {
                objectName: "settingsConfirm"
                text: "确定"
                enabled: !root.applying
                onClicked: root.apply(true)
            }
            QuiButton {
                objectName: "settingsCancel"
                text: "取消"
                // A commit in flight owns the window until it answers; closing now would hide a
                // write the user cannot see.
                enabled: !root.applying
                onClicked: root.close()
            }
            QuiFilledButton {
                objectName: "settingsApply"
                text: root.applying ? "检查中" : "应用"
                enabled: root.dirty && !root.applying
                onClicked: root.apply(false)
            }
        }
    }
    onClosing: function (event) {
        if (applying && !applicationClosing)
            event.accepted = false;
    }
    QuiFileFolderDialog {
        id: pythonPicker
        title: "选择 Python 解释器"
        dialogMode: QuiFileFolderDialog.DialogMode.FileOrFolder
        onAccepted: function (path) {
            root.draftPython = path;
            bodyItem.pythonText = path;
            if (root.errorField === "runtime")
                root.errorText = "";
        }
    }
}
