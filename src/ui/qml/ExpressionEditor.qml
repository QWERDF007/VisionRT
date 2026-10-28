import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import quickui
import VisionRT.Style 1.0

// Node-model editor for the expression and condition tools. Port identifiers are
// allocated by the controller; names, symbols, types and formulas are owned here
// until one transaction commits them.
Dialog {
    id: root
    property string nodeId: ""
    property string model: "expression"
    property var variables: []
    property var program: []
    property int nextSeq: 1
    property bool conditionMode: false

    modal: true
    closePolicy: Popup.CloseOnEscape
    parent: Overlay.overlay ? Overlay.overlay : undefined
    x: Math.round(((parent ? parent.width : 0) - width) / 2)
    y: Math.round(((parent ? parent.height : 0) - height) / 2)
    width: Math.min(1080, Overlay.overlay ? Overlay.overlay.width - 80 : 1080)
    height: Math.min(720, Overlay.overlay ? Overlay.overlay.height - 80 : 720)
    title: conditionMode ? "条件公式" : "变量与公式"
    standardButtons: Dialog.NoButton
    padding: 12

    background: Rectangle {
        color: QuiColor.CardBackground
        border.color: QuiColor.CardBorder
        border.width: 1
        radius: 6
    }

    function openNode(id) {
        openNodeWithDraft(id, "")
    }

    function openNodeWithDraft(id, initialDraft, hasDraft) {
        nodeId = id
        let config = app.expressionConfig(id)
        model = config.model || "expression"
        conditionMode = model === "condition"
        nextSeq = config.nextPortSeq || 1
        variables = (config.variables || []).map(function(item) { return Object.assign({}, item) })
        program = (config.program || []).map(function(item) { return Object.assign({}, item) })
        // The presence of a draft is explicit: an empty draft is the user clearing the field, not
        // "no draft", so the committed formula must not come back just because the text is empty.
        if (hasDraft === true) {
            let rows = program.slice()
            if (rows.length === 0) {
                rows.push({ portId: allocatePort(), name: "结果1", symbol: "r1", expression: "",
                            type: conditionMode ? "Bool" : "Float", dtype: "", rank: -1,
                            exposed: !conditionMode })
            }
            if (rows.length === 1) {
                rows[0] = Object.assign({}, rows[0])
                rows[0].expression = initialDraft === undefined || initialDraft === null ? "" : initialDraft
            }
            program = rows
        }
        open()
    }

    Connections {
        target: app
        function onSelectedNodeChanged() {
            if (root.visible && app.selectedNode !== root.nodeId)
                root.close()
        }
        function onRuntimeBusyChanged() {
            if (root.visible && app.runtimeBusy)
                root.close()
        }
    }
    function allocatePort() {
        let id = "p" + nextSeq
        nextSeq += 1
        return id
    }
    function addVariable() {
        let rows = variables.slice()
        rows.push({ portId: allocatePort(), name: "变量" + (rows.length + 1), symbol: "v" + (rows.length + 1),
                    type: "Float", dtype: "", rank: -1, allowMissing: false })
        variables = rows
    }
    function addProgram() {
        let rows = program.slice()
        // Conditions always produce a boolean decision, so their single formula starts as Bool.
        rows.push({ portId: allocatePort(), name: "结果" + (rows.length + 1), symbol: "r" + (rows.length + 1),
                    expression: "", type: conditionMode ? "Bool" : "Float", dtype: "", rank: -1,
                    exposed: !conditionMode })
        program = rows
    }
    function removeVariable(index) {
        let rows = variables.slice(); rows.splice(index, 1); variables = rows
    }
    function removeProgram(index) {
        let rows = program.slice(); rows.splice(index, 1); program = rows
    }
    function replaceRow(programRow, index, row) {
        let rows = (programRow ? program : variables).slice()
        rows[index] = row
        if (programRow) program = rows
        else variables = rows
    }
    function rowAt(programRow, index) {
        return (programRow ? program : variables)[index]
    }
    function updateVariable(index, key, value) {
        let row = Object.assign({}, variables[index]); row[key] = value; replaceRow(false, index, row)
    }
    function updateProgram(index, key, value) {
        let row = Object.assign({}, program[index]); row[key] = value; replaceRow(true, index, row)
    }
    // Switching the kind of a row must also settle the array shape, otherwise the row would render
    // a fallback dtype/rank while the committed draft stays unrepresentable and gets rejected.
    function changeType(index, programRow, type) {
        let row = Object.assign({}, rowAt(programRow, index))
        row.type = type
        if (type === "Array") {
            if (root.dtypeValues.indexOf(row.dtype) < 0) row.dtype = "float64"
            if (!(row.rank >= 1 && row.rank <= 4)) row.rank = 1
        } else {
            row.dtype = ""
            row.rank = -1
        }
        replaceRow(programRow, index, row)
    }
    function commit() {
        if (!app.applyExpressionConfig(nodeId, variables, program))
            return false
        close()
        return true
    }

    readonly property var typeValues: ["Int", "Float", "Bool", "String", "Array"]
    readonly property var dtypeValues: ["uint8", "int32", "int64", "float32", "float64", "bool", "string"]
    function typeLabel(value) {
        let text = app.typeDescription(value, "", -1)
        return text.length > 0 ? text : value
    }
    function rowDescription(row) {
        return row.type === "Array" ? app.typeDescription("Array", row.dtype, row.rank) : typeLabel(row.type)
    }

    component Field: TextField {
        Layout.fillWidth: true
        selectByMouse: true
    }

    component RowTypeEditor: RowLayout {
        id: typeRow
        required property var rowData
        required property int rowIndex
        required property bool programRow
        spacing: 4
        ComboBox {
            id: typeCombo
            Layout.preferredWidth: 110
            model: root.typeValues.map(function(value) { return root.typeLabel(value) })
            currentIndex: Math.max(0, root.typeValues.indexOf(typeRow.rowData.type || "Int"))
            onActivated: root.changeType(typeRow.rowIndex, typeRow.programRow, root.typeValues[typeCombo.currentIndex])
        }
        ComboBox {
            id: dtypeCombo
            Layout.preferredWidth: 110
            visible: typeRow.rowData.type === "Array"
            model: root.dtypeValues
            currentIndex: Math.max(0, root.dtypeValues.indexOf(typeRow.rowData.dtype || "float64"))
            onActivated: typeRow.programRow
                ? root.updateProgram(typeRow.rowIndex, "dtype", root.dtypeValues[dtypeCombo.currentIndex])
                : root.updateVariable(typeRow.rowIndex, "dtype", root.dtypeValues[dtypeCombo.currentIndex])
        }
        SpinBox {
            Layout.preferredWidth: 84
            visible: typeRow.rowData.type === "Array"
            from: 1; to: 4
            value: Math.max(1, typeRow.rowData.rank || 1)
            onValueModified: typeRow.programRow
                ? root.updateProgram(typeRow.rowIndex, "rank", value)
                : root.updateVariable(typeRow.rowIndex, "rank", value)
        }
        QuiText {
            Layout.preferredWidth: 150
            color: Theme.textSecondary
            font: Qt.font({ pixelSize: 11 })
            elide: Text.ElideRight
            text: root.rowDescription(typeRow.rowData)
        }
    }

    onOpened: Qt.callLater(function() { contentItem.focus = true })

    contentItem: ColumnLayout {
        spacing: 8

        QuiText {
            Layout.fillWidth: true
            wrapMode: Text.WordWrap
            color: Theme.textSecondary
            text: conditionMode
                ? "条件公式必须返回布尔值；结果为真执行成立分支，为假执行否则分支，公式错误不会自动落入否则分支。"
                : "变量端口由稳定标识区分；结果公式按顺序求值，隐藏结果只供后续公式使用，不输出端口。"
        }

        BoundedScrollView {
            id: scroller
            objectName: "expressionScroller"
            Layout.fillWidth: true
            Layout.fillHeight: true
            clip: true
            contentWidth: availableWidth
            ScrollBar.horizontal.policy: ScrollBar.AlwaysOff

            ColumnLayout {
                width: scroller.availableWidth
                spacing: 8

                QuiText { text: "输入变量"; font: Qt.font({ pixelSize: 13, bold: true }); color: Theme.textPrimary }
                Repeater {
                    model: root.variables
                    RowLayout {
                        required property var modelData
                        required property int index
                        Layout.fillWidth: true
                        Field {
                            Layout.preferredWidth: 120
                            text: modelData.name
                            placeholderText: "名称"
                            onTextEdited: root.updateVariable(index, "name", text)
                        }
                        Field {
                            Layout.preferredWidth: 110
                            text: modelData.symbol
                            placeholderText: "公式符号"
                            onTextEdited: root.updateVariable(index, "symbol", text)
                        }
                        RowTypeEditor { rowData: modelData; rowIndex: index; programRow: false; Layout.fillWidth: true }
                        CheckBox {
                            text: "可有缺失"
                            checked: modelData.allowMissing
                            onToggled: root.updateVariable(index, "allowMissing", checked)
                        }
                        Button {
                            text: "删除"
                            onClicked: root.removeVariable(index)
                        }
                    }
                }
                Button { text: "添加变量"; onClicked: root.addVariable(); enabled: !root.conditionMode || root.variables.length < 64 }

                QuiText { text: "公式列表"; font: Qt.font({ pixelSize: 13, bold: true }); color: Theme.textPrimary }
                Repeater {
                    model: root.program
                    RowLayout {
                        required property var modelData
                        required property int index
                        Layout.fillWidth: true
                        Field {
                            Layout.preferredWidth: 110
                            text: modelData.name
                            placeholderText: "名称"
                            onTextEdited: root.updateProgram(index, "name", text)
                        }
                        Field {
                            Layout.preferredWidth: 90
                            text: modelData.symbol
                            placeholderText: "结果符号"
                            onTextEdited: root.updateProgram(index, "symbol", text)
                        }
                        Field {
                            Layout.fillWidth: true
                            text: modelData.expression
                            placeholderText: "例如 all_nonempty(v_gt(radii, 5.0))"
                            onTextEdited: root.updateProgram(index, "expression", text)
                        }
                        RowTypeEditor { rowData: modelData; rowIndex: index; programRow: true }
                        CheckBox {
                            text: "输出端口"
                            visible: !root.conditionMode
                            checked: modelData.exposed
                            onToggled: root.updateProgram(index, "exposed", checked)
                        }
                        Button { text: "删除"; onClicked: root.removeProgram(index) }
                    }
                }
                Button {
                    text: "添加公式"
                    enabled: root.conditionMode ? root.program.length === 0 : true
                    onClicked: root.addProgram()
                }
            }
        }

        QuiText {
            Layout.fillWidth: true
            wrapMode: Text.WordWrap
            color: Theme.textSecondary
            font: Qt.font({ pixelSize: 11 })
            text: "函数：abs min max sum len round sqrt sin cos tan atan2 floor ceil mean median std "
                  + "is_empty is_missing all_nonempty any v_add v_sub v_mul v_div v_gt v_ge v_lt v_le v_eq select concat。"
                  + "三角函数的单位是弧度；条件分支的 ROI 角度使用度。"
        }

        RowLayout {
            Layout.fillWidth: true
            Item { Layout.fillWidth: true }
            Button { text: "取消"; onClicked: root.close() }
            Button { text: "应用"; highlighted: true; onClicked: root.commit() }
        }
    }
}
