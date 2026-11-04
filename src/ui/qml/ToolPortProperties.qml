import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import quickui
import VisionRT.Style 1.0

Item {
    id: root

    property var controller: null
    property int selectedRow: -1
    // portAt() is a plain call, so the panel needs an explicit nudge whenever the draft row it
    // shows is rewritten — a type change resets the default and clears the range constraints.
    property int revision: 0
    property var portData: {
        revision
        return (controller && controller.portModel && selectedRow >= 0)
               ? controller.portModel.portAt(selectedRow) : null
    }

    // What the current port's relation of this kind may point at. Reading `revision` makes the
    // list follow every draft change, so it never offers a stale reference.
    function relationOptions(kind) {
        revision
        if (!controller || !controller.portModel || selectedRow < 0)
            return []
        let options = [{ ref: "", label: "（无）" }]
        const candidates = controller.portModel.relationCandidates(selectedRow, kind)
        for (let i = 0; i < candidates.length; ++i)
            options.push(candidates[i])
        return options
    }

    function relationIndex(kind, current) {
        const options = relationOptions(kind)
        for (let i = 0; i < options.length; ++i)
            if (options[i].ref === (current || ""))
                return i
        return 0
    }

    function relationVisible(kind) {
        if (!controller || !root.portData)
            return false
        const token = root.portData.typeToken
        if (kind === "image")
            return token === "roi"
        if (kind === "alignment")
            return token === "roi" && controller.currentStage === "inputs"
        if (kind === "path_mode")
            return root.portData.editorType === "path"
        if (kind === "same_geometry_as")
            return token === "image" && controller.currentStage === "outputs"
        return false
    }

    function optionsVisible() {
        if (!controller || !root.portData || controller.currentStage === "outputs")
            return false
        const token = root.portData.typeToken
        return token === "int" || token === "float" || token === "string" || token === "bool"
    }

    // Editor kinds that render the row's type; the list comes from the same rule the declaration
    // reconciliation uses, so a chosen editor is never silently dropped later.
    function editorOptions() {
        revision
        if (!controller || !controller.portModel || selectedRow < 0)
            return [{ token: "", label: "默认" }]
        return controller.portModel.editorOptions(selectedRow)
    }

    function editorIndex() {
        const options = editorOptions()
        const current = root.portData ? (root.portData.editorType || "") : ""
        for (let i = 0; i < options.length; ++i)
            if (options[i].token === current)
                return i
        return 0
    }

    Connections {
        target: root.controller ? root.controller.portModel : null
        function onDataChanged() { root.revision++ }
        function onModelReset() { root.revision++ }
    }

    implicitWidth: 320
    implicitHeight: 400

    Rectangle {
        anchors.fill: parent
        color: QuiColor.CardBackground
        border.color: QuiColor.Border
        border.width: 1
        radius: 4
    }

    ScrollView {
        id: scroll
        anchors.fill: parent
        anchors.margins: 12
        clip: true

        ColumnLayout {
            width: scroll.width - 24
            spacing: 10

            Label {
                text: "参数属性"
                font.bold: true
                font.pixelSize: 15
                color: QuiColor.FontPrimary
            }

            Rectangle {
                Layout.fillWidth: true
                height: 1
                color: QuiColor.Border
            }

            // Fixed row banner
            Rectangle {
                Layout.fillWidth: true
                height: 32
                color: QuiColor.Background
                border.color: QuiColor.Border
                radius: 4
                visible: root.portData ? root.portData.isFixed : false

                RowLayout {
                    anchors.fill: parent
                    anchors.margins: 6
                    spacing: 6
                    Label {
                        text: "🔒 系统保留参数（只读）"
                        color: Theme.warning
                        font.pixelSize: 12
                    }
                }
            }

            // Empty selection note
            Label {
                text: "请选择左侧列表中的参数查看或编辑属性"
                color: QuiColor.FontCaption
                font.pixelSize: 13
                visible: !root.portData
            }

            // Property fields (when portData is available)
            ColumnLayout {
                Layout.fillWidth: true
                spacing: 8
                visible: root.portData !== null

                // Port ID (Read-only)
                RowLayout {
                    Layout.fillWidth: true
                    Label {
                        text: "参数编号 (ID)："
                        Layout.preferredWidth: 100
                        color: QuiColor.FontCaption
                    }
                    Label {
                        text: root.portData ? String(root.portData.portId) : ""
                        font.bold: true
                        color: QuiColor.FontPrimary
                    }
                }

                // Name ZH
                RowLayout {
                    Layout.fillWidth: true
                    Label {
                        text: "参数名称："
                        Layout.preferredWidth: 100
                        color: QuiColor.FontCaption
                    }
                    TextField {
                        id: nameZhField
                        Layout.fillWidth: true
                        text: root.portData ? root.portData.nameZh : ""
                        enabled: root.portData && !root.portData.isFixed
                        onEditingFinished: {
                            if (root.controller && root.portData && !root.portData.isFixed) {
                                root.controller.portModel.updatePort(root.selectedRow, { nameZh: text })
                            }
                        }
                    }
                }

                // Name EN / Alias
                RowLayout {
                    Layout.fillWidth: true
                    Label {
                        text: "英文别名："
                        Layout.preferredWidth: 100
                        color: QuiColor.FontCaption
                    }
                    TextField {
                        id: nameEnField
                        Layout.fillWidth: true
                        text: root.portData ? root.portData.nameEn : ""
                        placeholderText: "可选"
                        enabled: root.portData && !root.portData.isFixed
                        onEditingFinished: {
                            if (root.controller && root.portData && !root.portData.isFixed) {
                                root.controller.portModel.updatePort(root.selectedRow, { nameEn: text })
                            }
                        }
                    }
                }

                // Type
                RowLayout {
                    Layout.fillWidth: true
                    Label {
                        text: "数据类型："
                        Layout.preferredWidth: 100
                        color: QuiColor.FontCaption
                    }
                    ComboBox {
                        id: typeCombo
                        Layout.fillWidth: true
                        model: root.controller ? root.controller.supportedTypes : []
                        textRole: "label"
                        valueRole: "token"
                        enabled: root.portData && !root.portData.isFixed
                        currentIndex: {
                            if (!root.portData || !root.controller) return 0
                            for (let i = 0; i < count; ++i) {
                                if (model[i].token === root.portData.typeToken)
                                    return i
                            }
                            return 0
                        }
                        onActivated: function(index) {
                            if (!root.controller || !root.portData || root.portData.isFixed)
                                return
                            const token = model[index].token
                            if (token === root.portData.typeToken)
                                return
                            // A type change can drop values, constraints and relations. Ask first,
                            // showing exactly what the change would clear.
                            const impact = root.controller.portModel.typeChangeImpact(root.selectedRow, token)
                            if (impact.length === 0) {
                                root.controller.portModel.changeType(root.selectedRow, token)
                                return
                            }
                            typeChangeDialog.pendingRow = root.selectedRow
                            typeChangeDialog.pendingToken = token
                            typeChangeDialog.clearedFields = impact
                            typeChangeDialog.open()
                        }
                    }
                }

                // Value editor (inputs and init only; an output carries no editor in a declaration)
                RowLayout {
                    Layout.fillWidth: true
                    visible: root.controller && root.controller.currentStage !== "outputs"
                             && root.portData && !root.portData.isFixed
                    Label {
                        text: "编辑器："
                        Layout.preferredWidth: 100
                        color: QuiColor.FontCaption
                    }
                    ComboBox {
                        id: editorCombo
                        Layout.fillWidth: true
                        model: root.editorOptions()
                        textRole: "label"
                        enabled: root.portData && !root.portData.isFixed
                        currentIndex: root.editorIndex()
                        onActivated: function(index) {
                            if (!root.controller || !root.portData || root.portData.isFixed)
                                return
                            root.controller.portModel.updatePort(root.selectedRow, { editor: model[index].token })
                        }
                    }
                }

                // Default Value (Only for inputs and init)
                RowLayout {
                    Layout.fillWidth: true
                    visible: root.controller && root.controller.currentStage !== "outputs"
                    Label {
                        text: "默认值："
                        Layout.preferredWidth: 100
                        color: QuiColor.FontCaption
                    }
                    TextField {
                        id: defaultValField
                        Layout.fillWidth: true
                        text: root.portData ? root.portData.defaultValue : ""
                        enabled: root.portData && !root.portData.isFixed && root.portData.typeToken !== "image"
                        onEditingFinished: {
                            if (root.controller && root.portData && !root.portData.isFixed) {
                                root.controller.portModel.updatePort(root.selectedRow, { defaultValue: text })
                            }
                        }
                    }
                }

                // Min constraint (for int/float)
                RowLayout {
                    Layout.fillWidth: true
                    visible: root.portData && (root.portData.typeToken === "int" || root.portData.typeToken === "float")
                             && root.controller && root.controller.currentStage !== "outputs"
                    Label {
                        text: "最小值 (Min)："
                        Layout.preferredWidth: 100
                        color: QuiColor.FontCaption
                    }
                    TextField {
                        Layout.fillWidth: true
                        text: root.portData ? root.portData.minValue : ""
                        enabled: root.portData && !root.portData.isFixed
                        onEditingFinished: {
                            if (root.controller && root.portData && !root.portData.isFixed) {
                                root.controller.portModel.updatePort(root.selectedRow, { min: text })
                            }
                        }
                    }
                }

                // Max constraint (for int/float)
                RowLayout {
                    Layout.fillWidth: true
                    visible: root.portData && (root.portData.typeToken === "int" || root.portData.typeToken === "float")
                             && root.controller && root.controller.currentStage !== "outputs"
                    Label {
                        text: "最大值 (Max)："
                        Layout.preferredWidth: 100
                        color: QuiColor.FontCaption
                    }
                    TextField {
                        Layout.fillWidth: true
                        text: root.portData ? root.portData.maxValue : ""
                        enabled: root.portData && !root.portData.isFixed
                        onEditingFinished: {
                            if (root.controller && root.portData && !root.portData.isFixed) {
                                root.controller.portModel.updatePort(root.selectedRow, { max: text })
                            }
                        }
                    }
                }

                // Tab / Page (for inputs). Only the pages a declaration may name are offered.
                RowLayout {
                    Layout.fillWidth: true
                    visible: root.controller && root.controller.currentStage === "inputs"
                    Label {
                        text: "所属页面："
                        Layout.preferredWidth: 100
                        color: QuiColor.FontCaption
                    }
                    ComboBox {
                        id: tabCombo
                        Layout.fillWidth: true
                        model: root.controller ? root.controller.tabs : []
                        enabled: root.portData && !root.portData.isFixed
                        currentIndex: {
                            if (!root.portData) return 0
                            const name = root.portData.tabName || "基本输入"
                            for (let i = 0; i < count; ++i)
                                if (model[i] === name) return i
                            return 0
                        }
                        onActivated: function(index) {
                            if (root.controller && root.portData && !root.portData.isFixed)
                                root.controller.portModel.updatePort(root.selectedRow, { tab: model[index] })
                        }
                    }
                }

                // Group (optional free-form heading inside the page)
                RowLayout {
                    Layout.fillWidth: true
                    Label {
                        text: "分组："
                        Layout.preferredWidth: 100
                        color: QuiColor.FontCaption
                    }
                    TextField {
                        Layout.fillWidth: true
                        text: root.portData ? root.portData.groupName : ""
                        placeholderText: "可选"
                        enabled: root.portData && !root.portData.isFixed
                        onEditingFinished: {
                            if (root.controller && root.portData && !root.portData.isFixed)
                                root.controller.portModel.updatePort(root.selectedRow, { group: text })
                        }
                    }
                }

                // Step (numeric inputs and init only)
                RowLayout {
                    Layout.fillWidth: true
                    visible: root.portData && (root.portData.typeToken === "int" || root.portData.typeToken === "float")
                             && root.controller && root.controller.currentStage !== "outputs"
                    Label {
                        text: "步长 (Step)："
                        Layout.preferredWidth: 100
                        color: QuiColor.FontCaption
                    }
                    TextField {
                        Layout.fillWidth: true
                        text: root.portData ? root.portData.stepValue : ""
                        enabled: root.portData && !root.portData.isFixed
                        onEditingFinished: {
                            if (root.controller && root.portData && !root.portData.isFixed)
                                root.controller.portModel.updatePort(root.selectedRow, { step: text })
                        }
                    }
                }

                // Decimals (float only)
                RowLayout {
                    Layout.fillWidth: true
                    visible: root.portData && root.portData.typeToken === "float"
                             && root.controller && root.controller.currentStage !== "outputs"
                    Label {
                        text: "小数位："
                        Layout.preferredWidth: 100
                        color: QuiColor.FontCaption
                    }
                    TextField {
                        Layout.fillWidth: true
                        text: root.portData ? root.portData.decimalsValue : ""
                        placeholderText: "可选"
                        enabled: root.portData && !root.portData.isFixed
                        onEditingFinished: {
                            if (root.controller && root.portData && !root.portData.isFixed)
                                root.controller.portModel.updatePort(root.selectedRow, { decimals: text })
                        }
                    }
                }

                // Relations: only ports the declaration accepts are offered, and "（无）" clears.
                Repeater {
                    model: [
                        { kind: "image",            label: "图像关联",   current: "imageRef" },
                        { kind: "alignment",        label: "对齐关系",   current: "alignmentRef" },
                        { kind: "path_mode",        label: "路径模式",   current: "pathModeRef" },
                        { kind: "same_geometry_as", label: "同几何关系", current: "sameGeometryRef" }
                    ]

                    RowLayout {
                        Layout.fillWidth: true
                        visible: root.relationVisible(modelData.kind)

                        Label {
                            text: modelData.label + "："
                            Layout.preferredWidth: 100
                            color: QuiColor.FontCaption
                        }
                        ComboBox {
                            Layout.fillWidth: true
                            model: root.relationOptions(modelData.kind)
                            textRole: "label"
                            enabled: root.portData && !root.portData.isFixed
                            currentIndex: root.portData
                                          ? root.relationIndex(modelData.kind, root.portData[modelData.current])
                                          : 0
                            onActivated: function(index) {
                                if (!root.controller || !root.portData || root.portData.isFixed)
                                    return
                                let patch = {}
                                patch[modelData.kind] = model[index].ref
                                root.controller.portModel.updatePort(root.selectedRow, patch)
                            }
                        }
                    }
                }

                // Declared candidate values, one per line; the run-time parameter panel renders a
                // selection list only when a port actually declares them.
                RowLayout {
                    Layout.fillWidth: true
                    visible: root.optionsVisible()

                    Label {
                        text: "候选值（每行一个）："
                        Layout.preferredWidth: 100
                        color: QuiColor.FontCaption
                    }
                    TextArea {
                        Layout.fillWidth: true
                        Layout.preferredHeight: 64
                        text: root.portData ? root.portData.optionsText : ""
                        enabled: root.portData && !root.portData.isFixed
                        wrapMode: TextArea.NoWrap
                        onActiveFocusChanged: {
                            if (activeFocus || !root.controller || !root.portData || root.portData.isFixed)
                                return
                            root.controller.portModel.updatePort(root.selectedRow, { options: text })
                        }
                    }
                }
            }
        }
    }

    Dialog {
        id: typeChangeDialog
        property int pendingRow: -1
        property string pendingToken: ""
        property var clearedFields: []
        title: "修改数据类型"
        modal: true
        standardButtons: Dialog.Ok | Dialog.Cancel
        width: 420

        ColumnLayout {
            anchors.fill: parent
            spacing: 8

            Label {
                Layout.fillWidth: true
                text: "改成该类型将清除以下内容，其余字段保持原值："
                wrapMode: Text.WordWrap
                color: QuiColor.FontPrimary
            }
            Repeater {
                model: typeChangeDialog.clearedFields
                Label {
                    text: "· " + modelData
                    font.pixelSize: 12
                    color: QuiColor.FontCaption
                }
            }
        }

        onAccepted: {
            if (root.controller && root.controller.portModel)
                root.controller.portModel.changeType(typeChangeDialog.pendingRow, typeChangeDialog.pendingToken)
        }
        onRejected: root.revision++
    }
}
