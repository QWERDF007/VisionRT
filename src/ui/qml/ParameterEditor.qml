import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import QtQuick.Dialogs
import quickui
import VisionRT.Style 1.0

ColumnLayout {
    id: root
    required property var parameter
    property string nodeId: app.selectedNode
    property var panel: null
    objectName: "parameter_" + parameter.id
    readonly property bool isMeta: !!parameter.isMeta
    readonly property bool alwaysUpstream: parameter.type === "Image"
    readonly property bool guardRow: parameter.editor === "guard"
    readonly property bool conditionRow: parameter.editor === "condition" || parameter.editor === "condition_upstream"
    // The source button only reports the input mode. It never infers the mode from a
    // source, from the guard text or from this round's success.
    readonly property bool upstream: parameter.inputMode === "upstream"
    // Condition rows edit the single `condition` port, ordinary rows their own port.
    readonly property string bindingPort: root.conditionRow ? "condition" : root.parameter.id
    readonly property bool editable: root.enabled && nodeId === app.selectedNode && !app.busy
    readonly property string valueText: parameter.valueText === undefined ? parameter.literal : parameter.valueText
    readonly property string parameterKey: nodeId + (parameter.init ? ":init:" : ":input:") + parameter.id
    readonly property string pathKind: parameter.editor || ""
    readonly property real numberValue: Number(root.valueText)
    readonly property real minimumValue: parameter.minimum === undefined || parameter.minimum === "" ? -Infinity : Number(parameter.minimum)
    readonly property real maximumValue: parameter.maximum === undefined || parameter.maximum === "" ? Infinity : Number(parameter.maximum)
    readonly property real numberStep: parameter.step === undefined || parameter.step === "" ? 1 : Number(parameter.step)
    readonly property int decimalPlaces: parameter.decimals === undefined || parameter.decimals < 0 ? 6 : parameter.decimals
    readonly property int labelWidth: 96
    readonly property int actionWidth: Theme.paramActionBtnWidth
    readonly property font editorFont: Theme.parameterFont
    spacing: Theme.parameterRowSpacing

    function getPanel() {
        if (root.panel) return root.panel
        let item = root.parent
        while (item) {
            if (typeof item.openExpressionEditor === "function") return item
            item = item.parent
        }
        return null
    }

    function commit(text) { return root.editable && app.setParameter(parameter.id, parameter.init, text) }
    onPathKindChanged: { pathPicker.close() }
    onParameterKeyChanged: { pathPicker.close() }

    RowLayout {
        Layout.fillWidth: true
        spacing: 6
        implicitHeight: Theme.parameterRowHeight

        QuiText {
            objectName: "parameterLabel"
            text: root.parameter.name; Layout.preferredWidth: root.labelWidth
            Layout.alignment: Qt.AlignVCenter
            font: Theme.parameterFont
            color: Theme.parameterTextColor
            elide: Text.ElideRight
            MouseArea { id: nameHover; anchors.fill: parent; hoverEnabled: true; acceptedButtons: Qt.NoButton }
            QuiToolTip {
                visible: nameHover.containsMouse && text.length > 0
                text: {
                    let name = root.parameter.name || ""
                    let typeText = root.parameter.typeDisplay || root.parameter.typeDetail || root.parameter.type || ""
                    return typeText.length > 0 ? (name + "（" + typeText + "）") : name
                }
                delay: 500
            }
        }

        Rectangle {
            id: controlFrame
            Layout.fillWidth: true
            Layout.minimumWidth: 70
            Layout.preferredHeight: Theme.paramEditorHeight
            Layout.alignment: Qt.AlignVCenter
            radius: 4
            color: frameHover.hovered ? Theme.paramRowHoverBg : Theme.paramEditorBg
            clip: true

            // Validation never paints the field red: an unnormalisable draft stays visible and is
            // reported to the log, while the committed value keeps its own state.
            readonly property color frameBorderColor: (frameHover.hovered || isEditorFocused)
                    ? Theme.paramEditorFocusBorder : Theme.paramEditorBorder

            readonly property bool isEditorFocused: {
                if (!editorLoader.item) return false
                if (editorLoader.item.activeFocus) return true
                if (editorLoader.item.inputItem && editorLoader.item.inputItem.activeFocus) return true
                return false
            }

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
                        sourceComponent: (root.isMeta && root.upstream && root.conditionRow) ? upstreamEditor
                                       : root.isMeta ? metaEditor
                                       : root.upstream ? upstreamEditor
                                       : (root.parameter.options || []).length > 0 ? choiceEditor
                                       : (root.parameter.type === "Bool" && (root.parameter.editor === "switch" || root.parameter.id === "reload")) ? toggleSwitchComponent
                                       : root.parameter.type === "Bool" ? boolEditor
                                       : root.parameter.type === "Int" ? integerEditor
                                       : root.parameter.type === "Float" ? floatEditor
                                       : (root.parameter.type === "ROI" && root.parameter.editor === "rect") ? rectEditor
                                       : root.parameter.type === "ROI" ? regionsEditor
                                       : root.parameter.type === "Array" ? arrayEditor
                                       : textEditor
                    }
                }

                Rectangle {
                    id: toggleDivider
                    visible: toggleAction.visible
                    Layout.preferredWidth: 1
                    Layout.fillHeight: true
                    color: Theme.paramEditorBorder
                }

                Rectangle {
                    id: toggleAction
                    objectName: "parameterActions"
                    visible: !root.parameter.init && !root.alwaysUpstream && (!root.isMeta || root.guardRow || root.conditionRow)
                    Layout.preferredWidth: Theme.paramActionBtnWidth
                    Layout.fillHeight: true
                    radius: Theme.radiusSmall
                    color: root.upstream ? Theme.sourceButtonActiveBackground
                                        : (toggleMouse.containsMouse ? Theme.paramRowHoverBg : "transparent")
                    border.width: root.upstream ? 1 : 0
                    border.color: Theme.sourceButtonActiveBorder
                    opacity: root.editable ? 1.0 : Theme.sourceButtonDisabledOpacity

                    UpstreamToggleIcon {
                        anchors.centerIn: parent
                        iconSize: Theme.paramToggleIconSize
                        iconColor: root.upstream ? Theme.sourceButtonActiveIcon : Theme.sourceButtonIdleIcon
                    }

                    MouseArea {
                        id: toggleMouse
                        anchors.fill: parent
                        hoverEnabled: true
                        cursorShape: Qt.PointingHandCursor
                        enabled: root.editable
                        onClicked: {
                            if (root.guardRow) {
                                app.setExecutionGuardMode(root.nodeId, !root.upstream)
                            } else if (root.conditionRow) {
                                app.setConditionMode(root.nodeId, !root.upstream)
                            } else {
                                app.setParameterMode(root.parameter.id, !root.upstream)
                            }
                        }
                    }

                    Accessible.name: root.guardRow ? "执行条件输入来源"
                                   : root.conditionRow ? "条件公式输入来源" : "参数输入来源"
                    Accessible.checkable: true
                    Accessible.checked: root.upstream

                    QuiToolTip {
                        visible: toggleMouse.containsMouse
                        text: {
                            if (root.guardRow) {
                                return root.upstream ? "切换为始终执行（用户输入）" : "绑定上层输入（执行条件）"
                            }
                            if (root.conditionRow) {
                                return root.upstream ? "切换为条件公式" : "使用上层布尔值"
                            }
                            return root.upstream ? "切换为直接输入" : "绑定上层输入"
                        }
                        delay: 300
                    }
                }
            }

            Rectangle {
                id: borderOverlay
                anchors.fill: parent
                radius: 4
                color: "transparent"
                border.width: 1
                border.color: controlFrame.frameBorderColor
                z: 10
            }

            QuiFileFolderDialog {
                id: pathPicker
                title: root.parameter.editor === "directory"
                           ? "选择文件夹"
                           : (root.parameter.editor === "file"
                               ? "选择文件"
                               : "选择文件或文件夹")
                currentFolder: root.valueText
                dialogMode: root.parameter.editor === "directory"
                                ? QuiFileFolderDialog.DialogMode.Folder
                                : (root.parameter.editor === "file"
                                    ? QuiFileFolderDialog.DialogMode.File
                                    : QuiFileFolderDialog.DialogMode.FileOrFolder)
                onAccepted: (path) => {
                    if (root.editable)
                        app.setParameterPath(root.parameter.id, root.parameter.init, path)
                }
            }
        }

    }

    function currentUpstreamValue() {
        if (root.parameter.sourceNode && root.parameter.sourcePort) {
            return String(root.parameter.sourceNode) + ":" + String(root.parameter.sourcePort)
        }
        return ""
    }

    function buildBindingGroups() {
        let candidates = app.bindingCandidates(app.portContext(root.bindingPort))
        let groups = []
        if (root.parameter.source) {
            groups.push({
                label: "解除绑定",
                value: "__unbind__",
                subOptions: []
            })
        }
        // An empty candidate list stays empty: no placeholder row is invented for it.
        if (!candidates || candidates.length === 0)
            return groups
        let nodeMap = {}
        for (let i = 0; i < candidates.length; ++i) {
            let c = candidates[i]
            let key = String(c.node)
            if (!nodeMap[key]) {
                let nodeEntry = {
                    label: c.title,
                    value: "",
                    subOptions: []
                }
                nodeMap[key] = nodeEntry
                groups.push(nodeEntry)
            }
            nodeMap[key].subOptions.push({
                label: c.name + (c.type ? " (" + c.type + ")" : ""),
                value: String(c.node) + ":" + String(c.port)
            })
        }
        return groups
    }

    Component {
        id: upstreamEditor
        QuiGroupComboBox {
            id: groupCombo
            flat: true
            property var bindingContext: ({})
            font: root.editorFont
            objectName: "parameterUpstream"
            implicitHeight: Theme.paramEditorHeight
            savedValue: root.currentUpstreamValue()
            Accessible.name: root.parameter.name
            displayText: root.parameter.source || ""
            optionGroups: root.buildBindingGroups()
            onAboutToOpen: {
                bindingContext = app.portContext(root.bindingPort)
                optionGroups = root.buildBindingGroups()
            }
            commitValue: function(val) {
                if (!root.editable || !app.portContextCurrent(bindingContext)) return false
                if (!val || val === "__unbind__") {
                    return app.unbindInput(bindingContext)
                } else if (val !== "__none__") {
                    let sep = val.indexOf(":")
                    if (sep > 0) {
                        let targetNode = val.slice(0, sep)
                        let targetPort = val.slice(sep + 1)
                        return app.bindInput(bindingContext, targetNode, targetPort)
                    }
                }
                return false
            }
            function validateSession() {
                if (popup.visible && (!root.editable || !app.portContextCurrent(bindingContext))) popup.close()
            }
            HoverHandler { id: upstreamHover }
            QuiToolTip {
                visible: upstreamHover.hovered && text.length > 0
                text: root.parameter.source || ""
                delay: 500
            }
            Connections {
                target: app
                function onSelectionChanged() { groupCombo.validateSession() }
                function onNodesChanged() { groupCombo.validateSession() }
                function onParametersChanged() { groupCombo.validateSession() }
                function onRuntimeChanged() { groupCombo.validateSession() }
                function onResultsChanged() { groupCombo.validateSession() }
            }
        }
    }
    Component {
        id: integerEditor
        IntegerParameterEditor {
            parameterId: root.parameter.id
            flat: true
            font: root.editorFont
            valueText: root.valueText
            canDecrement: root.parameter.canDecrement === true
            canIncrement: root.parameter.canIncrement === true
            commitValue: function(text) { return root.commit(text) }
            stepValue: function(direction, text) {
                return root.editable
                    && app.stepIntegerParameter(root.parameter.id, root.parameter.init, direction, text)
            }
        }
    }
    Component {
        id: floatEditor
        QuiSpinEditor {
            id: spin
            flat: true
            font: root.editorFont
            label: ""
            decimals: root.decimalPlaces
            step: root.numberStep > 0 ? root.numberStep : 1
            minValue: root.minimumValue
            maxValue: root.maximumValue
            Component.onCompleted: spin.value = root.numberValue
            commitValue: function(number) { return root.commit(String(number)) }
            // The shared editor repaints itself red on a rejected draft; parameter rows keep one
            // text colour and leave the draft in place for correction.
            onInvalidChanged: if (invalid) {
                app.logParameterDraftRejected(root.parameter.id + "：" + text)
                invalid = false
            }
            Connections {
                target: root
                function onValueTextChanged() { spin.value = root.numberValue; spin.invalid = false }
                function onParameterKeyChanged() { spin.value = root.numberValue; spin.invalid = false }
            }
        }
    }
    Component {
        id: boolEditor
        QuiComboBox {
            flat: true
            font: root.editorFont
            implicitHeight: Theme.paramEditorHeight
            readonly property bool isRecursive: root.parameter.id === "recursive"
            model: isRecursive ? ["不递归", "递归"] : ["是", "否"]
            currentIndex: {
                let val = (root.parameter.literal === "true" || root.parameter.value === true || root.parameter.value === "true")
                if (isRecursive) return val ? 1 : 0
                return val ? 0 : 1
            }
            onActivated: function(index) {
                let isTrue = isRecursive ? (index === 1) : (index === 0)
                root.commit(isTrue ? "true" : "false")
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
                anchors.leftMargin: 8
                anchors.verticalCenter: parent.verticalCenter
                checked: (root.parameter.literal === "true" || root.parameter.value === true || root.parameter.value === "true")
                enabled: root.editable
                clickListener: function() {
                    let nextVal = !sw.checked
                    sw.checked = nextVal
                    root.commit(nextVal ? "true" : "false")
                }
            }
        }
    }
    Component {
        id: choiceEditor
        QuiComboBox {
            id: choice
            flat: true
            font: root.editorFont
            implicitHeight: Theme.paramEditorHeight
            model: root.parameter.options
            textRole: "label"
            currentIndex: {
                for (let index = 0; index < model.length; ++index)
                    if (model[index].value === root.valueText) return index
                return -1
            }
            displayText: currentIndex < 0 ? root.valueText : model[currentIndex].label
            onActivated: function(index) { root.commit(model[index].value) }
            HoverHandler { id: choiceHover }
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
            font: Theme.monoFont
            actionText: {
                if (root.parameter.editor === "guard") return "设置执行条件"
                if (root.parameter.editor === "select_type") return "选择合流类型"
                if (root.parameter.editor === "condition") return "编辑条件公式"
                return "编辑公式"
            }
            property string committedText: ""
            // hasDraft records that the field holds user text, including an empty string. Without
            // it an empty draft is indistinguishable from "never edited" and would reload the old
            // formula on the next edit or cancel.
            property bool hasDraft: false
            onTextChanged: if (inputItem.activeFocus) hasDraft = true
            function sync(force) {
                const currentText = root.parameter.displayText || ""
                committedText = currentText
                // An existing draft survives parameter refreshes: clearing the field and opening
                // the formula editor must not silently restore the committed formula.
                if (force || (!inputItem.activeFocus && !hasDraft)) {
                    text = committedText
                    invalid = false
                    hasDraft = false
                }
            }
            Component.onCompleted: sync(true)
            onCommitRequested: function(commitText) {
                if (root.parameter.isSingleFormula) {
                    let ok = app.applySingleExpression(root.nodeId, commitText)
                    invalid = !ok
                    if (ok) { committedText = commitText; hasDraft = false }
                }
            }
            onCancelRequested: {
                text = committedText
                invalid = false
                hasDraft = false
            }
            onActionRequested: {
                let p = root.getPanel()
                if (!p) return
                if (root.parameter.editor === "guard") {
                    p.openGuardEditor(root.nodeId)
                } else if (root.parameter.editor === "select_type") {
                    p.openSelectTypeEditor(root.nodeId)
                } else {
                    p.openExpressionEditor(root.nodeId, field.text,
                                           field.hasDraft || field.text !== field.committedText)
                }
            }
            Connections {
                target: root
                function onParameterChanged() { field.sync(false) }
                function onValueTextChanged() { field.sync(false) }
                function onParameterKeyChanged() { field.sync(true) }
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
                const dirty = text !== committedText
                committedText = root.valueText
                if (force || !inputItem.activeFocus || !dirty) {
                    text = committedText
                    invalid = false
                }
            }
            Component.onCompleted: sync(true)
            // Path parameters use a unified picker dialog supporting both files and folders.
            actionIcon: root.parameter.editor === "file"
                            ? QuiFontIcon.Document
                            : (root.parameter.editor === "directory" || root.parameter.editor === "path"
                                ? QuiFontIcon.FolderOpen
                                : 0)
            actionText: root.parameter.editor === "directory" ? "选择文件夹"
                      : (root.parameter.editor === "file" ? "选择文件"
                      : (root.parameter.editor === "path" ? "选择路径" : ""))
            secondaryActionIcon: 0
            secondaryActionText: ""
            onCommitRequested: {
                if (text === root.valueText) {
                    invalid = false
                    return
                }
                invalid = !root.commit(text)
            }
            onCancelRequested: {
                text = root.valueText
                invalid = false
            }
            onActionRequested: pathPicker.open()
            Connections {
                target: root
                function onValueTextChanged() { field.sync(false) }
                function onParameterKeyChanged() { field.sync(true) }
            }
        }
    }

    Component {
        id: roiRectIconDelegate
        RoiRectIcon {
            anchors.centerIn: parent
            iconSize: Theme.paramRoiIconSize
            iconColor: parent && parent.iconColor ? parent.iconColor : Theme.textPrimary
        }
    }

    Component {
        id: rectEditor
        ParameterTextField {
            id: field
            backgroundColor: Theme.paramInputReadOnlyBg
            readonly property bool hasRect: root.parameter.hasRect === true
            readonly property bool isDrawing: app.activeRoiParameter === root.parameter.id && app.activeRoiShape === 2

            leadingActionDelegate: roiRectIconDelegate
            leadingActionText: isDrawing ? "正在绘制..." : "画矩形"
            leadingActionActive: isDrawing

            actionIcon: hasRect ? QuiFontIcon.Clear : 0
            actionText: "清空矩形"

            onLeadingActionRequested: {
                if (isDrawing) {
                    app.endRoiPreview()
                } else {
                    app.startRoiDraw(root.parameter.id, 2)
                }
            }
            onActionRequested: app.clearRoi(root.parameter.id)

            function sync(force) {
                const currentText = hasRect ? (root.parameter.displayText || "[]") : "[]"
                if (force || !inputItem.activeFocus) {
                    text = currentText
                    invalid = false
                }
            }

            Component.onCompleted: sync(true)

            onCommitRequested: {
                if (text === "[]" || text.trim() === "" || text === "null" || text === "~") {
                    app.clearRoi(root.parameter.id)
                    invalid = false
                    return
                }
                invalid = !root.commit(text)
            }

            onCancelRequested: sync(true)

            Connections {
                target: root
                function onParameterChanged() { field.sync(false) }
                function onValueTextChanged() { field.sync(false) }
                function onParameterKeyChanged() { field.sync(true) }
            }
        }
    }

    Component {
        id: regionsEditor
        ParameterTextField {
            id: field
            backgroundColor: Theme.paramInputReadOnlyBg
            readonly property int count: root.parameter.roiCount !== undefined ? root.parameter.roiCount : 0
            readonly property bool hasRois: count > 0
            readonly property bool isDrawing: app.activeRoiParameter === root.parameter.id && app.activeRoiShape > 0

            leadingActionDelegate: roiRectIconDelegate
            leadingActionText: isDrawing ? "正在绘制..." : "画矩形"
            leadingActionActive: isDrawing

            actionIcon: hasRois ? QuiFontIcon.Clear : 0
            actionText: "清空区域"

            onLeadingActionRequested: {
                if (isDrawing) {
                    app.endRoiPreview()
                } else {
                    app.startRoiDraw(root.parameter.id, 2)
                }
            }
            onActionRequested: app.clearRoi(root.parameter.id)

            MouseArea {
                anchors.fill: parent
                acceptedButtons: Qt.RightButton
                onClicked: shapeMenu.open()
            }

            Menu {
                id: shapeMenu
                MenuItem {
                    text: "画矩形"
                    onTriggered: app.startRoiDraw(root.parameter.id, 2)
                }
                MenuItem {
                    text: "画圆"
                    onTriggered: app.startRoiDraw(root.parameter.id, 3)
                }
                MenuItem {
                    text: "画线"
                    onTriggered: app.startRoiDraw(root.parameter.id, 1)
                }
                MenuItem {
                    text: "画多边形"
                    onTriggered: app.startRoiDraw(root.parameter.id, 4)
                }
                MenuItem {
                    text: "画点集"
                    onTriggered: app.startRoiDraw(root.parameter.id, 5)
                }
            }

            function sync(force) {
                const currentText = hasRois ? ("[" + count + " 个区域]") : "[]"
                if (force || !inputItem.activeFocus) {
                    text = currentText
                    invalid = false
                }
            }

            Component.onCompleted: sync(true)

            onCommitRequested: {
                if (text === "[]" || text.trim() === "" || text === "null" || text === "~") {
                    app.clearRoi(root.parameter.id)
                    invalid = false
                    return
                }
                invalid = !root.commit(text)
            }

            onCancelRequested: sync(true)
            onCountChanged: sync(false)

            Connections {
                target: root
                function onParameterChanged() { field.sync(false) }
                function onValueTextChanged() { field.sync(false) }
                function onParameterKeyChanged() { field.sync(true) }
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
                text = "[]"
                committedText = "[]"
                root.commit("[]")
            }

            function sync(force) {
                const currentText = (root.valueText && root.valueText !== "null") ? root.valueText : "[]"
                committedText = currentText
                if (force || !inputItem.activeFocus) {
                    text = committedText
                    invalid = false
                }
            }

            Component.onCompleted: sync(true)

            onCommitRequested: {
                let trimmed = text.trim()
                if (trimmed === "" || trimmed === "[]" || trimmed === "null" || trimmed === "~") {
                    trimmed = "[]"
                    text = "[]"
                }
                if (trimmed === committedText) {
                    invalid = false
                    return
                }
                invalid = !root.commit(trimmed)
                if (!invalid) {
                    committedText = trimmed
                }
            }

            onCancelRequested: sync(true)

            Connections {
                target: root
                function onValueTextChanged() { field.sync(false) }
                function onParameterKeyChanged() { field.sync(true) }
            }
        }
    }
}
