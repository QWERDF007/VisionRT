import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import QtQuick.Dialogs
import quickui

RowLayout {
    id: root
    required property var parameter
    property var gpuDevices: []
    property string structureDraft: ""
    property string structureError: ""
    readonly property bool upstream: parameter.mode === "upstream" && !parameter.init
    readonly property string valueText: parameter.valueText === undefined ? parameter.literal : parameter.valueText
    readonly property bool structured: ["NumericArray", "StringArray", "RoiBatch"].indexOf(parameter.type) >= 0
    spacing: 5
    function commit(text) { return app.setParameter(parameter.id, parameter.init, text) }
    function pathText(url) { return decodeURIComponent(url.toString()).replace(/^file:\/\/\//, "").replace(/^file:\/\//, "//") }
    function startRoi() {
        if (upstream) roiCopy.open()
        else app.editRoi(parameter.id, false)
    }

    QuiText {
        text: root.parameter.name; Layout.preferredWidth: 96
        elide: Text.ElideRight
        MouseArea { id: nameHover; anchors.fill: parent; hoverEnabled: true; acceptedButtons: Qt.NoButton }
        QuiToolTip {
            visible: nameHover.containsMouse
            text: {
                let lines = []
                lines.push("参数: " + root.parameter.name + (root.parameter.id && root.parameter.id !== root.parameter.name ? " (" + root.parameter.id + ")" : ""))
                lines.push("类型: " + (root.parameter.typeDetail || root.parameter.type))
                lines.push("模式: " + (root.upstream ? "上层输入 (" + (root.parameter.source || "未连接") + ")" : "静态输入"))
                return lines.join("\n")
            }
            delay: 500
        }
    }
    Loader {
        Layout.fillWidth: true; Layout.minimumWidth: 70
        enabled: !app.busy
        sourceComponent: root.upstream ? upstreamEditor : root.parameter.optionsProvider === "platform.gpu_devices" ? gpuEditor : (root.parameter.options || []).length > 0 ? choiceEditor : root.parameter.type === "Bool" ? boolEditor : root.parameter.type === "Int" || root.parameter.type === "Float" ? numberEditor : root.structured ? structureEditor : root.parameter.type === "ImageBatch" ? imageEditor : textEditor
    }
    QuiTextIconButton {
        visible: !root.parameter.init
        text: root.upstream ? "切换为用户输入\n保留上层草稿" : "切换为上层输入\n保留用户草稿"
        iconSource: root.upstream ? QuiFontIcon.Edit : QuiFontIcon.Link
        enabled: !app.busy
        onClicked: app.setParameterMode(root.parameter.id, !root.upstream)
    }
    QuiTextIconButton {
        visible: root.parameter.type === "RoiBatch"; enabled: !app.busy
        text: "编辑输入 ROI"; iconSource: QuiFontIcon.Crop
        onClicked: root.startRoi()
    }

    Component {
        id: upstreamEditor
        Rectangle {
            implicitHeight: 26; implicitWidth: 160
            color: "#ffffff"; radius: 3; border.color: "#c5ccd5"; border.width: 1
            RowLayout {
                anchors.fill: parent; anchors.leftMargin: 8; anchors.rightMargin: 6; spacing: 4
                QuiText {
                    Layout.fillWidth: true; elide: Text.ElideRight
                    text: root.parameter.source || "未选择 · 点击选择上层输入"
                    font: QuiFont.Body
                    color: root.parameter.source ? QuiColor.FontPrimary : "#8c959f"
                }
                QuiTextIcon {
                    iconSource: QuiFontIcon.ChevronDown; iconSize: 10; iconColor: "#57606a"
                }
            }
            MouseArea {
                anchors.fill: parent; cursorShape: Qt.PointingHandCursor
                onClicked: binding.openInput(root.parameter.id, root.parameter.name)
            }
        }
    }
    Component {
        id: numberEditor
        NumberParameterEditor {
            value: root.parameter.literal; integer: root.parameter.type === "Int"
            minimum: root.parameter.minimum || ""; maximum: root.parameter.maximum || ""
            step: root.parameter.step || "1"; decimals: root.parameter.decimals === undefined ? 6 : root.parameter.decimals
            commitValue: function(text) { return root.commit(text) }
        }
    }
    Component {
        id: boolEditor
        QuiToggleSwitch {
            checked: root.parameter.literal === "true"
            clickListener: function() { root.commit(checked ? "false" : "true") }
        }
    }
    Component {
        id: choiceEditor
        QuiComboBox {
            model: root.parameter.options
            currentIndex: model.indexOf(root.valueText)
            displayText: currentIndex < 0 ? root.valueText + "（当前不可用）" : currentText
            onActivated: root.commit(root.parameter.type === "String" ? JSON.stringify(currentText) : currentText)
        }
    }
    Component {
        id: gpuEditor
        RowLayout {
            spacing: 2
            QuiComboBox {
                id: gpuChoice; Layout.fillWidth: true
                model: root.gpuDevices; textRole: "name"; valueRole: "id"
                currentIndex: { for (let i = 0; i < root.gpuDevices.length; ++i) if (String(root.gpuDevices[i].id) === root.parameter.literal) return i; return -1 }
                displayText: currentIndex < 0 ? root.parameter.literal + "（当前不可用）" : currentText
                onActivated: root.commit(String(currentValue))
            }
            QuiTextIconButton { text: "查询 GPU"; iconSource: QuiFontIcon.Refresh; onClicked: app.queryGpuDevices() }
        }
    }
    Component {
        id: textEditor
        RowLayout {
            spacing: 2
            QuiTextField {
                Layout.fillWidth: true; implicitHeight: 32; text: root.valueText
                property bool rejected: false
                color: rejected ? "#c9483d" : QuiColor.FontPrimary
                onEditingFinished: { if (text !== root.valueText) rejected = !root.commit(root.parameter.type === "String" ? JSON.stringify(text) : text) }
            }
            QuiTextIconButton {
                visible: root.parameter.editor === "file"
                text: "选择文件"; iconSource: QuiFontIcon.OpenFile; onClicked: filePicker.open()
            }
            QuiTextIconButton {
                visible: root.parameter.editor === "directory" || root.parameter.editor === "file"
                text: "选择文件夹"; iconSource: QuiFontIcon.FolderOpen; onClicked: directoryPicker.open()
            }
        }
    }
    Component {
        id: structureEditor
        QuiButton {
            text: (root.parameter.typeDetail || root.parameter.type) + " · " + root.parameter.literal.replace(/\s+/g, " ").slice(0, 64)
            focusPolicy: Qt.TabFocus
            onClicked: { root.structureDraft = root.parameter.literal; root.structureError = ""; structureDialog.open() }
        }
    }
    Component {
        id: imageEditor
        QuiText { text: "图像由上层输出提供"; color: QuiColor.FontCaption; elide: Text.ElideRight }
    }
    BindingPopup { id: binding }
    FileDialog { id: filePicker; title: "选择 " + root.parameter.name; onAccepted: root.commit(JSON.stringify(root.pathText(selectedFile))) }
    FolderDialog { id: directoryPicker; title: "选择 " + root.parameter.name; onAccepted: root.commit(JSON.stringify(root.pathText(selectedFolder))) }
    WorkbenchDialog {
        id: roiCopy; title: "复制当前 ROI 为用户输入？"
        message: "将断开此输入的绑定并编辑当前值的副本，不会修改源节点。"
        onPositiveClickListener: function() { if (app.editRoi(root.parameter.id, true)) roiCopy.close() }
    }
    WorkbenchDialog {
        id: structureDialog; title: root.parameter.name + " · 结构数据"
        width: Math.min(760, parent ? parent.width - 32 : 760)
        positiveButtonEnabled: !app.busy
        contentDelegate: Component {
            ColumnLayout {
                implicitHeight: 340; spacing: 8
                QuiText { text: (root.parameter.typeDetail || root.parameter.type) + " · YAML / JSON；图像不可进行像素编辑"; Layout.fillWidth: true; wrapMode: Text.Wrap }
                ScrollView {
                    Layout.fillWidth: true; Layout.fillHeight: true
                    QuiTextArea { text: root.structureDraft; selectByMouse: true; wrapMode: TextEdit.NoWrap; onTextChanged: root.structureDraft = text; font.family: "Consolas" }
                }
                QuiText { text: root.structureError; color: "#c9483d"; Layout.fillWidth: true; wrapMode: Text.Wrap }
            }
        }
        onPositiveClickListener: function() {
            if (root.commit(root.structureDraft)) structureDialog.close()
            else root.structureError = app.diagnostic
        }
    }
    Connections { target: app; function onGpuDevicesReady(devices) { root.gpuDevices = devices } }
}
