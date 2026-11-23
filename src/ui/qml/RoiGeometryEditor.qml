import QtQuick
import QtQuick.Layouts
import quickui
import VisionRT.Ui 1.0

Flow {
    id: root
    property var editor: app.roiEditor
    property var labels: []
    function syncLabels() {
        const next = editor.labels || [];
        if (next.length !== labels.length || next.some(function(label, index) { return label !== root.labels[index]; }))
            labels = next;
    }
    onEditorChanged: syncLabels()
    Component.onCompleted: syncLabels()
    spacing: VisionRTSize.Spacing
    visible: editor.coordinates !== undefined
    enabled: editor.readOnly !== true
    Repeater {
        model: root.labels
        RowLayout {
            required property int index
            required property string modelData
            spacing: VisionRTSize.Spacing
            QuiText { text: modelData; font: QuiFont.Caption }
            ParameterTextField {
                id: field
                objectName: "roiCoordinate" + index
                implicitWidth: 85
                property bool syncing: false
                property bool canceling: false
                function sync() {
                    if (!inputItem.activeFocus) {
                        syncing = true;
                        text = String(root.editor.coordinates[index]);
                        invalid = false;
                        syncing = false;
                    }
                }
                Component.onCompleted: sync()
                Connections {
                    target: root
                    function onEditorChanged() { field.sync(); }
                }
                Connections {
                    target: field.inputItem
                    function onTextEdited() {
                        if (!field.syncing)
                            field.invalid = !app.setRoiCoordinate(root.editor.id, index, field.text, false);
                    }
                }
                onCommitRequested: {
                    if (!canceling) invalid = !app.setRoiCoordinate(root.editor.id, index, text, true);
                }
                onCancelRequested: {
                    canceling = true;
                    app.cancelRoiCoordinates();
                    inputItem.focus = false;
                    sync();
                    canceling = false;
                }
            }
        }
    }
}
