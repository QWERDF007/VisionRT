import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import QtQuick.Dialogs
import quickui

WorkbenchDialog {
    id: root
    property real hostWidth: 1000
    property real hostHeight: 800
    property var draft: ({})
    property var definitions: []
    property var implementations: []
    property bool choosing: false
    property bool newDefinition: true
    property string errorText: ""
    property string impactText: ""
    property int portTab: 0
    readonly property bool cpp: String(draft.kind || "").indexOf("cpp_") === 0
    readonly property bool builtin: draft.builtin === true
    readonly property var kinds: ["python_function", "python_class", "cpp_function", "cpp_class"]
    title: choosing ? "选择工具定义" : builtin ? "内置工具定义（只读）" : newDefinition ? "新建工具定义" : "编辑工具定义"
    width: Math.min(1040, hostWidth - 40)
    positiveText: choosing ? "编辑所选" : builtin ? "复制为自定义定义" : "预览影响并保存"
    positiveButtonEnabled: !app.busy && (!choosing || !!draft.id)
    negativeText: "取消"
    function setField(key, value) { let next = Object.assign({}, draft); next[key] = value; draft = next }
    function createDefinition() {
        implementations = app.cppImplementations()
        draft = { id: "", displayName: "", version: "1.0", kind: "python_function", category: "自定义", subcategory: "通用", builtin: false, definitionPath: "", registryName: "", script: "", symbol: "", inputs: [], initParams: [], outputs: [] }
        choosing = false; newDefinition = true; errorText = ""; portTab = 0; open()
    }
    function chooseDefinition() {
        definitions = app.toolDefinitions(); implementations = app.cppImplementations()
        draft = definitions.length ? app.toolMetadata(definitions[0].id) : ({})
        choosing = true; newDefinition = false; errorText = ""; portTab = 0; open()
    }
    function selectImplementation(index) {
        if (index < 0 || index >= implementations.length) return
        let registered = implementations[index]
        let next = Object.assign({}, draft)
        next.registryName = registered.registryName || registered.id
        next.kind = registered.kind
        next.inputs = JSON.parse(JSON.stringify(registered.inputs || []))
        next.initParams = JSON.parse(JSON.stringify(registered.initParams || []))
        next.outputs = JSON.parse(JSON.stringify(registered.outputs || []))
        draft = next
    }
    onPositiveClickListener: function() {
        if (choosing) { choosing = false; return }
        if (builtin) {
            let copy = JSON.parse(JSON.stringify(draft))
            copy.id = copy.id + ".custom"; copy.displayName = copy.displayName + "（自定义）"
            copy.builtin = false; copy.definitionPath = ""
            draft = copy; newDefinition = true; return
        }
        let impact = app.previewToolImpact(draft)
        if (!impact.valid) { errorText = impact.message; return }
        impactText = impact.message || JSON.stringify(impact, null, 2)
        impactConfirm.open()
    }
    contentDelegate: Component {
        ColumnLayout {
            implicitHeight: Math.min(650, root.hostHeight - 180)
            spacing: 8
            RowLayout {
                visible: root.choosing
                Layout.fillWidth: true; Layout.leftMargin: 18; Layout.rightMargin: 18
                QuiText { text: "工具" }
                QuiComboBox {
                    Layout.fillWidth: true; model: root.definitions; textRole: "displayName"; valueRole: "id"
                    currentIndex: root.definitions.length ? 0 : -1
                    onActivated: root.draft = app.toolMetadata(currentValue)
                }
            }
            ScrollView {
                id: formScroll
                Layout.fillWidth: true; Layout.fillHeight: true
                contentWidth: availableWidth
                ColumnLayout {
                    width: formScroll.availableWidth - 36; x: 18; spacing: 8
                    enabled: !root.choosing && !app.busy
                    GridLayout {
                        enabled: !root.builtin
                        Layout.fillWidth: true; columns: 4; columnSpacing: 8; rowSpacing: 3
                        QuiText { text: "稳定英文 ID" }
                        QuiTextField { Layout.fillWidth: true; text: root.draft.id || ""; readOnly: !root.newDefinition; placeholderText: "company.tool.name"; onTextEdited: root.setField("id", text) }
                        QuiText { text: "显示名称" }
                        QuiTextField { Layout.fillWidth: true; text: root.draft.displayName || ""; onTextEdited: root.setField("displayName", text) }
                        QuiText { text: "版本" }
                        QuiTextField { Layout.fillWidth: true; text: root.draft.version || ""; onTextEdited: root.setField("version", text) }
                        QuiText { text: "实现类型" }
                        QuiComboBox {
                            Layout.fillWidth: true; model: root.kinds; currentIndex: model.indexOf(root.draft.kind)
                            onActivated: {
                                root.setField("kind", currentText)
                                if (root.cpp && root.implementations.length) {
                                    for (let i = 0; i < root.implementations.length; ++i) if (root.implementations[i].kind === currentText) { root.selectImplementation(i); break }
                                }
                            }
                        }
                        QuiText { text: "大类" }
                        QuiTextField { Layout.fillWidth: true; text: root.draft.category || ""; onTextEdited: root.setField("category", text) }
                        QuiText { text: "子类" }
                        QuiTextField { Layout.fillWidth: true; text: root.draft.subcategory || ""; onTextEdited: root.setField("subcategory", text) }
                    }
                    RowLayout {
                        enabled: !root.builtin
                        visible: root.cpp; Layout.fillWidth: true
                        QuiText { text: "已注册 C++ 实现" }
                        QuiComboBox {
                            Layout.fillWidth: true; model: root.implementations; textRole: "displayName"
                            currentIndex: { for (let i = 0; i < root.implementations.length; ++i) if ((root.implementations[i].registryName || root.implementations[i].id) === root.draft.registryName) return i; return -1 }
                            displayText: currentIndex < 0 ? root.draft.registryName || "选择注册实现" : currentText
                            onActivated: root.selectImplementation(currentIndex)
                        }
                    }
                    RowLayout {
                        enabled: !root.builtin
                        visible: !root.cpp; Layout.fillWidth: true
                        QuiText { text: "custom 脚本 / 包入口" }
                        QuiTextField { Layout.fillWidth: true; text: root.draft.script || ""; placeholderText: "相对于 tools/custom 的脚本路径"; onTextEdited: root.setField("script", text) }
                        QuiButton { text: "浏览…"; focusPolicy: Qt.TabFocus; onClicked: scriptPicker.open() }
                    }
                    RowLayout {
                        enabled: !root.builtin
                        visible: !root.cpp; Layout.fillWidth: true
                        QuiText { text: root.draft.kind === "python_class" ? "类名" : "函数名" }
                        QuiTextField { Layout.fillWidth: true; text: root.draft.symbol || ""; onTextEdited: root.setField("symbol", text) }
                    }
                    RowLayout {
                        Layout.fillWidth: true; spacing: 2
                        Repeater {
                            model: ["输入端口", "初始化参数", "输出端口"]
                            QuiTabButton {
                                required property string modelData; required property int index
                                text: modelData; checked: root.portTab === index
                                implicitHeight: 32; showIndicator: true
                                normalColor: "#f5f7fa"; checkedColor: QuiColor.Primary
                                normalTextColor: QuiColor.FontPrimary; checkedTextColor: QuiColor.Highlight
                                onClicked: root.portTab = index
                            }
                        }
                        Item { Layout.fillWidth: true }
                    }
                    PortDefinitionEditor {
                        Layout.fillWidth: true
                        ports: root.draft[root.portTab === 0 ? "inputs" : root.portTab === 1 ? "initParams" : "outputs"] || []
                        signatureLocked: root.cpp; outputs: root.portTab === 2; initialization: root.portTab === 1
                        readOnly: root.builtin
                        onPortsEdited: function(value) { root.setField(root.portTab === 0 ? "inputs" : root.portTab === 1 ? "initParams" : "outputs", value) }
                    }
                }
            }
            QuiText {
                Layout.fillWidth: true; Layout.leftMargin: 18; Layout.rightMargin: 18
                text: root.errorText; visible: text.length > 0; color: "#c9483d"; wrapMode: Text.Wrap
            }
        }
    }
    WorkbenchDialog {
        id: impactConfirm; title: "确认工具定义变更"
        width: Math.min(680, root.hostWidth - 60)
        message: root.impactText
        positiveText: "保存定义"; positiveButtonEnabled: !app.busy
        onPositiveClickListener: function() {
            if (app.saveToolMetadata(root.draft)) { impactConfirm.close(); root.close() }
            else { root.errorText = app.diagnostic; impactConfirm.close() }
        }
    }
    FileDialog {
        id: scriptPicker; title: "选择 tools/custom 下的 Python 脚本或包入口"
        nameFilters: ["Python 脚本 (*.py)", "所有文件 (*)"]
        onAccepted: root.setField("script", decodeURIComponent(selectedFile.toString()).replace(/^file:\/\/\//, "").replace(/^file:\/\//, "//"))
    }
}
