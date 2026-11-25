import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import quickui
import VisionRT.Ui 1.0

// Edits one port in the controller's current draft.
QuiCard {
    id: root

    required property int index
    required property var model
    required property real labelWidth
    readonly property alias implicitLabelWidth: cardLayout.implicitLabelWidth
    property var portsModel: null
    property var listView: null
    property var types: []
    property bool editable: false
    property int stage: PortStage.Inputs
    property int revision: 0

    readonly property bool fixed: root.model.isFixed === true
    readonly property bool output: stage === PortStage.Outputs
    readonly property var typeSpec: root.model.typeSpec
    readonly property int portType: typeSpec.type
    readonly property bool numeric: portType === PortType.Int || portType === PortType.Float
    readonly property bool isImage: portType === PortType.Image
    readonly property bool isRoi: root.types.some(function (item) { return item.token.type === root.portType && item.isShape; })
    readonly property bool isGeometry: isRoi || portType === PortType.Direction || portType === PortType.CoordinateFrame2D
    readonly property bool hasAdvanced: root.portType === PortType.String || (showsRelation(RelationKind.Image) && root.model.imageRef.id !== undefined) || (showsRelation(RelationKind.Alignment) && root.model.alignmentRef.id !== undefined) || (showsRelation(RelationKind.SameGeometry) && root.model.sameGeometryRef.id !== undefined) || (portType === PortType.String && root.model.editorType !== EditorKind.Default) || (root.model.optionsText && root.model.optionsText !== "")
    property bool advancedChecked: false
    readonly property real lowerBound: root.model.minValue !== "" && !isNaN(Number(root.model.minValue)) ? Number(root.model.minValue) : -2147483648
    readonly property real upperBound: root.model.maxValue !== "" && !isNaN(Number(root.model.maxValue)) ? Number(root.model.maxValue) : 2147483647

    implicitHeight: cardLayout.implicitHeight + 2 * cardLayout.anchors.margins
    cardColor: QuiColor.Primary
    borderColor: QuiColor.Border

    function sameType(a, b) {
        return a.type === b.type && a.dtype === b.dtype && a.rank === b.rank;
    }

    function typeIndex() {
        for (let i = 0; i < root.types.length; ++i)
            if (sameType(root.types[i].token, root.typeSpec))
                return i;
        return -1;
    }

    function commit(properties) {
        if (!root.portsModel)
            return false;
        return root.portsModel.updatePort(root.index, properties);
    }

    function relationOptions(kind) {
        root.revision;
        let options = [
            {
                ref: ({}),
                label: "（无）"
            }
        ];
        if (root.portsModel) {
            const candidates = root.portsModel.relationCandidates(root.index, kind);
            for (let i = 0; i < candidates.length; ++i)
                options.push(candidates[i]);
        }
        return options;
    }

    function relationIndex(kind, current) {
        // A model reset invalidates delegate roles before destroying the delegate.
        if (current === undefined)
            return -1;
        const options = relationOptions(kind);
        for (let i = 0; i < options.length; ++i)
            if (options[i].ref.stage === current.stage && options[i].ref.id === current.id)
                return i;
        return 0;
    }

    function editorOptions() {
        root.revision;
        if (!root.portsModel)
            return [
                {
                    token: EditorKind.Default,
                    label: "默认"
                }
            ];
        return root.portsModel.editorOptions(root.index);
    }

    function editorIndex() {
        const options = editorOptions();
        for (let i = 0; i < options.length; ++i)
            if (options[i].token === root.model.editorType)
                return i;
        return 0;
    }

    function showsRelation(kind) {
        if (kind === RelationKind.Image)
            return root.isGeometry;
        if (kind === RelationKind.Alignment)
            return root.isGeometry;
        if (kind === RelationKind.SameGeometry)
            return root.isImage && root.stage === PortStage.Outputs;
        return false;
    }

    Connections {
        target: root.portsModel
        function onDataChanged() {
            root.revision++;
        }
        function onModelReset() {
            root.revision++;
        }
        function onRowsInserted() {
            root.revision++;
        }
        function onRowsRemoved() {
            root.revision++;
        }
        function onRowsMoved() {
            root.revision++;
        }
    }

    ToolFormLayout {
        id: cardLayout
        anchors.fill: parent
        anchors.margins: 5

        // Header: Badge with port ID on the left, delete button on the right
        RowLayout {
            Layout.fillWidth: true
            spacing: 5

            Rectangle {
                id: numberChip
                Layout.preferredWidth: 26
                Layout.preferredHeight: 20
                radius: 3
                color: rowDrag.active ? QuiColor.Highlight : QuiColor.Primary
                border.color: rowDrag.active ? QuiColor.Highlight : "transparent"

                Label {
                    anchors.centerIn: parent
                    text: String(root.model.portId)
                    font: QuiFont.Caption
                    color: rowDrag.active ? QuiColor.White : QuiColor.Highlight
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
                            return -1;
                        const point = numberChip.mapToItem(root.listView.contentItem, numberChip.width / 2, numberChip.height / 2 + rowDrag.activeTranslation.y);
                        let row = root.listView.indexAt(point.x, point.y);
                        if (row < 0)
                            row = point.y < 0 ? 0 : root.listView.count - 1;
                        return Math.max(row, root.portsModel.fixedRowCount());
                    }

                    property int targetRow: -1

                    onActiveChanged: {
                        if (active) {
                            root.listView.interactive = false;
                            targetRow = root.index;
                            return;
                        }
                        root.listView.interactive = true;
                        const target = targetRow;
                        targetRow = -1;
                        if (target < 0 || target === root.index)
                            return;
                        root.portsModel.movePort(root.index, target);
                    }
                    onActiveTranslationChanged: if (active)
                        targetRow = dropTarget()
                }
            }

            Item {
                Layout.fillWidth: true
            }

            // Delete action button
            WorkbenchActionButton {
                iconSource: QuiFontIcon.ChromeClose
                text: "删除参数"
                enabled: root.editable && !root.fixed
                onClicked: root.portsModel.removePort(root.index)
            }
        }

        // Field 1: 中文名
        ToolFormRow {
            labelWidth: root.labelWidth
            label: "中文名:"
            ParameterTextField {
                Layout.fillWidth: true
                flat: false
                text: root.model.nameZh
                readOnly: !root.editable || root.fixed
                onCommitRequested: {
                    if (text !== root.model.nameZh)
                        root.commit({
                            nameZh: text
                        });
                }
                onCancelRequested: text = root.model.nameZh
            }
        }

        // Field 2: 英文名
        ToolFormRow {
            labelWidth: root.labelWidth
            label: "英文名:"
            ParameterTextField {
                Layout.fillWidth: true
                flat: false
                text: root.model.nameEn
                readOnly: !root.editable || root.fixed
                onCommitRequested: if (text !== root.model.nameEn)
                    root.commit({
                        nameEn: text
                    })
                onCancelRequested: text = root.model.nameEn
            }
        }

        // Field 3: 类型
        ToolFormRow {
            labelWidth: root.labelWidth
            label: "类型:"
            ParameterComboBox {
                id: typeCombo
                Layout.fillWidth: true
                enabled: root.editable && !root.fixed
                model: root.types
                textRole: "label"
                currentIndex: root.typeIndex()
                normalColor: QuiColor.Button
                disableColor: QuiColor.Button
                background: Rectangle {
                    radius: 3
                    border.width: 1
                    border.color: typeCombo.activeFocus ? QuiColor.Highlight : QuiColor.Border
                    color: QuiColor.Button
                }
                onActivated: function (index) {
                    const token = root.types[index].token;
                    if (sameType(token, root.typeSpec))
                        return;
                    const impact = root.portsModel ? root.portsModel.typeChangeImpact(root.index, token) : [];
                    if (impact.length > 0) {
                        typeImpactDialog.impact = impact;
                        typeImpactDialog.pendingType = token;
                        typeImpactDialog.open();
                        return;
                    }
                    root.portsModel.changeType(root.index, token);
                }
            }
        }

        // Field: 默认值。A business output can carry one as well: the declaration decides what a
        // missing value means, so the editor offers the same field in both directions. A path string
        // is edited in the same single picker the parameter panel uses, and a numeric default is
        // bounded by the same limits the contract stores.
        ToolFormRow {
            labelWidth: root.labelWidth
            label: "默认值:"
            visible: !root.isImage && root.portType !== PortType.Bool
            ParameterTextField {
                id: defaultField
                Layout.fillWidth: true
                flat: false
                text: root.model.defaultValue
                readOnly: !root.editable || root.fixed
                actionIcon: root.model.editorType === EditorKind.Path ? QuiFontIcon.FolderOpen : 0
                inputItem.validator: root.numeric ? (root.portType === PortType.Int ? intValidator : realValidator) : null
                onActionRequested: defaultPicker.open()
                onCommitRequested: if (text !== root.model.defaultValue)
                    root.commit({
                        defaultValue: text
                    })
                onCancelRequested: text = root.model.defaultValue
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
            onAccepted: function (path) {
                defaultField.text = path;
                root.commit({
                    defaultValue: path
                });
            }
        }

        // Field: 布尔类型默认值。A business output carries a default the same way an input does, and
        // the host-owned fixed ports stay read-only in both directions.
        ToolFormRow {
            labelWidth: root.labelWidth
            label: "默认值:"
            visible: root.portType === PortType.Bool
            QuiToggleSwitch {
                Layout.fillWidth: false
                Layout.alignment: Qt.AlignLeft | Qt.AlignVCenter
                enabled: root.editable && !root.fixed
                checked: String(root.model.defaultValue) === "true"
                clickListener: function () {
                    if (root.editable)
                        root.commit({
                            defaultValue: checked ? "false" : "true"
                        });
                }
            }
        }

        // Fields: 下限 & 上限 (Input parameters, numeric types, only for editable custom parameters)
        ToolFormRow {
            labelWidth: root.labelWidth
            label: "下限:"
            visible: root.numeric && (!root.fixed || root.model.minValue !== "" || root.model.maxValue !== "")
            ParameterTextField {
                Layout.fillWidth: true
                flat: false
                text: root.model.minValue
                readOnly: !root.editable || root.fixed
                onCommitRequested: if (text !== root.model.minValue)
                    root.commit({
                        min: text
                    })
                onCancelRequested: text = root.model.minValue
            }
        }

        ToolFormRow {
            labelWidth: root.labelWidth
            label: "上限:"
            visible: root.numeric && (!root.fixed || root.model.minValue !== "" || root.model.maxValue !== "")
            ParameterTextField {
                Layout.fillWidth: true
                flat: false
                text: root.model.maxValue
                readOnly: !root.editable || root.fixed
                onCommitRequested: if (text !== root.model.maxValue)
                    root.commit({
                        max: text
                    })
                onCancelRequested: text = root.model.maxValue
            }
        }

        // Field: 图像区域显示 (Image parameters only)
        ToolFormRow {
            labelWidth: root.labelWidth
            label: "图像区域显示:"
            visible: root.isImage
            QuiToggleSwitch {
                Layout.fillWidth: false
                Layout.alignment: Qt.AlignLeft | Qt.AlignVCenter
                enabled: root.editable && !root.fixed
                checked: root.model.showImage === true
                clickListener: function () {
                    if (root.editable)
                        root.commit({
                            showImage: !checked
                        });
                }
            }
        }

        // Advanced expandable settings (only when needed by specialized types)
        RowLayout {
            Layout.fillWidth: true
            visible: !root.fixed && root.hasAdvanced

            Text {
                text: root.advancedChecked ? "收起详情" : "更多设置"
                font: QuiFont.Body
                color: advMouse.containsMouse ? QuiColor.Highlight : QuiColor.FontCaption

                MouseArea {
                    id: advMouse
                    anchors.fill: parent
                    hoverEnabled: true
                    cursorShape: Qt.PointingHandCursor
                    onClicked: root.advancedChecked = !root.advancedChecked
                }
            }
            Item {
                Layout.fillWidth: true
            }
        }

        ToolFormLayout {
            Layout.fillWidth: true
            visible: root.advancedChecked && !root.fixed && root.hasAdvanced

            ToolFormRow {
                labelWidth: root.labelWidth
                label: "编辑方式:"
                visible: root.portType === PortType.String
                ParameterComboBox {
                    Layout.fillWidth: true
                    enabled: root.editable
                    model: root.editorOptions()
                    textRole: "label"
                    currentIndex: root.editorIndex()
                    background: Rectangle {
                        radius: 3
                        border.width: 1
                        border.color: QuiColor.Border
                        color: QuiColor.Button
                    }
                    onActivated: function (index) {
                        root.commit({
                            editor: root.editorOptions()[index].token
                        });
                    }
                }
            }

            ToolFormRow {
                labelWidth: root.labelWidth
                label: "关联图像:"
                visible: root.showsRelation(RelationKind.Image)
                ParameterComboBox {
                    Layout.fillWidth: true
                    enabled: root.editable
                    model: root.relationOptions(RelationKind.Image)
                    textRole: "label"
                    currentIndex: root.relationIndex(RelationKind.Image, root.model.imageRef)
                    background: Rectangle {
                        radius: 3
                        border.width: 1
                        border.color: QuiColor.Border
                        color: QuiColor.Button
                    }
                    onActivated: function (index) {
                        root.commit({
                            image: root.relationOptions(RelationKind.Image)[index].ref
                        });
                    }
                }
            }

            ToolFormRow {
                labelWidth: root.labelWidth
                label: "对齐关系:"
                visible: root.showsRelation(RelationKind.Alignment)
                ParameterComboBox {
                    Layout.fillWidth: true
                    enabled: root.editable
                    model: root.relationOptions(RelationKind.Alignment)
                    textRole: "label"
                    currentIndex: root.relationIndex(RelationKind.Alignment, root.model.alignmentRef)
                    background: Rectangle {
                        radius: 3
                        border.width: 1
                        border.color: QuiColor.Border
                        color: QuiColor.Button
                    }
                    onActivated: function (index) {
                        root.commit({
                            alignment: root.relationOptions(RelationKind.Alignment)[index].ref
                        });
                    }
                }
            }

            ToolFormRow {
                labelWidth: root.labelWidth
                label: "同几何:"
                visible: root.showsRelation(RelationKind.SameGeometry)
                ParameterComboBox {
                    Layout.fillWidth: true
                    enabled: root.editable
                    model: root.relationOptions(RelationKind.SameGeometry)
                    textRole: "label"
                    currentIndex: root.relationIndex(RelationKind.SameGeometry, root.model.sameGeometryRef)
                    background: Rectangle {
                        radius: 3
                        border.width: 1
                        border.color: QuiColor.Border
                        color: QuiColor.Button
                    }
                    onActivated: function (index) {
                        root.commit({
                            same_geometry_as: root.relationOptions(RelationKind.SameGeometry)[index].ref
                        });
                    }
                }
            }

            ToolFormRow {
                labelWidth: root.labelWidth
                label: "候选值:"
                visible: !root.output && (root.portType === PortType.Int || root.portType === PortType.Float || root.portType === PortType.String || root.portType === PortType.Bool)
                Rectangle {
                    Layout.fillWidth: true
                    implicitHeight: 50
                    color: QuiColor.Button
                    border.color: QuiColor.Border
                    radius: 3

                    TextArea {
                        id: optionsField
                        anchors.fill: parent
                        anchors.margins: 5
                        enabled: root.editable
                        text: root.model.optionsText
                        wrapMode: TextArea.Wrap
                        font: QuiFont.Body
                        color: QuiColor.FontPrimary
                        onActiveFocusChanged: {
                            if (!activeFocus && text !== root.model.optionsText)
                                root.commit({
                                    options: text
                                });
                        }
                    }
                }
            }
        }
    }

    WorkbenchDialog {
        id: typeImpactDialog
        property var impact: []
        property var pendingType: ({})
        title: "改变类型会清除以下内容"
        positiveText: "确定"
        negativeText: "取消"
        width: 420
        message: typeImpactDialog.impact.join("\n")
        onPositiveClicked: if (root.portsModel)
            root.portsModel.changeType(root.index, pendingType)
    }
}
