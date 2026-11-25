import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import QtQuick.Dialogs
import quickui
import VisionRT.Ui 1.0

ColumnLayout {
    id: root
    required property var parameter
    property string nodeId: app.selectedNode
    signal expressionEditorRequested(string node, string draft, bool hasDraft)
    signal selectTypeEditorRequested(string node)
    objectName: "parameter_" + parameter.id
    readonly property bool isMeta: !!parameter.isMeta
    readonly property bool conditionRow: parameter.editor === EditorKind.Condition || parameter.editor === EditorKind.ConditionUpstream
    // The source button only reports the input mode. It never infers the mode from a
    // source, from a retained binding or this round's success.
    readonly property bool upstream: parameter.inputMode === BindingMode.Upstream
    readonly property int bindingPort: root.conditionRow ? root.parameter.bindingPort : root.parameter.id
    readonly property bool editable: root.enabled && nodeId === app.selectedNode && app.workspaceEnabled && (!parameter.recordScoped || !!parameter.recordId)
    readonly property string valueText: parameter.valueText === undefined ? parameter.literal : parameter.valueText
    readonly property string displayText: parameter.displayText === undefined ? root.valueText : parameter.displayText
    readonly property string parameterKey: nodeId + ":" + parameter.stage + ":" + !!parameter.recordScoped + ":" + (parameter.recordScoped ? parameter.datasetId : "") + ":" + (parameter.recordId || "") + ":" + parameter.id
    readonly property real numberValue: Number(root.valueText)
    readonly property real minimumValue: parameter.minimum === undefined || parameter.minimum === "" ? -Infinity : Number(parameter.minimum)
    readonly property real maximumValue: parameter.maximum === undefined || parameter.maximum === "" ? Infinity : Number(parameter.maximum)
    readonly property real numberStep: parameter.step === undefined || parameter.step === "" ? 1 : Number(parameter.step)
    readonly property int decimalPlaces: parameter.decimals === undefined || parameter.decimals < 0 ? 6 : parameter.decimals
    readonly property int labelWidth: VisionRTSize.ParamLabelWidth
    spacing: 5

    function commit(text) {
        if (!root.editable) return false;
        if (parameter.stage === PortStage.Train)
            return parameter.recordId ? app.setTrainingField(parameter.recordId, parameter.id, text) : app.setTrainingParameter(parameter.id, text);
        return app.setParameter(parameter.id, parameter.init, text);
    }
    function clearRegions() {
        return commit("[]");
    }
    function drawRegion(shape) {
        return app.startParameterRoi(parameter.id, parameter.stage, shape, !!parameter.recordScoped);
    }
    onParameterKeyChanged: {
        pathPicker.close();
        imagePicker.close();
    }

    RowLayout {
        Layout.fillWidth: true
        spacing: 5

        QuiText {
            objectName: "parameterLabel"
            text: root.parameter.name
            Layout.preferredWidth: root.labelWidth
            Layout.fillHeight: true
            verticalAlignment: Text.AlignVCenter
            font: QuiFont.Body
            color: QuiColor.FontPrimary
            elide: Text.ElideRight
            MouseArea {
                id: nameHover
                anchors.fill: parent
                hoverEnabled: true
                acceptedButtons: Qt.NoButton
            }
            QuiToolTip {
                visible: nameHover.containsMouse && text.length > 0
                text: {
                    let name = root.parameter.name || "";
                    let typeText = root.parameter.typeDisplay || root.parameter.typeDetail || root.parameter.type || "";
                    return typeText.length > 0 ? (name + "（" + typeText + "）") : name;
                }
                delay: 500
            }
        }

        ParameterFieldFrame {
            id: controlFrame
            Layout.fillWidth: true
            Layout.minimumWidth: 70
            Layout.alignment: Qt.AlignVCenter
            hovered: frameHover.hovered
            inputItem: editorLoader.item && editorLoader.item.inputItem ? editorLoader.item.inputItem : editorLoader.item
            clip: true

            HoverHandler {
                id: frameHover
            }

            RowLayout {
                anchors.fill: parent
                spacing: 0

                Item {
                    id: editorContainer
                    Layout.fillWidth: true
                    Layout.fillHeight: true

                    Loader {
                        id: editorLoader
                        anchors.fill: parent
                        enabled: root.editable
                        sourceComponent: (root.isMeta && root.upstream && root.conditionRow) ? upstreamEditor : root.isMeta ? metaEditor : root.upstream ? upstreamEditor : root.parameter.datasetEditor ? datasetEditor : root.parameter.type === PortType.Image ? imageEditor : (root.parameter.options || []).length > 0 ? choiceEditor : (root.parameter.type === PortType.Bool && (root.parameter.editor === EditorKind.Switch || root.parameter.editor === EditorKind.Recursive)) ? toggleSwitchComponent : root.parameter.type === PortType.Bool ? boolEditor : root.parameter.type === PortType.Int ? integerEditor : root.parameter.type === PortType.Float ? floatEditor : (root.parameter.isShape && root.parameter.editor === EditorKind.Rect) ? rectEditor : root.parameter.isShape ? regionsEditor : root.parameter.type === PortType.Array || root.parameter.type === PortType.Direction || root.parameter.type === PortType.CoordinateFrame2D || root.parameter.type === PortType.TrainingSet ? arrayEditor : textEditor
                    }
                }

                Rectangle {
                    id: toggleDivider
                    visible: toggleAction.visible
                    Layout.preferredWidth: 1
                    Layout.fillHeight: true
                    color: QuiColor.Border
                }

                ParameterActionButton {
                    id: toggleAction
                    objectName: "parameterActions"
                    visible: root.parameter.stage === PortStage.Inputs && (!root.isMeta || root.conditionRow)
                    Layout.fillHeight: true
                    padding: 0
                    verticalPadding: 0
                    horizontalPadding: 0
                    focusPolicy: Qt.NoFocus
                    enabled: root.editable
                    opacity: root.editable ? 1.0 : 0.3
                    normalColor: "transparent"
                    hoverColor: QuiColor.ItemHover
                    pressedColor: Qt.darker(QuiColor.ItemHover, 1.05)
                    iconSource: QuiFontIcon.Upload
                    iconColor: root.upstream ? QuiColor.Highlight : (hovered ? QuiColor.FontPrimary : QuiColor.FontCaption)

                    onClicked: {
                        if (root.conditionRow) {
                            app.setConditionMode(root.nodeId, !root.upstream);
                        } else {
                            app.setParameterMode(root.parameter.id, !root.upstream);
                        }
                    }

                    Accessible.name: root.conditionRow ? "条件公式输入来源" : "参数输入来源"
                    Accessible.checkable: true
                    Accessible.checked: root.upstream

                    QuiToolTip {
                        visible: toggleAction.hovered
                        text: {
                            if (root.conditionRow) {
                                return root.upstream ? "切换为条件公式" : "使用上层布尔值";
                            }
                            return root.upstream ? "切换为直接输入" : "绑定上层输入";
                        }
                        delay: 300
                    }
                }
            }

            QuiFileFolderDialog {
                id: pathPicker
                title: "选择文件或文件夹"
                currentFolder: root.valueText
                dialogMode: QuiFileFolderDialog.DialogMode.FileOrFolder
                onAccepted: path => {
                    if (root.editable)
                        root.parameter.stage === PortStage.Train ? app.setTrainingParameterPath(root.parameter.id, path) : app.setParameterPath(root.parameter.id, root.parameter.init, path);
                }
            }
            FileDialog {
                id: imagePicker
                title: "选择图像"
                fileMode: FileDialog.OpenFile
                nameFilters: ["图像 (*.png *.jpg *.jpeg *.bmp *.tif *.tiff)", "所有文件 (*)"]
                onAccepted: app.loadParameterImage(root.parameter.id, root.parameter.stage, selectedFile, root.parameter.recordId || "")
            }
        }
    }

    function currentUpstreamValue() {
        if (root.parameter.sourceNode && root.parameter.sourcePort !== undefined) {
            return String(root.parameter.sourceNode) + ":" + String(root.parameter.sourcePort);
        }
        return "";
    }

    function buildBindingGroups() {
        let candidates = app.bindingCandidates(app.portContext(root.bindingPort));
        let groups = [];
        if (root.parameter.source) {
            groups.push({
                label: "解除绑定",
                value: "__unbind__",
                subOptions: []
            });
        }
        // An empty candidate list stays empty: no placeholder row is invented for it.
        if (!candidates || candidates.length === 0)
            return groups;
        let nodeMap = {};
        for (let i = 0; i < candidates.length; ++i) {
            let c = candidates[i];
            let key = String(c.node);
            if (!nodeMap[key]) {
                let nodeEntry = {
                    label: c.title,
                    value: "",
                    subOptions: []
                };
                nodeMap[key] = nodeEntry;
                groups.push(nodeEntry);
            }
            nodeMap[key].subOptions.push({
                label: c.name + (c.type ? " (" + c.type + ")" : ""),
                value: String(c.node) + ":" + String(c.port)
            });
        }
        return groups;
    }

    Component {
        id: upstreamEditor
        QuiGroupComboBox {
            id: groupCombo
            indicatorIconSize: VisionRTSize.IconSize
            flat: true
            property var bindingContext: ({})
            objectName: "parameterUpstream"
            implicitHeight: VisionRTSize.ContentSize
            savedValue: root.currentUpstreamValue()
            Accessible.name: root.parameter.name
            displayText: root.parameter.source || ""
            optionGroups: root.buildBindingGroups()
            onAboutToOpen: {
                bindingContext = app.portContext(root.bindingPort);
                optionGroups = root.buildBindingGroups();
            }
            commitValue: function (val) {
                if (!root.editable || !app.portContextCurrent(bindingContext))
                    return false;
                if (!val || val === "__unbind__") {
                    return app.unbindInput(bindingContext);
                } else if (val !== "__none__") {
                    let sep = val.indexOf(":");
                    if (sep > 0) {
                        let targetNode = val.slice(0, sep);
                        let targetPort = val.slice(sep + 1);
                        return app.bindInput(bindingContext, targetNode, targetPort);
                    }
                }
                return false;
            }
            function validateSession() {
                if (popup.visible && (!root.editable || !app.portContextCurrent(bindingContext)))
                    popup.close();
            }
            HoverHandler {
                id: upstreamHover
            }
            QuiToolTip {
                visible: upstreamHover.hovered && text.length > 0
                text: root.parameter.source || ""
                delay: 500
            }
            Connections {
                target: app
                function onSelectionChanged() {
                    groupCombo.validateSession();
                }
                function onNodesChanged() {
                    groupCombo.validateSession();
                }
                function onParametersChanged() {
                    groupCombo.validateSession();
                }
                function onRuntimeChanged() {
                    groupCombo.validateSession();
                }
                function onResultsChanged() {
                    groupCombo.validateSession();
                }
            }
        }
    }
    Component {
        id: integerEditor
        IntegerParameterEditor {
            parameterId: root.parameter.id
            flat: true
            valueText: root.valueText
            canDecrement: root.parameter.canDecrement === true
            canIncrement: root.parameter.canIncrement === true
            commitValue: function (text) {
                return root.commit(text);
            }
            onRejected: function (text) {
                app.logParameterDraftRejected(root.parameter.id + "：" + text);
            }
            stepValue: function (direction, text) {
                return root.editable && app.stepIntegerParameter(root.parameter.id, root.parameter.init, direction, text, root.parameter.stage, root.parameter.recordId || "");
            }
        }
    }
    Component {
        id: floatEditor
        QuiSpinEditor {
            id: spin
            buttonWidth: height
            iconSize: VisionRTSize.IconSize
            contentPadding: 0
            flat: true
            label: ""
            decimals: root.decimalPlaces
            step: root.numberStep > 0 ? root.numberStep : 1
            minValue: root.minimumValue
            maxValue: root.maximumValue
            Component.onCompleted: spin.value = root.numberValue
            commitValue: function (number) {
                return root.commit(String(number));
            }
            // The shared editor repaints itself red on a rejected draft; parameter rows keep one
            // text colour and leave the draft in place for correction.
            onInvalidChanged: if (invalid) {
                app.logParameterDraftRejected(root.parameter.id + "：" + text);
                invalid = false;
            }
            Connections {
                target: root
                function onValueTextChanged() {
                    spin.value = root.numberValue;
                    spin.invalid = false;
                }
                function onParameterKeyChanged() {
                    spin.value = root.numberValue;
                    spin.invalid = false;
                }
            }
        }
    }
    Component {
        id: boolEditor
        ParameterComboBox {
            flat: true
            implicitHeight: VisionRTSize.ContentSize
            readonly property bool isRecursive: root.parameter.editor === EditorKind.Recursive
            model: isRecursive ? ["不递归", "递归"] : ["是", "否"]
            currentIndex: {
                let val = (root.parameter.literal === "true" || root.parameter.value === true || root.parameter.value === "true");
                if (isRecursive)
                    return val ? 1 : 0;
                return val ? 0 : 1;
            }
            onActivated: function (index) {
                let isTrue = isRecursive ? (index === 1) : (index === 0);
                root.commit(isTrue ? "true" : "false");
            }
        }
    }

    Component {
        id: toggleSwitchComponent
        Item {
            anchors.fill: parent
            QuiToggleSwitch {
                id: sw
                anchors.left: parent.left
                anchors.leftMargin: 5
                anchors.verticalCenter: parent.verticalCenter
                checked: (root.parameter.literal === "true" || root.parameter.value === true || root.parameter.value === "true")
                enabled: root.editable
                clickListener: function () {
                    let nextVal = !sw.checked;
                    sw.checked = nextVal;
                    root.commit(nextVal ? "true" : "false");
                }
            }
        }
    }
    Component {
        id: choiceEditor
        ParameterComboBox {
            id: choice
            flat: true
            implicitHeight: VisionRTSize.ContentSize
            model: root.parameter.options
            textRole: "label"
            currentIndex: {
                for (let index = 0; index < model.length; ++index)
                    if (model[index].value === root.valueText)
                        return index;
                return -1;
            }
            displayText: currentIndex < 0 ? root.valueText : model[currentIndex].label
            onActivated: function (index) {
                root.commit(model[index].value);
            }
            HoverHandler {
                id: choiceHover
            }
            QuiToolTip {
                visible: choiceHover.hovered && text.length > 0 && Boolean(choice.contentItem && (choice.contentItem.truncated || choice.contentItem.contentWidth > choice.contentItem.width))
                text: choice.displayText || choice.currentText || ""
                delay: 500
            }
        }
    }
    Component {
        id: metaEditor
        ParameterDialogField {
            id: field
            flat: true
            readOnly: !root.parameter.isSingleFormula
            actionText: {
                if (root.parameter.editor === EditorKind.Select)
                    return "选择合流类型";
                if (root.parameter.editor === EditorKind.Condition)
                    return "编辑条件公式";
                return "编辑公式";
            }
            property string committedText: ""
            // hasDraft records that the field holds user text, including an empty string. Without
            // it an empty draft is indistinguishable from "never edited" and would reload the old
            // formula on the next edit or cancel.
            property bool hasDraft: false
            onTextChanged: if (inputItem.activeFocus)
                hasDraft = true
            function sync(force) {
                const currentText = root.parameter.displayText || "";
                committedText = currentText;
                // An existing draft survives parameter refreshes: clearing the field and opening
                // the formula editor must not silently restore the committed formula.
                if (force || (!inputItem.activeFocus && !hasDraft)) {
                    text = committedText;
                    invalid = false;
                    hasDraft = false;
                }
            }
            Component.onCompleted: sync(true)
            onCommitRequested: function (commitText) {
                if (root.parameter.isSingleFormula) {
                    let ok = app.applySingleExpression(root.nodeId, commitText);
                    invalid = !ok;
                    if (ok) {
                        committedText = commitText;
                        hasDraft = false;
                    }
                }
            }
            onCancelRequested: {
                text = committedText;
                invalid = false;
                hasDraft = false;
            }
            onActionRequested: {
                if (root.parameter.editor === EditorKind.Select) {
                    root.selectTypeEditorRequested(root.nodeId);
                } else {
                    root.expressionEditorRequested(root.nodeId, field.text, field.hasDraft || field.text !== field.committedText);
                }
            }
            Connections {
                target: root
                function onParameterChanged() {
                    field.sync(false);
                }
                function onValueTextChanged() {
                    field.sync(false);
                }
                function onParameterKeyChanged() {
                    field.sync(true);
                }
            }
        }
    }

    Component {
        id: imageEditor
        ParameterTextField {
            flat: true
            readOnly: true
            text: root.displayText
            actionIcon: QuiFontIcon.FolderOpen
            actionText: "选择图像"
            onActionRequested: imagePicker.open()
        }
    }
    Component {
        id: datasetEditor
        ParameterTextField {
            flat: true
            readOnly: true
            text: root.displayText
            actionIcon: QuiFontIcon.Edit
            actionText: "编辑训练集"
            secondaryActionIcon: QuiFontIcon.Add
            secondaryActionText: "添加记录"
            onSecondaryActionRequested: app.addTrainingRecord(root.parameter.id)
            onActionRequested: {
                app.selectTrainingDataset(root.parameter.id);
                app.trainingMode = true;
            }
        }
    }
    Component {
        id: textEditor
        ParameterTextField {
            id: field
            flat: true
            property string committedText: ""
            function sync(force) {
                const dirty = text !== committedText;
                committedText = root.valueText;
                if (force || !inputItem.activeFocus || !dirty) {
                    text = committedText;
                    invalid = false;
                }
            }
            Component.onCompleted: sync(true)
            // Path parameters use a unified picker dialog supporting both files and folders.
            // A path parameter has one picker: it selects a file or a folder in the same dialog.
            actionIcon: root.parameter.editor === EditorKind.Path ? QuiFontIcon.FolderOpen : 0
            actionText: root.parameter.editor === EditorKind.Path ? "选择路径" : ""
            secondaryActionIcon: 0
            secondaryActionText: ""
            onCommitRequested: {
                if (text === root.valueText) {
                    invalid = false;
                    return;
                }
                invalid = !root.commit(text);
            }
            onCancelRequested: {
                text = root.valueText;
                invalid = false;
            }
            onActionRequested: pathPicker.open()
            Connections {
                target: root
                function onValueTextChanged() {
                    field.sync(false);
                }
                function onParameterKeyChanged() {
                    field.sync(true);
                }
            }
        }
    }

    Component {
        id: rectEditor
        ParameterTextField {
            id: field
            property string committedText: ""
            backgroundColor: QuiColor.Hovered
            readonly property bool hasRect: root.parameter.hasRect === true
            readonly property bool isDrawing: app.activeRoiParameter === root.parameter.id && app.activeRoiShape === RoiDrawShape.DrawRectangle

            leadingActionIcon: QuiFontIcon.RectangularClipping
            leadingActionText: isDrawing ? "正在绘制..." : "画矩形"
            leadingActionActive: isDrawing

            actionIcon: hasRect ? QuiFontIcon.Clear : 0
            actionText: "清空矩形"

            onLeadingActionRequested: {
                if (isDrawing) {
                    app.endRoiPreview();
                } else {
                    root.drawRegion(RoiDrawShape.DrawRectangle);
                }
            }
            onActionRequested: root.clearRegions()

            function sync(force) {
                const currentText = hasRect ? (root.parameter.displayText || "[]") : "[]";
                if (force || !inputItem.activeFocus) {
                    text = currentText;
                    committedText = currentText;
                    invalid = false;
                }
            }

            Component.onCompleted: sync(true)

            onCommitRequested: {
                if (text === committedText)
                    return;
                if (text === "[]" || text.trim() === "" || text === "null" || text === "~") {
                    root.clearRegions();
                    invalid = false;
                    return;
                }
                invalid = !root.commit(text);
                if (!invalid)
                    committedText = text;
            }

            onCancelRequested: sync(true)

            Connections {
                target: root
                function onParameterChanged() {
                    field.sync(false);
                }
                function onValueTextChanged() {
                    field.sync(false);
                }
                function onParameterKeyChanged() {
                    field.sync(true);
                }
            }
        }
    }

    Component {
        id: regionsEditor
        ParameterTextField {
            id: field
            property string committedText: ""
            backgroundColor: QuiColor.Hovered
            readonly property int count: root.parameter.roiCount !== undefined ? root.parameter.roiCount : 0
            readonly property bool hasRois: count > 0
            readonly property bool isDrawing: app.activeRoiParameter === root.parameter.id && app.activeRoiShape > 0

            leadingActionIcon: QuiFontIcon.RectangularClipping
            leadingActionText: isDrawing ? "正在绘制..." : "画" + root.parameter.typeDisplay
            leadingActionActive: isDrawing

            actionIcon: hasRois ? QuiFontIcon.Clear : 0
            actionText: "清空区域"

            onLeadingActionRequested: {
                if (isDrawing) {
                    app.endRoiPreview();
                } else {
                    root.drawRegion(root.parameter.roiDrawShape);
                }
            }
            onActionRequested: root.clearRegions()

            function sync(force) {
                const currentText = root.displayText;
                if (force || !inputItem.activeFocus) {
                    text = currentText;
                    committedText = currentText;
                    invalid = false;
                }
            }

            Component.onCompleted: sync(true)

            onCommitRequested: {
                if (text === committedText)
                    return;
                if (text === "[]" || text.trim() === "" || text === "null" || text === "~") {
                    root.clearRegions();
                    invalid = false;
                    return;
                }
                invalid = !root.commit(text);
                if (!invalid)
                    committedText = text;
            }

            onCancelRequested: sync(true)
            onCountChanged: sync(false)

            Connections {
                target: root
                function onParameterChanged() {
                    field.sync(false);
                }
                function onValueTextChanged() {
                    field.sync(false);
                }
                function onParameterKeyChanged() {
                    field.sync(true);
                }
            }
        }
    }

    Component {
        id: arrayEditor
        ParameterTextField {
            id: field
            property string committedText: ""

            actionIcon: (text.length > 0 && text !== "[]") ? QuiFontIcon.Clear : 0
            actionText: "清空"
            onActionRequested: {
                text = "[]";
                committedText = "[]";
                root.commit(root.parameter.emptyLiteral || "[]");
            }

            function sync(force) {
                const currentText = (root.displayText && root.displayText !== "null") ? root.displayText : "[]";
                committedText = currentText;
                if (force || !inputItem.activeFocus) {
                    text = committedText;
                    invalid = false;
                }
            }

            Component.onCompleted: sync(true)

            onCommitRequested: {
                let trimmed = text.trim();
                if (trimmed === "" || trimmed === "[]" || trimmed === "null" || trimmed === "~") {
                    trimmed = "[]";
                    text = "[]";
                }
                if (trimmed === committedText) {
                    invalid = false;
                    return;
                }
                invalid = !root.commit(trimmed);
                if (!invalid) {
                    committedText = trimmed;
                }
            }

            onCancelRequested: sync(true)

            Connections {
                target: root
                function onValueTextChanged() {
                    field.sync(false);
                }
                function onParameterKeyChanged() {
                    field.sync(true);
                }
            }
        }
    }
}
