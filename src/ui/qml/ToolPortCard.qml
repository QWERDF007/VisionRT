import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import quickui
import VisionRT.Style 1.0

// One declared parameter. The card is layout only: every value field is the same component the run
// parameter panel uses, and every commit goes through the port model, which owns validation.
Rectangle {
    id: root

    required property int index
    required property var model
    property var portsModel: null
    property var listView: null
    property var types: []
    property bool editable: false
    property string stage: "inputs"
    // Re-evaluated whenever the draft row changes, so relation and editor lists never go stale.
    property int revision: 0

    readonly property bool fixed: model.isFixed === true
    readonly property bool output: stage === "outputs"
    readonly property string typeToken: model.typeToken || ""
    readonly property bool numeric: typeToken === "int" || typeToken === "float"
    readonly property bool isImage: typeToken === "image"
    readonly property bool isRoi: typeToken === "roi"
    readonly property bool expanded: advanced.checked

    implicitHeight: layout.implicitHeight + 20
    color: QuiColor.CardBackground
    border.color: root.fixed ? QuiColor.Border : Theme.paramEditorBorder
    border.width: 1
    radius: Theme.radiusBase

    function typeIndex() {
        for (let i = 0; i < root.types.length; ++i)
            if (root.types[i].token === root.typeToken)
                return i
        return -1
    }

    function commit(properties) {
        if (!root.portsModel)
            return false
        return root.portsModel.updatePort(root.index, properties)
    }

    function relationOptions(kind) {
        root.revision
        let options = [{ ref: "", label: "（无）" }]
        if (root.portsModel) {
            const candidates = root.portsModel.relationCandidates(root.index, kind)
            for (let i = 0; i < candidates.length; ++i)
                options.push(candidates[i])
        }
        return options
    }

    function relationIndex(kind, current) {
        const options = relationOptions(kind)
        for (let i = 0; i < options.length; ++i)
            if (options[i].ref === (current || ""))
                return i
        return 0
    }

    function editorOptions() {
        root.revision
        if (!root.portsModel)
            return [{ token: "", label: "默认" }]
        return root.portsModel.editorOptions(root.index)
    }

    function editorIndex() {
        const options = editorOptions()
        for (let i = 0; i < options.length; ++i)
            if (options[i].token === (model.editorType || ""))
                return i
        return 0
    }

    function showsRelation(kind) {
        if (kind === "image")
            return root.isRoi
        if (kind === "alignment")
            return root.isRoi && root.stage === "inputs"
        if (kind === "path_mode")
            return model.editorType === "path"
        if (kind === "same_geometry_as")
            return root.isImage && root.stage === "outputs"
        return false
    }

    Connections {
        target: root.portsModel
        function onDataChanged() { root.revision++ }
        function onModelReset() { root.revision++ }
        function onRowsInserted() { root.revision++ }
        function onRowsRemoved() { root.revision++ }
        function onRowsMoved() { root.revision++ }
    }

    ColumnLayout {
        id: layout
        anchors.fill: parent
        anchors.margins: 10
        spacing: 6

        RowLayout {
            Layout.fillWidth: true
            spacing: 6

            // The number is the port identity and the drag handle: reordering never renumbers.
            Rectangle {
                id: numberChip
                Layout.preferredWidth: 34
                Layout.preferredHeight: 22
                radius: 3
                color: rowDrag.active ? QuiColor.Highlight : QuiColor.WindowBackground
                border.color: QuiColor.Border
                Label {
                    anchors.centerIn: parent
                    text: String(model.portId)
                    font.pixelSize: 11
                    font.bold: true
                    color: rowDrag.active ? "#ffffff" : (root.fixed ? QuiColor.FontCaption : QuiColor.FontPrimary)
                }
                HoverHandler {
                    enabled: root.editable && !root.fixed
                    cursorShape: rowDrag.active ? Qt.ClosedHandCursor : Qt.OpenHandCursor
                }
                DragHandler {
                    id: rowDrag
                    target: null
                    xAxis.enabled: false
                    enabled: root.editable && !root.fixed && root.listView !== null

                    function dropTarget() {
                        if (!rowDrag.active || root.listView === null || !root.portsModel)
                            return -1
                        const point = numberChip.mapToItem(root.listView.contentItem,
                                                           numberChip.width / 2,
                                                           numberChip.height / 2 + rowDrag.activeTranslation.y)
                        let row = root.listView.indexAt(point.x, point.y)
                        if (row < 0)
                            row = point.y < 0 ? 0 : root.listView.count - 1
                        return Math.max(row, root.portsModel.fixedRowCount())
                    }

                    property int targetRow: -1

                    onActiveChanged: {
                        if (active) {
                            root.listView.interactive = false
                            targetRow = root.index
                            return
                        }
                        root.listView.interactive = true
                        const target = targetRow
                        targetRow = -1
                        if (target < 0 || target === root.index)
                            return
                        root.portsModel.movePort(root.index, target)
                    }
                    onActiveTranslationChanged: if (active) targetRow = dropTarget()
                }
            }

            Label {
                Layout.fillWidth: true
                text: model.nameZh
                elide: Text.ElideRight
                font.bold: true
                color: QuiColor.FontPrimary
            }
            Label {
                visible: root.fixed
                text: "系统"
                font.pixelSize: Theme.fontSizeCaption
                color: QuiColor.FontCaption
            }
            Button {
                Layout.preferredWidth: 24
                Layout.preferredHeight: 22
                text: "×"
                flat: true
                enabled: root.editable && !root.fixed
                ToolTip.visible: hovered
                ToolTip.text: root.fixed ? "系统端口不可删除" : "删除参数"
                onClicked: if (root.portsModel) root.portsModel.removePort(root.index)
            }
        }

        RowLayout {
            Layout.fillWidth: true
            spacing: 6
            Label { text: "名称"; Layout.preferredWidth: 34; font.pixelSize: Theme.fontSizeCaption; color: QuiColor.FontCaption }
            ParameterTextField {
                Layout.fillWidth: true
                text: model.nameZh
                readOnly: !root.editable || root.fixed
                onCommitRequested: {
                    if (text !== model.nameZh)
                        root.commit({ nameZh: text })
                }
                onCancelRequested: text = model.nameZh
            }
        }

        RowLayout {
            Layout.fillWidth: true
            spacing: 6
            Label { text: "英文名"; Layout.preferredWidth: 34; font.pixelSize: Theme.fontSizeCaption; color: QuiColor.FontCaption }
            ParameterTextField {
                Layout.fillWidth: true
                text: model.nameEn
                readOnly: !root.editable || root.fixed
                onCommitRequested: if (text !== model.nameEn) root.commit({ nameEn: text })
                onCancelRequested: text = model.nameEn
            }
        }

        RowLayout {
            Layout.fillWidth: true
            spacing: 6
            Label { text: "类型"; Layout.preferredWidth: 34; font.pixelSize: Theme.fontSizeCaption; color: QuiColor.FontCaption }
            QuiComboBox {
                id: typeCombo
                Layout.fillWidth: true
                implicitHeight: Theme.paramEditorHeight
                enabled: root.editable && !root.fixed
                model: root.types
                textRole: "label"
                currentIndex: root.typeIndex()
                onActivated: function(index) {
                    const token = root.types[index].token
                    if (token === root.typeToken)
                        return
                    const impact = root.portsModel ? root.portsModel.typeChangeImpact(root.index, token) : []
                    if (impact.length > 0) {
                        typeImpactDialog.impact = impact
                        typeImpactDialog.pendingToken = token
                        typeImpactDialog.open()
                        return
                    }
                    root.portsModel.changeType(root.index, token)
                }
            }
        }

        // Default value: one shared editor per type, exactly as the run parameter panel renders it.
        RowLayout {
            Layout.fillWidth: true
            spacing: 6
            visible: !root.output
            Label { text: "默认值"; Layout.preferredWidth: 34; font.pixelSize: Theme.fontSizeCaption; color: QuiColor.FontCaption }

            IntegerParameterEditor {
                Layout.fillWidth: true
                visible: root.typeToken === "int"
                enabled: root.editable && !root.fixed
                valueText: model.defaultValue
                canDecrement: true
                canIncrement: true
                commitValue: function(text) { return root.commit({ defaultValue: text }) }
                stepValue: function(direction, text) {
                    if (!root.editable)
                        return false
                    const base = Number(text)
                    return root.commit({ defaultValue: String((isNaN(base) ? 0 : base) + direction) })
                }
            }

            QuiSpinEditor {
                Layout.fillWidth: true
                visible: root.typeToken === "float"
                enabled: root.editable && !root.fixed
                flat: true
                decimals: {
                    const text = String(model.defaultValue || "")
                    const dot = text.indexOf(".")
                    return dot < 0 ? 0 : Math.min(6, text.length - dot - 1)
                }
                step: 1
                minValue: -1e12
                maxValue: 1e12
                value: Number(model.defaultValue || 0)
                commitValue: function(number) { return root.commit({ defaultValue: String(number) }) }
            }

            Item {
                Layout.fillWidth: true
                visible: root.typeToken === "bool"
                implicitHeight: Theme.paramEditorHeight
                QuiToggleSwitch {
                    anchors.left: parent.left
                    anchors.verticalCenter: parent.verticalCenter
                    enabled: root.editable && !root.fixed
                    checked: String(model.defaultValue) === "true"
                    clickListener: function() {
                        if (root.editable)
                            root.commit({ defaultValue: checked ? "false" : "true" })
                    }
                }
            }

            ParameterTextField {
                Layout.fillWidth: true
                visible: !root.numeric && root.typeToken !== "bool"
                text: model.defaultValue
                readOnly: !root.editable || root.fixed
                onCommitRequested: if (text !== model.defaultValue) root.commit({ defaultValue: text })
                onCancelRequested: text = model.defaultValue
            }
        }

        RowLayout {
            Layout.fillWidth: true
            spacing: 6
            visible: !root.output && root.numeric
            Label { text: "范围"; Layout.preferredWidth: 34; font.pixelSize: Theme.fontSizeCaption; color: QuiColor.FontCaption }
            IntegerParameterEditor {
                Layout.fillWidth: true
                enabled: root.editable && !root.fixed
                valueText: model.minValue
                canDecrement: true
                canIncrement: true
                commitValue: function(text) { return root.commit({ min: text }) }
                stepValue: function(direction, text) {
                    const base = Number(text)
                    return root.commit({ min: String((isNaN(base) ? 0 : base) + direction) })
                }
            }
            Label { text: "至"; font.pixelSize: Theme.fontSizeCaption; color: QuiColor.FontCaption }
            IntegerParameterEditor {
                Layout.fillWidth: true
                enabled: root.editable && !root.fixed
                valueText: model.maxValue
                canDecrement: true
                canIncrement: true
                commitValue: function(text) { return root.commit({ max: text }) }
                stepValue: function(direction, text) {
                    const base = Number(text)
                    return root.commit({ max: String((isNaN(base) ? 0 : base) + direction) })
                }
            }
        }

        // The author's explicit choice to offer this Image port in the result image selector.
        RowLayout {
            Layout.fillWidth: true
            spacing: 6
            visible: root.isImage
            Label { text: "图像区域显示"; font.pixelSize: Theme.fontSizeCaption; color: QuiColor.FontCaption }
            Item { Layout.fillWidth: true }
            QuiToggleSwitch {
                enabled: root.editable && !root.fixed
                checked: model.showImage === true
                clickListener: function() {
                    if (root.editable)
                        root.commit({ showImage: !checked })
                }
            }
        }

        RowLayout {
            Layout.fillWidth: true
            visible: !root.fixed
            Button {
                id: advanced
                checkable: true
                flat: true
                text: checked ? "收起详情" : "更多设置"
                font.pixelSize: Theme.fontSizeCaption
            }
            Item { Layout.fillWidth: true }
        }

        ColumnLayout {
            Layout.fillWidth: true
            spacing: 6
            visible: root.expanded && !root.fixed

            RowLayout {
                Layout.fillWidth: true
                spacing: 6
                visible: root.typeToken === "string"
                Label { text: "编辑方式"; Layout.preferredWidth: 56; font.pixelSize: Theme.fontSizeCaption; color: QuiColor.FontCaption }
                QuiComboBox {
                    Layout.fillWidth: true
                    implicitHeight: Theme.paramEditorHeight
                    enabled: root.editable
                    model: root.editorOptions()
                    textRole: "label"
                    currentIndex: root.editorIndex()
                    onActivated: function(index) {
                        root.commit({ editor: root.editorOptions()[index].token })
                    }
                }
            }

            RowLayout {
                Layout.fillWidth: true
                spacing: 6
                visible: root.showsRelation("image")
                Label { text: "关联图像"; Layout.preferredWidth: 56; font.pixelSize: Theme.fontSizeCaption; color: QuiColor.FontCaption }
                QuiComboBox {
                    Layout.fillWidth: true
                    implicitHeight: Theme.paramEditorHeight
                    enabled: root.editable
                    model: root.relationOptions("image")
                    textRole: "label"
                    currentIndex: root.relationIndex("image", model.imageRef)
                    onActivated: function(index) {
                        root.commit({ image: root.relationOptions("image")[index].ref })
                    }
                }
            }

            RowLayout {
                Layout.fillWidth: true
                spacing: 6
                visible: root.showsRelation("alignment")
                Label { text: "对齐关系"; Layout.preferredWidth: 56; font.pixelSize: Theme.fontSizeCaption; color: QuiColor.FontCaption }
                QuiComboBox {
                    Layout.fillWidth: true
                    implicitHeight: Theme.paramEditorHeight
                    enabled: root.editable
                    model: root.relationOptions("alignment")
                    textRole: "label"
                    currentIndex: root.relationIndex("alignment", model.alignmentRef)
                    onActivated: function(index) {
                        root.commit({ alignment: root.relationOptions("alignment")[index].ref })
                    }
                }
            }

            RowLayout {
                Layout.fillWidth: true
                spacing: 6
                visible: root.showsRelation("same_geometry_as")
                Label { text: "同几何"; Layout.preferredWidth: 56; font.pixelSize: Theme.fontSizeCaption; color: QuiColor.FontCaption }
                QuiComboBox {
                    Layout.fillWidth: true
                    implicitHeight: Theme.paramEditorHeight
                    enabled: root.editable
                    model: root.relationOptions("same_geometry_as")
                    textRole: "label"
                    currentIndex: root.relationIndex("same_geometry_as", model.sameGeometryRef)
                    onActivated: function(index) {
                        root.commit({ same_geometry_as: root.relationOptions("same_geometry_as")[index].ref })
                    }
                }
            }

            RowLayout {
                Layout.fillWidth: true
                spacing: 6
                visible: root.showsRelation("path_mode")
                Label { text: "路径模式"; Layout.preferredWidth: 56; font.pixelSize: Theme.fontSizeCaption; color: QuiColor.FontCaption }
                QuiComboBox {
                    Layout.fillWidth: true
                    implicitHeight: Theme.paramEditorHeight
                    enabled: root.editable
                    model: root.relationOptions("path_mode")
                    textRole: "label"
                    currentIndex: root.relationIndex("path_mode", model.pathModeRef)
                    onActivated: function(index) {
                        root.commit({ path_mode: root.relationOptions("path_mode")[index].ref })
                    }
                }
            }

            RowLayout {
                Layout.fillWidth: true
                spacing: 6
                visible: !root.output && (root.typeToken === "int" || root.typeToken === "float"
                                         || root.typeToken === "string" || root.typeToken === "bool")
                Label { text: "候选值"; Layout.preferredWidth: 56; font.pixelSize: Theme.fontSizeCaption; color: QuiColor.FontCaption }
                TextArea {
                    id: optionsField
                    Layout.fillWidth: true
                    implicitHeight: 54
                    enabled: root.editable
                    text: model.optionsText
                    wrapMode: TextArea.Wrap
                    font.pixelSize: Theme.fontSizeCaption
                    onActiveFocusChanged: {
                        if (!activeFocus && text !== model.optionsText)
                            root.commit({ options: text })
                    }
                }
            }
        }
    }

    Dialog {
        id: typeImpactDialog
        property var impact: []
        property string pendingToken: ""
        title: "改变类型会清除以下内容"
        modal: true
        standardButtons: Dialog.Ok | Dialog.Cancel
        width: 420
        Label {
            width: 380
            wrapMode: Text.WordWrap
            text: typeImpactDialog.impact.join("\n")
        }
        onAccepted: if (root.portsModel) root.portsModel.changeType(root.index, pendingToken)
    }
}
