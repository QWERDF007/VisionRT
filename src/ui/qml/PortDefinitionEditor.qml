import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import quickui

ColumnLayout {
    id: root
    property var ports: []
    property bool signatureLocked: false
    property bool readOnly: false
    property bool outputs: false
    property bool initialization: false
    property int selectedIndex: -1
    signal portsEdited(var value)
    readonly property var currentPort: selectedIndex >= 0 && selectedIndex < ports.length ? ports[selectedIndex] : ({})
    readonly property bool systemPort: currentPort.id === "__status_code" || currentPort.id === "__status_message"
    readonly property var types: ["Bool", "Int", "Float", "String", "ImageBatch", "RoiBatch", "NumericArray", "StringArray"]
    spacing: 6
    function update(key, value) {
        if (selectedIndex < 0 || selectedIndex >= ports.length || systemPort) return
        let rows = ports.slice(), row = Object.assign({}, rows[selectedIndex]); row[key] = value; rows[selectedIndex] = row
        portsEdited(rows)
    }
    function addPort() {
        let rows = ports.slice(), id = "port", number = 1
        while (rows.some(function(p) { return p.id === id + number })) number += 1
        rows.push({ id: id + number, name: "新端口", type: "String", dtype: "float32", rank: 1, layout: "dense", default: "\"\"", editor: "", tab: "基本输入", group: "", minimum: "", maximum: "", step: "", decimals: 6, options: [], optionsProvider: "", imageInput: "", imagePort: "", colorOrder: "", sameGeometryAsInput: "" })
        portsEdited(rows); selectedIndex = rows.length - 1
    }
    RowLayout {
        Layout.fillWidth: true
        QuiText { text: root.signatureLocked ? "C++ 注册签名只读；名称、展示分组和合法默认值可编辑" : "选择端口后在下方编辑"; Layout.fillWidth: true; wrapMode: Text.Wrap; color: QuiColor.FontCaption; font: QuiFont.Caption }
        QuiButton { text: "新增端口"; enabled: !root.signatureLocked && !root.readOnly; focusPolicy: Qt.TabFocus; onClicked: root.addPort() }
        QuiButton { text: "删除端口"; enabled: !root.signatureLocked && !root.readOnly && !root.systemPort && root.selectedIndex >= 0; focusPolicy: Qt.TabFocus; onClicked: { let rows = root.ports.slice(); rows.splice(root.selectedIndex, 1); root.portsEdited(rows); root.selectedIndex = Math.min(root.selectedIndex, rows.length - 1) } }
    }
    QuiTableView {
        id: portTable
        Layout.fillWidth: true; Layout.preferredHeight: 154
        dataSource: root.ports.map(function(p, i) { return { _key: "port-" + i, row: i, id: p.id, name: p.name, type: p.type, signature: p.type === "NumericArray" || p.type === "StringArray" ? (p.dtype || "string") + " / " + p.rank + " / " + p.layout : "" } })
        columnSource: [
            { title: "ID", dataIndex: "id", width: 145, readOnly: true },
            { title: "名称", dataIndex: "name", width: 145, readOnly: true },
            { title: "类型", dataIndex: "type", width: 120, readOnly: true },
            { title: "dtype / rank / layout", dataIndex: "signature", width: 250, readOnly: true }
        ]
        fitColumnsToWidth: true; resizableRows: false; rowHeight: 30; headerHeight: 28
        rowColor: QuiColor.Primary; zebraColor: "#f4f6f9"; selectedColor: "#dceefa"; hoverColor: "#eaf4fc"
        onCurrentChanged: { if (current && current.row !== undefined) root.selectedIndex = current.row }
    }
    QuiText {
        visible: root.outputs
        text: "系统输出（只读）：__status_code · Int　 /　__status_message · String"
        color: QuiColor.FontCaption; font: QuiFont.Caption; Layout.fillWidth: true; wrapMode: Text.Wrap
    }
    GridLayout {
        visible: root.selectedIndex >= 0 && root.selectedIndex < root.ports.length
        enabled: !root.systemPort && !root.readOnly
        Layout.fillWidth: true; columns: 4; columnSpacing: 8; rowSpacing: 3
        QuiText { text: "端口 ID" }
        QuiTextField { Layout.fillWidth: true; text: root.currentPort.id || ""; readOnly: root.signatureLocked; onTextEdited: root.update("id", text) }
        QuiText { text: "显示名称" }
        QuiTextField { Layout.fillWidth: true; text: root.currentPort.name || ""; onTextEdited: root.update("name", text) }
        QuiText { text: "类型" }
        QuiComboBox { Layout.fillWidth: true; model: root.types; currentIndex: model.indexOf(root.currentPort.type); enabled: !root.signatureLocked; onActivated: root.update("type", currentText) }
        QuiText { text: "默认值（YAML）"; visible: !root.outputs }
        QuiTextField { Layout.fillWidth: true; visible: !root.outputs; text: root.currentPort.default || ""; onTextEdited: root.update("default", text) }
        QuiText { text: "dtype"; visible: root.currentPort.type === "NumericArray" }
        QuiComboBox {
            Layout.fillWidth: true; visible: root.currentPort.type === "NumericArray"; enabled: !root.signatureLocked
            model: ["uint8", "int32", "int64", "float32", "float64", "bool"]; currentIndex: model.indexOf(root.currentPort.dtype)
            onActivated: root.update("dtype", currentText)
        }
        QuiText { text: "rank"; visible: root.currentPort.type === "NumericArray" || root.currentPort.type === "StringArray" }
        QuiTextField {
            Layout.fillWidth: true; visible: root.currentPort.type === "NumericArray" || root.currentPort.type === "StringArray"; readOnly: root.signatureLocked
            text: root.currentPort.rank === undefined ? "1" : String(root.currentPort.rank)
            validator: IntValidator { bottom: 0 }
            onEditingFinished: { if (acceptableInput) root.update("rank", Number(text)) }
        }
        QuiText { text: "layout"; visible: root.currentPort.type === "NumericArray" || root.currentPort.type === "StringArray" }
        QuiComboBox {
            Layout.fillWidth: true; visible: root.currentPort.type === "NumericArray" || root.currentPort.type === "StringArray"; enabled: !root.signatureLocked
            model: ["dense", "batch_ragged"]; currentIndex: model.indexOf(root.currentPort.layout)
            onActivated: root.update("layout", currentText)
        }
        QuiText { text: "标签"; visible: !root.outputs }
        QuiComboBox { Layout.fillWidth: true; visible: !root.outputs; model: ["基本输入", "预处理", "检测", "结果处理"]; currentIndex: Math.max(0, model.indexOf(root.currentPort.tab)); onActivated: root.update("tab", currentText) }
        QuiText { text: "分组"; visible: !root.outputs }
        QuiTextField { Layout.fillWidth: true; visible: !root.outputs; text: root.currentPort.group || ""; onTextEdited: root.update("group", text) }
        QuiText { text: "编辑控件"; visible: !root.outputs }
        QuiComboBox {
            Layout.fillWidth: true; visible: !root.outputs
            model: ["", "text", "number", "file", "directory", "roi", "array"]
            currentIndex: Math.max(0, model.indexOf(root.currentPort.editor || "")); onActivated: root.update("editor", currentText)
        }
        QuiText { text: "最小值"; visible: !root.outputs }
        QuiTextField { Layout.fillWidth: true; visible: !root.outputs; text: root.currentPort.minimum || ""; onTextEdited: root.update("minimum", text) }
        QuiText { text: "最大值"; visible: !root.outputs }
        QuiTextField { Layout.fillWidth: true; visible: !root.outputs; text: root.currentPort.maximum || ""; onTextEdited: root.update("maximum", text) }
        QuiText { text: "步长"; visible: !root.outputs }
        QuiTextField { Layout.fillWidth: true; visible: !root.outputs; text: root.currentPort.step || ""; onTextEdited: root.update("step", text) }
        QuiText { text: "小数位"; visible: !root.outputs }
        QuiTextField {
            Layout.fillWidth: true; visible: !root.outputs; text: root.currentPort.decimals === undefined ? "6" : String(root.currentPort.decimals)
            validator: IntValidator { bottom: 0; top: 15 }
            onEditingFinished: { if (acceptableInput) root.update("decimals", Number(text)) }
        }
        QuiText { text: "固定选项"; visible: !root.outputs }
        QuiTextField { Layout.fillWidth: true; visible: !root.outputs; placeholderText: "用 | 分隔"; text: (root.currentPort.options || []).join("|"); onTextEdited: root.update("options", text.length ? text.split("|") : []) }
        QuiText { text: "动态候选"; visible: !root.outputs }
        QuiComboBox { Layout.fillWidth: true; visible: !root.outputs; model: ["", "platform.gpu_devices"]; currentIndex: Math.max(0, model.indexOf(root.currentPort.optionsProvider || "")); onActivated: root.update("optionsProvider", currentText) }
        QuiText { text: root.outputs ? "关联图像输出" : "关联图像输入"; visible: root.currentPort.type === "RoiBatch" }
        QuiTextField { Layout.fillWidth: true; visible: root.currentPort.type === "RoiBatch"; text: root.outputs ? root.currentPort.imagePort || "" : root.currentPort.imageInput || ""; onTextEdited: root.update(root.outputs ? "imagePort" : "imageInput", text) }
        QuiText { text: "颜色顺序"; visible: root.outputs && root.currentPort.type === "ImageBatch" }
        QuiComboBox { Layout.fillWidth: true; visible: root.outputs && root.currentPort.type === "ImageBatch"; model: ["", "BGR", "BGRA", "RGB", "RGBA"]; currentIndex: Math.max(0, model.indexOf(root.currentPort.colorOrder || "")); onActivated: root.update("colorOrder", currentText) }
        QuiText { text: "同坐标系输入"; visible: root.outputs && root.currentPort.type === "ImageBatch" }
        QuiTextField { Layout.fillWidth: true; visible: root.outputs && root.currentPort.type === "ImageBatch"; text: root.currentPort.sameGeometryAsInput || ""; onTextEdited: root.update("sameGeometryAsInput", text) }
    }
}
