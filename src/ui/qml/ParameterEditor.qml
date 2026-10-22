import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import QtQuick.Dialogs
import quickui

ColumnLayout {
    id: root
    required property var parameter
    property string nodeId: app.selectedNode
    objectName: "parameter_" + parameter.id
    property string structureDraft: ""
    property string structureError: ""
    readonly property bool alwaysUpstream: parameter.type === "ImageBatch"
    readonly property bool upstream: (alwaysUpstream || parameter.mode === "upstream") && !parameter.init
    readonly property bool editable: root.enabled && nodeId === app.selectedNode && !app.busy
    readonly property string valueText: parameter.valueText === undefined ? parameter.literal : parameter.valueText
    readonly property bool structured: ["NumericArray", "StringArray", "RoiBatch", "Rect"].indexOf(parameter.type) >= 0
    readonly property string parameterKey: nodeId + (parameter.init ? ":init:" : ":input:") + parameter.id
    readonly property string pathKind: parameter.editor || ""
    readonly property real numberValue: Number(root.valueText)
    readonly property real minimumValue: parameter.minimum === undefined || parameter.minimum === "" ? -Infinity : Number(parameter.minimum)
    readonly property real maximumValue: parameter.maximum === undefined || parameter.maximum === "" ? Infinity : Number(parameter.maximum)
    readonly property real numberStep: parameter.step === undefined || parameter.step === "" ? 1 : Number(parameter.step)
    readonly property int decimalPlaces: parameter.decimals === undefined || parameter.decimals < 0 ? 6 : parameter.decimals
    readonly property int labelWidth: 96
    readonly property int actionWidth: Theme.paramActionBtnWidth
    readonly property font editorFont: Qt.font({ family: "Microsoft YaHei UI", pixelSize: Theme.paramEditorFontSize })
    spacing: 6

    function commit(text) { return root.editable && app.setParameter(parameter.id, parameter.init, text) }
    onPathKindChanged: { filePicker.close(); directoryPicker.close() }
    onParameterKeyChanged: { filePicker.close(); directoryPicker.close(); structureDialog.close() }

    RowLayout {
        Layout.fillWidth: true
        spacing: 6
        implicitHeight: Theme.paramEditorHeight

        QuiText {
            objectName: "parameterLabel"
            text: root.parameter.name; Layout.preferredWidth: root.labelWidth
            Layout.alignment: Qt.AlignVCenter
            font: Qt.font({ family: "Microsoft YaHei UI", pixelSize: Theme.paramLabelFontSize })
            color: Theme.paramLabelText
            elide: Text.ElideRight
            MouseArea { id: nameHover; anchors.fill: parent; hoverEnabled: true; acceptedButtons: Qt.NoButton }
            QuiToolTip {
                visible: nameHover.containsMouse && text.length > 0
                text: {
                    let t = root.parameter.name || ""
                    if (root.parameter.id && root.parameter.id !== root.parameter.name)
                        t += "(" + root.parameter.id + ")"
                    let typeText = root.parameter.typeDetail || root.parameter.type || ""
                    if (typeText.length > 0)
                        t += "(" + typeText + ")"
                    return t
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
            color: frameHover.hovered ? QuiColor.ItemHover : QuiColor.CardBackground
            clip: true

            readonly property color frameBorderColor: {
                if (editorLoader.item && editorLoader.item.invalid)
                    return QuiColor.Error
                if (frameHover.hovered || isEditorFocused)
                    return QuiColor.Highlight
                return QuiColor.CardBorder
            }

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
                        sourceComponent: root.upstream ? upstreamEditor
                                       : (root.parameter.options || []).length > 0 ? choiceEditor
                                       : root.parameter.type === "Bool" ? boolEditor
                                       : root.parameter.type === "Int" ? integerEditor
                                       : root.parameter.type === "Float" ? floatEditor
                                       : root.parameter.type === "Rect" ? rectEditor
                                       : root.parameter.type === "RoiBatch" ? roiBatchEditor
                                       : root.structured ? structureEditor
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
                    visible: !root.parameter.init && !root.alwaysUpstream
                    Layout.preferredWidth: Theme.paramActionBtnWidth
                    Layout.fillHeight: true
                    color: toggleMouse.containsMouse ? Theme.paramRowHoverBg : "transparent"

                    UpstreamToggleIcon {
                        anchors.centerIn: parent
                        iconSize: Theme.paramToggleIconSize
                        iconColor: root.upstream ? Theme.bindingActiveColor : Theme.bindingInactiveColor
                    }

                    MouseArea {
                        id: toggleMouse
                        anchors.fill: parent
                        hoverEnabled: true
                        cursorShape: Qt.PointingHandCursor
                        enabled: root.editable
                        onClicked: app.setParameterMode(root.parameter.id, !root.upstream)
                    }

                    QuiToolTip {
                        visible: toggleMouse.containsMouse
                        text: root.upstream ? "切换为直接输入" : "绑定上层输入"
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
        }

    }

    RectangleEditor {
        id: rectEditorItem
        Layout.fillWidth: true
        Layout.leftMargin: root.labelWidth + 6
        readonly property bool hasImage: session && session.imageWidth > 0
        visible: root.parameter.type === "Rect" && !root.parameter.init
            && app.roiEditor.node === root.nodeId && app.roiEditor.input === root.parameter.id
        opacity: hasImage ? 0 : 1
        Layout.preferredHeight: hasImage ? 0 : -1
        clip: hasImage
        enabled: root.editable
        session: visible ? app.roiEditor : ({})
    }

    function currentUpstreamValue() {
        if (root.parameter.sourceNode && root.parameter.sourcePort) {
            return String(root.parameter.sourceNode) + ":" + String(root.parameter.sourcePort)
        }
        return ""
    }

    function buildBindingGroups() {
        let candidates = app.bindingCandidates(app.portContext(root.parameter.id))
        let groups = []
        if (root.parameter.source) {
            groups.push({
                label: "解除绑定",
                value: "__unbind__",
                subOptions: []
            })
        }
        if (!candidates || candidates.length === 0) {
            if (!root.parameter.source) {
                groups.push({
                    label: "无可用兼容输出",
                    value: "__none__",
                    subOptions: []
                })
            }
            return groups
        }
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
            displayText: root.parameter.source ? root.parameter.source : "请选择"
            optionGroups: root.buildBindingGroups()
            onAboutToOpen: {
                bindingContext = app.portContext(root.parameter.id)
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
            model: ["是", "否"]
            currentIndex: root.parameter.literal === "true" ? 0 : 1
            onActivated: root.commit(currentIndex === 0 ? "true" : "false")
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
            displayText: currentIndex < 0 ? root.valueText + "（当前不可用）" : model[currentIndex].label
            onActivated: function(index) { root.commit(model[index].value) }
            HoverHandler { id: choiceHover }
            QuiToolTip {
                visible: choiceHover.hovered && text.length > 0 && choice.contentItem && choice.contentItem.truncated
                text: choice.currentText || ""
                delay: 500
            }
        }
    }
    Component {
        id: textEditor
        ParameterTextField {
            id: field
            flat: true
            placeholderText: "请选择"
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
            actionIcon: root.parameter.editor === "file" || root.parameter.editor === "directory" ? QuiFontIcon.FolderOpen : 0
            actionText: root.parameter.editor === "directory" ? "选择文件夹" : "选择文件"
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
            onActionRequested: root.parameter.editor === "directory" ? directoryPicker.open() : filePicker.open()
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
        id: roiBatchEditor
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
        id: structureEditor
        QuiButton {
            id: structure
            font: root.editorFont
            implicitHeight: Theme.paramEditorHeight
            leftPadding: 8
            rightPadding: 8
            opacity: 1
            text: (root.parameter.typeDetail || root.parameter.type) + " · " + root.parameter.literal.replace(/\s+/g, " ").slice(0, 48)
            focusPolicy: Qt.TabFocus
            background: Rectangle { color: structure.hovered ? QuiColor.ItemHover : "transparent" }
            contentItem: QuiText {
                text: structure.text
                font: structure.font
                color: structure.enabled ? QuiColor.FontPrimary : QuiColor.FontCaption
                verticalAlignment: Text.AlignVCenter
                elide: Text.ElideRight
            }
            onClicked: {
                root.structureDraft = root.parameter.literal; root.structureError = ""; structureDialog.open()
            }
        }
    }
    FileDialog { id: filePicker; title: "选择 " + root.parameter.name; onAccepted: if (root.editable) app.setParameterPath(root.parameter.id, root.parameter.init, selectedFile) }
    FolderDialog { id: directoryPicker; title: "选择 " + root.parameter.name; onAccepted: if (root.editable) app.setParameterPath(root.parameter.id, root.parameter.init, selectedFolder) }
    WorkbenchDialog {
        id: structureDialog; title: root.parameter.name + " · 结构数据"
        closePolicy: Popup.CloseOnEscape
        width: Math.min(760, parent ? parent.width - 32 : 760)
        positiveButtonEnabled: root.editable
        contentDelegate: Component {
            ColumnLayout {
                implicitHeight: 340; spacing: 8
                QuiText { text: (root.parameter.typeDetail || root.parameter.type) + " · JSON"; Layout.fillWidth: true; wrapMode: Text.Wrap }
                ScrollView {
                    Layout.fillWidth: true; Layout.fillHeight: true
                    QuiTextArea { text: root.structureDraft; readOnly: !root.editable; selectByMouse: true; wrapMode: TextEdit.NoWrap; onTextChanged: root.structureDraft = text; font.family: "Consolas" }
                }
                QuiText { text: root.structureError; color: Theme.error; Layout.fillWidth: true; wrapMode: Text.Wrap }
            }
        }
        onPositiveClickListener: function() {
            if (root.editable && app.setParameter(root.parameter.id, root.parameter.init, root.structureDraft, true)) structureDialog.close()
            else root.structureError = app.diagnostic
        }
    }
}
