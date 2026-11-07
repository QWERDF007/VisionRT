import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import quickui
import VisionRT.Style 1.0

// Modern parameter card matching design specification.
// Rigorous horizontal label/input alignment with port ID badge and Theme tokens.
Rectangle {
    id: root

    required property int index
    required property var model
    property var portsModel: null
    property var listView: null
    property var types: []
    property bool editable: false
    property string stage: "inputs"
    property int revision: 0

    readonly property bool fixed: model.isFixed === true
    readonly property bool output: stage === "outputs"
    readonly property string typeToken: model.typeToken || ""
    readonly property bool numeric: typeToken === "int" || typeToken === "float"
    readonly property bool isImage: typeToken === "image"
    readonly property bool isRoi: typeToken === "roi"
    readonly property bool hasAdvanced: root.typeToken === "string" ||
                                       (showsRelation("image") && model.imageRef !== "") ||
                                       (showsRelation("alignment") && model.alignmentRef !== "") ||
                                       (showsRelation("same_geometry_as") && model.sameGeometryRef !== "") ||
                                       (showsRelation("path_mode") && model.pathModeRef !== "") ||
                                       (typeToken === "string" && model.editorType !== "") ||
                                       (model.optionsText && model.optionsText !== "")
    property bool advancedChecked: false
    readonly property real lowerBound: model.minValue !== "" && !isNaN(Number(model.minValue)) ? Number(model.minValue) : -2147483648
    readonly property real upperBound: model.maxValue !== "" && !isNaN(Number(model.maxValue)) ? Number(model.maxValue) : 2147483647

    implicitHeight: cardLayout.implicitHeight + 24
    color: Theme.surfaceBg
    border.color: Theme.border
    border.width: 1
    radius: 8

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
        id: cardLayout
        anchors.fill: parent
        anchors.margins: 12
        spacing: 8

        // Header: Badge with port ID on the left, delete button on the right
        RowLayout {
            Layout.fillWidth: true
            spacing: 6

            Rectangle {
                id: numberChip
                Layout.preferredWidth: 26
                Layout.preferredHeight: 20
                radius: Theme.radiusSmall
                color: rowDrag.active ? Theme.highlight : Theme.treeItemSelectedBg
                border.color: rowDrag.active ? Theme.highlight : "transparent"

                Label {
                    anchors.centerIn: parent
                    text: String(model.portId)
                    font.pixelSize: 12
                    font.bold: true
                    color: rowDrag.active ? "#FFFFFF" : Theme.highlight
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

            Item { Layout.fillWidth: true }

            // Delete action button
            Rectangle {
                Layout.preferredWidth: 22
                Layout.preferredHeight: 20
                radius: Theme.radiusSmall
                color: (root.editable && !root.fixed && delMouse.containsMouse) ? Theme.menuButtonHoverBg : "transparent"

                Text {
                    anchors.centerIn: parent
                    text: "✕"
                    font.pixelSize: 11
                    color: root.fixed ? Theme.disabledText : (delMouse.containsMouse ? Theme.error : Theme.iconMuted)
                }

                MouseArea {
                    id: delMouse
                    anchors.fill: parent
                    hoverEnabled: true
                    enabled: root.editable && !root.fixed
                    cursorShape: (root.editable && !root.fixed) ? Qt.PointingHandCursor : Qt.ArrowCursor
                    onClicked: if (root.portsModel) root.portsModel.removePort(root.index)
                }
            }
        }

        // Field 1: 中文名
        RowLayout {
            Layout.fillWidth: true
            spacing: 8
            Label {
                text: "中文名:"
                Layout.preferredWidth: Theme.cardLabelWidth
                horizontalAlignment: Text.AlignRight
                font: Theme.parameterFont
                color: Theme.textSecondary
            }
            ParameterTextField {
                Layout.fillWidth: true
                flat: false
                implicitHeight: Theme.paramEditorHeight
                text: model.nameZh
                readOnly: !root.editable || root.fixed
                onCommitRequested: {
                    if (text !== model.nameZh)
                        root.commit({ nameZh: text })
                }
                onCancelRequested: text = model.nameZh
            }
        }

        // Field 2: 英文名
        RowLayout {
            Layout.fillWidth: true
            spacing: 8
            Label {
                text: "英文名:"
                Layout.preferredWidth: Theme.cardLabelWidth
                horizontalAlignment: Text.AlignRight
                font: Theme.parameterFont
                color: Theme.textSecondary
            }
            ParameterTextField {
                Layout.fillWidth: true
                flat: false
                implicitHeight: Theme.paramEditorHeight
                text: model.nameEn
                readOnly: !root.editable || root.fixed
                onCommitRequested: if (text !== model.nameEn) root.commit({ nameEn: text })
                onCancelRequested: text = model.nameEn
            }
        }

        // Field 3: 类型
        RowLayout {
            Layout.fillWidth: true
            spacing: 8
            Label {
                text: "类型:"
                Layout.preferredWidth: Theme.cardLabelWidth
                horizontalAlignment: Text.AlignRight
                font: Theme.parameterFont
                color: Theme.textSecondary
            }
            QuiComboBox {
                id: typeCombo
                Layout.fillWidth: true
                implicitHeight: Theme.paramEditorHeight
                enabled: root.editable && !root.fixed
                model: root.types
                textRole: "label"
                currentIndex: root.typeIndex()
                normalColor: Theme.surfaceBg
                disableColor: Theme.surfaceBg
                background: Rectangle {
                    radius: Theme.radiusSmall
                    border.width: 1
                    border.color: typeCombo.activeFocus ? Theme.paramEditorFocusBorder : Theme.border
                    color: Theme.surfaceBg
                }
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

        // Field: 默认值。A business output can carry one as well: the declaration decides what a
        // missing value means, so the editor offers the same field in both directions. A path string
        // is edited in the same single picker the parameter panel uses, and a numeric default is
        // bounded by the same limits the contract stores.
        RowLayout {
            Layout.fillWidth: true
            spacing: 8
            visible: !root.isImage && root.typeToken !== "bool"
            Label {
                text: "默认值:"
                Layout.preferredWidth: Theme.cardLabelWidth
                horizontalAlignment: Text.AlignRight
                font: Theme.parameterFont
                color: Theme.textSecondary
            }
            ParameterTextField {
                id: defaultField
                Layout.fillWidth: true
                flat: false
                implicitHeight: Theme.paramEditorHeight
                text: model.defaultValue
                readOnly: !root.editable || root.fixed
                actionIcon: model.editorType === "path" ? QuiFontIcon.FolderOpen : 0
                inputItem.validator: root.numeric
                                     ? (root.typeToken === "int" ? intValidator : realValidator)
                                     : null
                onActionRequested: defaultPicker.open()
                onCommitRequested: if (text !== model.defaultValue) root.commit({ defaultValue: text })
                onCancelRequested: text = model.defaultValue
            }
        }

        IntValidator {
            id: intValidator
            bottom: root.lowerBound
            top: root.upperBound
        }
        DoubleValidator {
            id: realValidator
            bottom: root.lowerBound
            top: root.upperBound
            notation: DoubleValidator.StandardNotation
        }
        QuiFileFolderDialog {
            id: defaultPicker
            title: "选择路径"
            dialogMode: QuiFileFolderDialog.DialogMode.FileOrFolder
            onAccepted: function(path) {
                defaultField.text = path
                root.commit({ defaultValue: path })
            }
        }

        // Field: 布尔类型默认值
        RowLayout {
            Layout.fillWidth: true
            spacing: 8
            visible: !root.output && root.typeToken === "bool"
            Label {
                text: "默认值:"
                Layout.preferredWidth: Theme.cardLabelWidth
                horizontalAlignment: Text.AlignRight
                font: Theme.parameterFont
                color: Theme.textSecondary
            }
            QuiToggleSwitch {
                enabled: root.editable && !root.fixed
                checked: String(model.defaultValue) === "true"
                clickListener: function() {
                    if (root.editable)
                        root.commit({ defaultValue: checked ? "false" : "true" })
                }
            }
            Item { Layout.fillWidth: true }
        }

        // Fields: 下限 & 上限 (Input parameters, numeric types, only for editable custom parameters)
        RowLayout {
            Layout.fillWidth: true
            spacing: 8
            visible: root.numeric && (!root.fixed || model.minValue !== "" || model.maxValue !== "")
            Label {
                text: "下限:"
                Layout.preferredWidth: Theme.cardLabelWidth
                horizontalAlignment: Text.AlignRight
                font: Theme.parameterFont
                color: Theme.textSecondary
            }
            ParameterTextField {
                Layout.fillWidth: true
                flat: false
                implicitHeight: Theme.paramEditorHeight
                text: model.minValue
                readOnly: !root.editable || root.fixed
                onCommitRequested: if (text !== model.minValue) root.commit({ min: text })
                onCancelRequested: text = model.minValue
            }
        }

        RowLayout {
            Layout.fillWidth: true
            spacing: 8
            visible: root.numeric && (!root.fixed || model.minValue !== "" || model.maxValue !== "")
            Label {
                text: "上限:"
                Layout.preferredWidth: Theme.cardLabelWidth
                horizontalAlignment: Text.AlignRight
                font: Theme.parameterFont
                color: Theme.textSecondary
            }
            ParameterTextField {
                Layout.fillWidth: true
                flat: false
                implicitHeight: Theme.paramEditorHeight
                text: model.maxValue
                readOnly: !root.editable || root.fixed
                onCommitRequested: if (text !== model.maxValue) root.commit({ max: text })
                onCancelRequested: text = model.maxValue
            }
        }

        // Field: 图像区域显示 (Image parameters only)
        RowLayout {
            Layout.fillWidth: true
            spacing: 8
            visible: root.isImage
            Label {
                text: "图像区域显示:"
                Layout.preferredWidth: Theme.cardLabelWidth + 24
                horizontalAlignment: Text.AlignRight
                font: Theme.parameterFont
                color: Theme.textSecondary
            }
            QuiToggleSwitch {
                enabled: root.editable && !root.fixed
                checked: model.showImage === true
                clickListener: function() {
                    if (root.editable)
                        root.commit({ showImage: !checked })
                }
            }
            Item { Layout.fillWidth: true }
        }

        // Advanced expandable settings (only when needed by specialized types)
        RowLayout {
            Layout.fillWidth: true
            visible: !root.fixed && root.hasAdvanced

            Text {
                text: root.advancedChecked ? "收起详情" : "更多设置"
                font: Theme.parameterFont
                color: advMouse.containsMouse ? Theme.highlight : Theme.textSecondary

                MouseArea {
                    id: advMouse
                    anchors.fill: parent
                    hoverEnabled: true
                    cursorShape: Qt.PointingHandCursor
                    onClicked: root.advancedChecked = !root.advancedChecked
                }
            }
            Item { Layout.fillWidth: true }
        }

        ColumnLayout {
            Layout.fillWidth: true
            spacing: 6
            visible: root.advancedChecked && !root.fixed && root.hasAdvanced

            RowLayout {
                Layout.fillWidth: true
                spacing: 8
                visible: root.typeToken === "string"
                Label { text: "编辑方式:"; Layout.preferredWidth: Theme.cardLabelWidth; horizontalAlignment: Text.AlignRight; font: Theme.parameterFont; color: Theme.textSecondary }
                QuiComboBox {
                    Layout.fillWidth: true
                    implicitHeight: Theme.paramEditorHeight
                    enabled: root.editable
                    model: root.editorOptions()
                    textRole: "label"
                    currentIndex: root.editorIndex()
                    background: Rectangle {
                        radius: Theme.radiusSmall
                        border.width: 1
                        border.color: Theme.border
                        color: Theme.surfaceBg
                    }
                    onActivated: function(index) {
                        root.commit({ editor: root.editorOptions()[index].token })
                    }
                }
            }

            RowLayout {
                Layout.fillWidth: true
                spacing: 8
                visible: root.showsRelation("image")
                Label { text: "关联图像:"; Layout.preferredWidth: Theme.cardLabelWidth; horizontalAlignment: Text.AlignRight; font: Theme.parameterFont; color: Theme.textSecondary }
                QuiComboBox {
                    Layout.fillWidth: true
                    implicitHeight: Theme.paramEditorHeight
                    enabled: root.editable
                    model: root.relationOptions("image")
                    textRole: "label"
                    currentIndex: root.relationIndex("image", model.imageRef)
                    background: Rectangle {
                        radius: Theme.radiusSmall
                        border.width: 1
                        border.color: Theme.border
                        color: Theme.surfaceBg
                    }
                    onActivated: function(index) {
                        root.commit({ image: root.relationOptions("image")[index].ref })
                    }
                }
            }

            RowLayout {
                Layout.fillWidth: true
                spacing: 8
                visible: root.showsRelation("alignment")
                Label { text: "对齐关系:"; Layout.preferredWidth: Theme.cardLabelWidth; horizontalAlignment: Text.AlignRight; font: Theme.parameterFont; color: Theme.textSecondary }
                QuiComboBox {
                    Layout.fillWidth: true
                    implicitHeight: Theme.paramEditorHeight
                    enabled: root.editable
                    model: root.relationOptions("alignment")
                    textRole: "label"
                    currentIndex: root.relationIndex("alignment", model.alignmentRef)
                    background: Rectangle {
                        radius: Theme.radiusSmall
                        border.width: 1
                        border.color: Theme.border
                        color: Theme.surfaceBg
                    }
                    onActivated: function(index) {
                        root.commit({ alignment: root.relationOptions("alignment")[index].ref })
                    }
                }
            }

            RowLayout {
                Layout.fillWidth: true
                spacing: 8
                visible: root.showsRelation("same_geometry_as")
                Label { text: "同几何:"; Layout.preferredWidth: Theme.cardLabelWidth; horizontalAlignment: Text.AlignRight; font: Theme.parameterFont; color: Theme.textSecondary }
                QuiComboBox {
                    Layout.fillWidth: true
                    implicitHeight: Theme.paramEditorHeight
                    enabled: root.editable
                    model: root.relationOptions("same_geometry_as")
                    textRole: "label"
                    currentIndex: root.relationIndex("same_geometry_as", model.sameGeometryRef)
                    background: Rectangle {
                        radius: Theme.radiusSmall
                        border.width: 1
                        border.color: Theme.border
                        color: Theme.surfaceBg
                    }
                    onActivated: function(index) {
                        root.commit({ same_geometry_as: root.relationOptions("same_geometry_as")[index].ref })
                    }
                }
            }

            RowLayout {
                Layout.fillWidth: true
                spacing: 8
                visible: root.showsRelation("path_mode")
                Label { text: "路径模式:"; Layout.preferredWidth: Theme.cardLabelWidth; horizontalAlignment: Text.AlignRight; font: Theme.parameterFont; color: Theme.textSecondary }
                QuiComboBox {
                    Layout.fillWidth: true
                    implicitHeight: Theme.paramEditorHeight
                    enabled: root.editable
                    model: root.relationOptions("path_mode")
                    textRole: "label"
                    currentIndex: root.relationIndex("path_mode", model.pathModeRef)
                    background: Rectangle {
                        radius: Theme.radiusSmall
                        border.width: 1
                        border.color: Theme.border
                        color: Theme.surfaceBg
                    }
                    onActivated: function(index) {
                        root.commit({ path_mode: root.relationOptions("path_mode")[index].ref })
                    }
                }
            }

            RowLayout {
                Layout.fillWidth: true
                spacing: 8
                visible: !root.output && (root.typeToken === "int" || root.typeToken === "float"
                                         || root.typeToken === "string" || root.typeToken === "bool")
                Label { text: "候选值:"; Layout.preferredWidth: Theme.cardLabelWidth; horizontalAlignment: Text.AlignRight; font: Theme.parameterFont; color: Theme.textSecondary }
                Rectangle {
                    Layout.fillWidth: true
                    implicitHeight: 50
                    color: Theme.surfaceBg
                    border.color: Theme.border
                    radius: Theme.radiusSmall

                    TextArea {
                        id: optionsField
                        anchors.fill: parent
                        anchors.margins: 4
                        enabled: root.editable
                        text: model.optionsText
                        wrapMode: TextArea.Wrap
                        font: Theme.parameterFont
                        color: Theme.textPrimary
                        onActiveFocusChanged: {
                            if (!activeFocus && text !== model.optionsText)
                                root.commit({ options: text })
                        }
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
            color: Theme.textPrimary
            text: typeImpactDialog.impact.join("\n")
        }
        onAccepted: if (root.portsModel) root.portsModel.changeType(root.index, pendingToken)
    }
}
