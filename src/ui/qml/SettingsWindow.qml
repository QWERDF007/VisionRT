import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import quickui
import VisionRT.Ui 1.0

WorkbenchWindow {
    id: root
    objectName: "settingsWindow"
    title: "平台设置"
    width: 1280
    height: 720
    closeEnabled: !applying
    // Applied state, reloaded whenever the window opens or an apply succeeds. The draft below is the
    // only thing the controls edit.
    property var applied: ({
            "editBuiltins": false,
            "loggingLevel": LogLevel.Info,
            "pythonExecutable": ""
        })
    property bool draftBuiltins: false
    property int draftLevel: LogLevel.Info
    property string draftPython: ""
    property int page: SettingsPage.Runtime
    property string errorText: ""
    property int errorField: SettingsPage.None
    // The one commit asks the controller for an answer instead of receiving it: a changed
    // interpreter is checked off the GUI thread, so the outcome arrives as settingsApplied.
    property bool closeAfterApply: true

    readonly property string appliedPython: applied.pythonExecutable ? applied.pythonExecutable : ""
    readonly property bool dirty: draftBuiltins !== applied.editBuiltins || draftLevel !== applied.loggingLevel || draftPython !== appliedPython
    readonly property bool pythonPending: draftPython !== appliedPython
    readonly property bool applying: app.settingsApplying

    function load() {
        applied = app.settingsSnapshot();
        draftBuiltins = applied.editBuiltins;
        draftLevel = applied.loggingLevel;
        draftPython = appliedPython;
        errorText = app.settingsError;
        errorField = app.settingsErrorField;
        page = errorField !== SettingsPage.None ? errorField : SettingsPage.Runtime;
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
            if (errorField !== SettingsPage.None)
                page = errorField;
            return;
        }
        applied = app.settingsSnapshot();
        draftPython = appliedPython;
        bodyItem.pythonText = draftPython;
        errorText = "";
        errorField = SettingsPage.None;
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
            if (errorField !== SettingsPage.None)
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
        SettingsContent {
            anchors.fill: parent
            draftBuiltins: root.draftBuiltins
            draftLevel: root.draftLevel
            page: root.page
            applying: root.applying
            pythonPending: root.pythonPending
            errorText: root.errorText
            onBuiltinsEdited: function (value) {
                root.draftBuiltins = value;
            }
            onLevelEdited: function (value) {
                root.draftLevel = value;
            }
            onPythonEdited: function (value) {
                root.draftPython = value;
                if (root.errorField === SettingsPage.Runtime)
                    root.errorText = "";
            }
            onPageRequested: function (value) {
                root.page = value;
            }
            onBrowsePythonRequested: pythonPicker.open()
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
            if (root.errorField === SettingsPage.Runtime)
                root.errorText = "";
        }
    }
}
