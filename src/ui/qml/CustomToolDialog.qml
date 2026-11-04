import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import QtQuick.Window
import quickui
import VisionRT.Style 1.0

Window {
    id: root

    title: "自定义工具"
    flags: Qt.Dialog | Qt.WindowTitleHint | Qt.WindowSystemMenuHint | Qt.WindowMinMaxButtonsHint | Qt.WindowCloseButtonHint
    modality: Qt.NonModal
    color: QuiColor.WindowBackground
    width: 1120
    height: 680
    minimumWidth: 860
    minimumHeight: 520

    property var controller: app ? app.toolEditor : null
    property int selectedPortRow: -1
    // Rows the pointer is carrying and hovering over; -1 when no drag is in flight.
    property int dragSourceRow: -1
    property int dragTargetRow: -1

    function openDialog() {
        if (root.visibility === Window.Minimized)
            root.showNormal()
        root.show()
        root.raise()
        root.requestActivate()
    }

    // Top Layout
    ColumnLayout {
        anchors.fill: parent
        spacing: 0

        // Main 3-column layout
        Item {
            Layout.fillWidth: true
            Layout.fillHeight: true

            RowLayout {
                anchors.fill: parent
                spacing: 0

                // ==========================================
                // 1. LEFT PANE: Tools Catalog
                // ==========================================
                Rectangle {
                    Layout.preferredWidth: 240
                    Layout.fillHeight: true
                    color: QuiColor.CardBackground
                    border.color: QuiColor.Border
                    border.width: 1

                    ColumnLayout {
                        anchors.fill: parent
                        anchors.margins: 10
                        spacing: 8

                        // Catalog Header
                        RowLayout {
                            Layout.fillWidth: true
                            Label {
                                text: "工具库"
                                font.bold: true
                                font.pixelSize: 14
                                color: QuiColor.FontPrimary
                            }
                            Item { Layout.fillWidth: true }
                            Button {
                                text: "+ 新建"
                                onClicked: newToolDialog.open()
                            }
                        }

                        // Search
                        TextField {
                            id: searchField
                            Layout.fillWidth: true
                            placeholderText: "搜索工具..."
                        }

                        // Tools List
                        ListView {
                            id: toolListView
                            Layout.fillWidth: true
                            Layout.fillHeight: true
                            clip: true
                            model: {
                                if (!root.controller) return []
                                let all = root.controller.tools
                                let filter = searchField.text.trim().toLowerCase()
                                if (!filter) return all
                                return all.filter(function(t) {
                                    return (t.name && t.name.toLowerCase().includes(filter)) ||
                                           (t.id && t.id.toLowerCase().includes(filter))
                                })
                            }

                            delegate: Rectangle {
                                width: toolListView.width
                                height: 42
                                color: (root.controller && root.controller.currentToolId === modelData.id)
                                       ? QuiColor.ItemCheck
                                       : (mouseArea.containsMouse ? QuiColor.ItemHover : "transparent")
                                radius: 4

                                MouseArea {
                                    id: mouseArea
                                    anchors.fill: parent
                                    hoverEnabled: true
                                    onClicked: {
                                        if (root.controller) {
                                            root.controller.selectTool(modelData.id)
                                            root.selectedPortRow = -1
                                        }
                                    }
                                }

                                RowLayout {
                                    anchors.fill: parent
                                    anchors.margins: 6
                                    spacing: 8

                                    // Kind badge
                                    Rectangle {
                                        Layout.preferredWidth: 24
                                        Layout.preferredHeight: 24
                                        radius: 3
                                        color: modelData.kind.includes("class") ? QuiColor.CardBackground : QuiColor.WindowBackground
                                        border.color: modelData.kind.includes("class") ? QuiColor.Highlight : QuiColor.FontDark
                                        Label {
                                            anchors.centerIn: parent
                                            text: modelData.kind.includes("class") ? "类" : "函"
                                            font.pixelSize: 11
                                            color: modelData.kind.includes("class") ? QuiColor.Highlight : QuiColor.FontDark
                                        }
                                    }

                                    ColumnLayout {
                                        Layout.fillWidth: true
                                        spacing: 2
                                        Label {
                                            text: modelData.name || modelData.id
                                            font.pixelSize: 13
                                            elide: Text.ElideRight
                                            color: QuiColor.FontPrimary
                                        }
                                        Label {
                                            text: modelData.id
                                            font.pixelSize: 10
                                            color: QuiColor.FontCaption
                                            elide: Text.ElideRight
                                        }
                                    }
                                }
                            }
                        }

                        // Tool actions
                        RowLayout {
                            Layout.fillWidth: true
                            spacing: 6
                            Button {
                                Layout.fillWidth: true
                                text: "复制"
                                // A Python tool carries a script that can live in the user root; a
                                // native factory cannot be copied, only recompiled.
                                enabled: root.controller && root.controller.canClone && root.controller.currentToolId !== ""
                                onClicked: {
                                    if (root.controller)
                                        root.controller.cloneTool(root.controller.currentToolId)
                                }
                            }
                            Button {
                                Layout.fillWidth: true
                                text: "删除"
                                enabled: root.controller && root.controller.isEditable && root.controller.currentToolId !== ""
                                onClicked: confirmDeleteDialog.open()
                            }
                        }
                    }
                }

                // ==========================================
                // 2. MIDDLE PANE: Tool Info & Port Table
                // ==========================================
                Rectangle {
                    Layout.fillWidth: true
                    Layout.fillHeight: true
                    color: QuiColor.CardBackground

                    ColumnLayout {
                        anchors.fill: parent
                        anchors.margins: 14
                        spacing: 12

                        // Tool Metadata Header
                        RowLayout {
                            Layout.fillWidth: true
                            spacing: 12

                            ColumnLayout {
                                Layout.fillWidth: true
                                spacing: 4
                                Label { text: "工具名称"; font.pixelSize: 11; color: QuiColor.FontCaption }
                                TextField {
                                    Layout.fillWidth: true
                                    text: root.controller ? root.controller.currentToolName : ""
                                    enabled: root.controller && root.controller.isEditable
                                    onEditingFinished: {
                                        if (root.controller)
                                            root.controller.currentToolName = text
                                    }
                                }
                            }

                            ColumnLayout {
                                Layout.fillWidth: true
                                spacing: 4
                                Label {
                                    text: root.controller && root.controller.isToolIdEditable
                                          ? "工具标识 (ID，首次应用前可修改)"
                                          : "工具标识 (ID)"
                                    font.pixelSize: 11
                                    color: QuiColor.FontCaption
                                }
                                TextField {
                                    Layout.fillWidth: true
                                    text: root.controller ? root.controller.currentToolId : ""
                                    readOnly: !(root.controller && root.controller.isToolIdEditable)
                                    onEditingFinished: {
                                        if (!root.controller || !root.controller.isToolIdEditable)
                                            return
                                        root.controller.currentToolId = text
                                        text = root.controller.currentToolId
                                    }
                                }
                            }

                            ColumnLayout {
                                Layout.preferredWidth: 140
                                spacing: 4
                                Label { text: "类别"; font.pixelSize: 11; color: QuiColor.FontCaption }
                                TextField {
                                    Layout.fillWidth: true
                                    text: root.controller ? root.controller.currentToolCategory : "自定义"
                                    enabled: root.controller && root.controller.isEditable
                                    onEditingFinished: {
                                        if (root.controller)
                                            root.controller.currentToolCategory = text
                                    }
                                }
                            }
                        }

                        // Impact preview: what the pending declaration does to the open project.
                        Label {
                            Layout.fillWidth: true
                            visible: root.controller && root.controller.isDirty && root.controller.currentToolId !== ""
                            text: root.controller ? root.controller.impactSummary : ""
                            wrapMode: Text.WordWrap
                            font.pixelSize: 11
                            color: QuiColor.FontCaption
                        }

                        // Installed tools open as definitions to read, never as drafts to publish.
                        Label {
                            Layout.fillWidth: true
                            visible: root.controller && root.controller.currentToolId !== "" && !root.controller.isEditable
                            text: root.controller && root.controller.canClone
                                  ? "内置工具：定义只读，可查看参数与声明 JSON；需要修改时先复制为自有工具，再在副本上编辑并应用。"
                                  : "内置工具：定义只读，可查看参数与声明 JSON。该工具由原生代码注册，修改端口需改源码后重新打包。"
                            wrapMode: Text.WordWrap
                            font.pixelSize: 11
                            color: QuiColor.FontCaption
                        }

                        // Stage Tabs
                        TabBar {
                            id: stageTabs
                            Layout.fillWidth: true
                            currentIndex: 0
                            onCurrentIndexChanged: {
                                if (!root.controller) return
                                if (currentIndex === 0) root.controller.currentStage = "inputs"
                                else if (currentIndex === 1) root.controller.currentStage = "outputs"
                                else if (currentIndex === 2) root.controller.currentStage = "init"
                                root.selectedPortRow = -1
                            }

                            TabButton { text: "输入参数" }
                            TabButton { text: "输出参数" }
                            TabButton { text: "初始化参数" }
                            TabButton { text: "代码文件" }
                        }

                        // Port table view (Tabs 0, 1, 2)
                        ColumnLayout {
                            Layout.fillWidth: true
                            Layout.fillHeight: true
                            visible: stageTabs.currentIndex !== 3
                            spacing: 6

                            // Port Table Toolbar
                            RowLayout {
                                Layout.fillWidth: true
                                spacing: 6

                                Button {
                                    text: "+ 添加参数"
                                    enabled: root.controller && root.controller.isEditable
                                    onClicked: {
                                        if (root.controller && root.controller.portModel) {
                                            root.controller.portModel.addPort("新参数", "int", "0")
                                            root.selectedPortRow = portListView.count - 1
                                        }
                                    }
                                }
                                Button {
                                    text: "↑ 上移"
                                    enabled: root.controller && root.controller.isEditable &&
                                             root.selectedPortRow > (root.controller.portModel ? root.controller.portModel.fixedRowCount() : 0)
                                    onClicked: {
                                        if (root.controller && root.controller.portModel) {
                                            let target = root.selectedPortRow - 1
                                            if (root.controller.portModel.movePort(root.selectedPortRow, target))
                                                root.selectedPortRow = target
                                        }
                                    }
                                }
                                Button {
                                    text: "↓ 下移"
                                    enabled: root.controller && root.controller.isEditable && root.controller.portModel &&
                                             root.selectedPortRow >= root.controller.portModel.fixedRowCount() &&
                                             root.selectedPortRow < portListView.count - 1
                                    onClicked: {
                                        if (root.controller && root.controller.portModel) {
                                            let target = root.selectedPortRow + 1
                                            if (root.controller.portModel.movePort(root.selectedPortRow, target))
                                                root.selectedPortRow = target
                                        }
                                    }
                                }
                                Button {
                                    text: "× 删除"
                                    enabled: root.controller && root.controller.isEditable && root.controller.portModel &&
                                             root.selectedPortRow >= root.controller.portModel.fixedRowCount()
                                    onClicked: {
                                        if (root.controller && root.controller.portModel) {
                                            root.controller.portModel.removePort(root.selectedPortRow)
                                            root.selectedPortRow = -1
                                        }
                                    }
                                }
                            }

                            // Table Header
                            Rectangle {
                                Layout.fillWidth: true
                                Layout.preferredHeight: 32
                                color: QuiColor.Background
                                border.color: QuiColor.Border
                                radius: 3

                                RowLayout {
                                    anchors.fill: parent
                                    anchors.leftMargin: 8
                                    anchors.rightMargin: 8
                                    // Keeps the header aligned with the rows, which carry a drag handle column.
                                    Item { Layout.preferredWidth: 18 }
                                    Label { text: "编号 (ID)"; Layout.preferredWidth: 80; font.bold: true; font.pixelSize: 12 }
                                    Label { text: "参数名称"; Layout.preferredWidth: 140; font.bold: true; font.pixelSize: 12 }
                                    Label { text: "类型"; Layout.preferredWidth: 120; font.bold: true; font.pixelSize: 12 }
                                    Label { text: "默认值"; Layout.fillWidth: true; font.bold: true; font.pixelSize: 12 }
                                    Label { text: "状态"; Layout.preferredWidth: 70; font.bold: true; font.pixelSize: 12 }
                                }
                            }

                            // Table List
                            ListView {
                                id: portListView
                                Layout.fillWidth: true
                                Layout.fillHeight: true
                                clip: true
                                model: root.controller ? root.controller.portModel : null

                                delegate: Rectangle {
                                    width: portListView.width
                                    height: 36
                                    color: (root.selectedPortRow === index)
                                           ? QuiColor.ItemCheck
                                           : (rowMouse.containsMouse ? QuiColor.ItemHover : "transparent")
                                    border.color: (root.dragSourceRow === index || (root.dragSourceRow >= 0 && root.dragTargetRow === index))
                                                  ? QuiColor.Highlight
                                                  : (root.selectedPortRow === index ? QuiColor.Border : "transparent")
                                    radius: 2

                                    MouseArea {
                                        id: rowMouse
                                        anchors.fill: parent
                                        hoverEnabled: true
                                        onClicked: root.selectedPortRow = index
                                    }

                                    RowLayout {
                                        anchors.fill: parent
                                        anchors.leftMargin: 8
                                        anchors.rightMargin: 8

                                        // Drag handle. The gutter is always present so every column lines
                                        // up with the header; only a business row can be carried, and the
                                        // drop is clamped to the business area.
                                        Item {
                                            id: dragHandle
                                            Layout.preferredWidth: 18
                                            Layout.fillHeight: true

                                            Label {
                                                anchors.centerIn: parent
                                                text: model.isFixed ? "" : "⋮⋮"
                                                font.pixelSize: 12
                                                color: rowDrag.active ? QuiColor.Highlight : QuiColor.FontCaption
                                            }
                                            HoverHandler {
                                                enabled: rowDrag.enabled
                                                cursorShape: rowDrag.active ? Qt.ClosedHandCursor : Qt.OpenHandCursor
                                            }
                                            DragHandler {
                                                id: rowDrag
                                                target: null
                                                xAxis.enabled: false
                                                enabled: root.controller && root.controller.isEditable && !model.isFixed

                                                // The drop row is resolved while the pointer moves, not on
                                                // release: the handler's translation is cleared as soon as
                                                // the grab ends.
                                                function updateDropTarget() {
                                                    if (!rowDrag.active || root.dragSourceRow < 0)
                                                        return
                                                    const portsModel = root.controller ? root.controller.portModel : null
                                                    if (!portsModel)
                                                        return
                                                    // Content coordinates: indexAt() speaks the same system,
                                                    // and contentY is frozen because flicking is off.
                                                    const point = dragHandle.mapToItem(portListView.contentItem,
                                                                                      dragHandle.width / 2,
                                                                                      dragHandle.height / 2 + rowDrag.activeTranslation.y)
                                                    let targetRow = portListView.indexAt(point.x, point.y)
                                                    const firstBusiness = portsModel.fixedRowCount()
                                                    if (targetRow < 0)
                                                        targetRow = point.y < 0 ? firstBusiness : portListView.count - 1
                                                    root.dragTargetRow = Math.max(targetRow, firstBusiness)
                                                }

                                                onActiveChanged: {
                                                    if (active) {
                                                        root.dragSourceRow = index
                                                        root.dragTargetRow = index
                                                        portListView.interactive = false
                                                        updateDropTarget()
                                                        return
                                                    }
                                                    portListView.interactive = true
                                                    const source = root.dragSourceRow
                                                    const target = root.dragTargetRow
                                                    root.dragSourceRow = -1
                                                    root.dragTargetRow = -1
                                                    if (source < 0 || target < 0 || target === source)
                                                        return
                                                    const portsModel = root.controller ? root.controller.portModel : null
                                                    if (portsModel && portsModel.movePort(source, target)
                                                            && root.selectedPortRow === source)
                                                        root.selectedPortRow = target
                                                }
                                                onActiveTranslationChanged: updateDropTarget()
                                            }
                                        }

                                        Label {
                                            text: String(model.portId)
                                            Layout.preferredWidth: 80
                                            font.bold: true
                                            color: model.isFixed ? QuiColor.FontCaption : QuiColor.FontPrimary
                                        }
                                        Label {
                                            text: model.nameZh
                                            Layout.preferredWidth: 140
                                            elide: Text.ElideRight
                                            color: QuiColor.FontPrimary
                                        }
                                        Label {
                                            text: model.displayType
                                            Layout.preferredWidth: 120
                                            elide: Text.ElideRight
                                            color: QuiColor.FontCaption
                                        }
                                        Label {
                                            text: model.defaultValue || "-"
                                            Layout.fillWidth: true
                                            elide: Text.ElideRight
                                            color: QuiColor.FontCaption
                                        }
                                        Label {
                                            text: model.isFixed ? "🔒 系统" : "业务"
                                            Layout.preferredWidth: 70
                                            font.pixelSize: 11
                                            color: model.isFixed ? Theme.warning : QuiColor.FontCaption
                                        }
                                    }
                                }
                            }
                        }

                        // Code tab view (Tab 3)
                        ColumnLayout {
                            Layout.fillWidth: true
                            Layout.fillHeight: true
                            visible: stageTabs.currentIndex === 3
                            spacing: 12

                            Label {
                                text: "Python 脚本文件："
                                font.bold: true
                                color: QuiColor.FontPrimary
                            }
                            TextField {
                                Layout.fillWidth: true
                                text: root.controller ? root.controller.currentToolScript : ""
                                readOnly: true
                            }
                            RowLayout {
                                spacing: 10
                                Button {
                                    text: "外部编辑器打开脚本"
                                    onClicked: if (root.controller) root.controller.openCodeFile()
                                }
                                Button {
                                    text: "在文件管理器中显示"
                                    onClicked: if (root.controller) root.controller.openCodeDirectory()
                                }
                            }
                            Item { Layout.fillHeight: true }
                        }
                    }
                }

                // ==========================================
                // 3. RIGHT PANE: Port Properties Editor
                // ==========================================
                ToolPortProperties {
                    Layout.preferredWidth: 280
                    Layout.fillHeight: true
                    controller: root.controller
                    selectedRow: root.selectedPortRow
                }
            }
        }

        // ==========================================
        // 4. FOOTER: Status Bar & Action Buttons
        // ==========================================
        Rectangle {
            Layout.fillWidth: true
            Layout.preferredHeight: 48
            color: QuiColor.CardBackground
            border.color: QuiColor.Border
            border.width: 1

            RowLayout {
                anchors.fill: parent
                anchors.margins: 10
                spacing: 10

                Label {
                    text: root.controller ? root.controller.statusMessage : ""
                    color: (root.controller && root.controller.isStatusError) ? QuiColor.Error : QuiColor.FontCaption
                    Layout.fillWidth: true
                    elide: Text.ElideRight
                }

                Button {
                    text: "查看声明 JSON"
                    enabled: root.controller && root.controller.currentToolId !== ""
                    onClicked: jsonPreviewDialog.open()
                }

                Button {
                    text: "撤销修改"
                    enabled: root.controller && root.controller.isDirty
                    onClicked: {
                        if (root.controller)
                            root.controller.revert()
                    }
                }

                Button {
                    text: "应用"
                    enabled: root.controller && root.controller.canApply && root.controller.isDirty
                    onClicked: {
                        if (root.controller)
                            root.controller.apply()
                    }
                }

                Button {
                    text: "关闭"
                    onClicked: root.close()
                }
            }
        }
    }

    // ==========================================
    // DIALOGS: New Tool, Confirm Delete, JSON Preview
    // ==========================================
    Dialog {
        id: newToolDialog
        title: "新建自定义工具"
        modal: true
        standardButtons: Dialog.Ok | Dialog.Cancel
        width: 420

        ColumnLayout {
            anchors.fill: parent
            spacing: 8

            Label { text: "工具名称 (中文)："; font.pixelSize: 12 }
            TextField { id: newNameField; Layout.fillWidth: true; placeholderText: "例如：图像二值化" }

            Label { text: "工具标识 (英文 name_en)："; font.pixelSize: 12 }
            TextField { id: newIdField; Layout.fillWidth: true; placeholderText: "例如：custom.image.threshold" }

            Label { text: "实现类型："; font.pixelSize: 12 }
            ComboBox {
                id: newKindCombo
                Layout.fillWidth: true
                model: [
                    { label: "Python 函数 (无状态)", value: "python_function" },
                    { label: "Python 类 (可持有状态)", value: "python_class" }
                ]
                textRole: "label"
                valueRole: "value"
            }

            Label { text: "工具类别："; font.pixelSize: 12 }
            TextField { id: newCategoryField; Layout.fillWidth: true; text: "自定义" }
        }

        onAccepted: {
            if (root.controller) {
                root.controller.newTool(newNameField.text, newIdField.text, newKindCombo.currentValue, newCategoryField.text)
                newNameField.text = ""
                newIdField.text = ""
            }
        }
    }

    Dialog {
        id: confirmDeleteDialog
        title: "确认删除工具"
        modal: true
        standardButtons: Dialog.Yes | Dialog.No
        width: 420
        Label {
            width: 400
            wrapMode: Text.WordWrap
            text: {
                if (!root.controller) return ""
                const count = root.controller.toolUsage(root.controller.currentToolId)
                if (count > 0)
                    return "当前工程有 " + count + " 个节点引用该工具。删除后这些节点保留为“工具缺失”，需要手动修复。确定删除？"
                return "当前工程没有节点引用该工具。确定删除工具声明及其专属脚本目录？"
            }
        }
        onAccepted: {
            if (root.controller)
                root.controller.deleteTool(root.controller.currentToolId)
        }
    }

    Dialog {
        id: jsonPreviewDialog
        title: "工具声明 JSON (Schema 6)"
        modal: true
        standardButtons: Dialog.Close
        width: 600
        height: 480

        ScrollView {
            anchors.fill: parent
            clip: true
            TextArea {
                text: root.controller ? root.controller.previewJson() : ""
                readOnly: true
                font.family: "Consolas, Courier, monospace"
                font.pixelSize: 12
                wrapMode: Text.NoWrap
            }
        }
    }
}
