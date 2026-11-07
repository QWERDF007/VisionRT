import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import QtQuick.Window
import quickui
import VisionRT.Style 1.0

// Custom tool dialog styled strictly with VisionRT Theme and QuickUI design system.
// Reuses ToolTreeDelegate for unified category icons, styling, and behavior.
Window {
    id: root

    title: "自定义工具"
    flags: Qt.Dialog | Qt.FramelessWindowHint
    modality: Qt.NonModal
    color: "transparent"
    width: 1312
    height: 792
    minimumWidth: 1056
    minimumHeight: 632

    property var controller: null
    property var flatTree: []
    property var collapsed: ({})
    property var menuItem: null
    property string searchKeyword: ""

    function openDialog() {
        if (root.visibility === Window.Minimized)
            root.showNormal()
        root.show()
        root.raise()
        root.requestActivate()
        root.rebuildTree()
        root.autoSelectFirstTool()
    }

    function autoSelectFirstTool() {
        if (!root.controller || root.controller.hasTool)
            return
        for (let i = 0; i < root.flatTree.length; ++i) {
            if (root.flatTree[i].kind === "tool") {
                root.selectKey(root.flatTree[i].key)
                return
            }
        }
    }

    function rebuildTree() {
        let rows = []
        const nodes = root.controller ? root.controller.tree : []
        const filter = root.searchKeyword.trim().toLowerCase()

        function matchNode(node) {
            if (!filter) return true
            if (node.kind === "tool") {
                const name = (node.name || "").toLowerCase()
                const id = (node.toolId || "").toLowerCase()
                return name.indexOf(filter) >= 0 || id.indexOf(filter) >= 0
            }
            if (node.children) {
                for (let i = 0; i < node.children.length; ++i) {
                    if (matchNode(node.children[i])) return true
                }
            }
            return false
        }

        function walk(list, depth) {
            for (let i = 0; i < list.length; ++i) {
                const node = list[i]
                if (filter && !matchNode(node))
                    continue

                const isFolder = node.kind === "folder"
                const hasChildren = (node.children || []).length > 0
                const isExpanded = filter ? true : (isFolder && !root.collapsed[node.path])
                rows.push({
                    kind: node.kind,
                    display: node.name,
                    name: node.name,
                    path: node.path || "",
                    key: node.key || "",
                    stableKey: node.key || "",
                    toolId: node.toolId || "",
                    folder: node.folder || "",
                    toolKind: node.toolKind || "",
                    draft: node.draft === true,
                    depth: depth,
                    isFolder: isFolder,
                    hasChildren: hasChildren,
                    leaf: !isFolder,
                    available: true,
                    status: "",
                    expanded: isExpanded
                })
                if (isFolder && isExpanded && hasChildren)
                    walk(node.children, depth + 1)
            }
        }
        walk(nodes, 0)
        root.flatTree = rows
        if (root.controller && !root.controller.hasTool)
            root.autoSelectFirstTool()
    }

    function toggleFolder(path) {
        if (root.collapsed[path])
            delete root.collapsed[path]
        else
            root.collapsed[path] = true
        root.rebuildTree()
    }

    function selectKey(key) {
        if (root.controller)
            root.controller.selectKey(key)
    }

    function openPrompt(mode, parentPath, path) {
        promptDialog.mode = mode
        promptDialog.parentPath = parentPath || ""
        promptDialog.path = path || ""
        promptField.text = mode === "rename" ? root.controller.folderName(path) : ""
        promptDialog.title = mode === "rename" ? "修改二级目录"
                            : (mode === "addChild" ? "添加二级目录" : "添加一级目录")
        promptDialog.open()
    }

    Component.onCompleted: {
        rebuildTree()
        autoSelectFirstTool()
    }
    onVisibleChanged: {
        if (visible) {
            rebuildTree()
            autoSelectFirstTool()
        }
    }

    Connections {
        target: root.controller
        function onTreeChanged() { root.rebuildTree() }
    }

    // Main window background card with 16px shadow margin, smooth 4-corner radius, and continuous border
    Rectangle {
        id: windowCard
        anchors.fill: parent
        anchors.margins: 16
        radius: 10
        color: Theme.surfaceBg
        border.color: Theme.border
        border.width: 1

        QuiShadow {
            radius: 10
            elevation: 16
        }

        ColumnLayout {
            anchors.fill: parent
            spacing: 0

            // ------------------------------------------------------------- Custom Title Bar
            Item {
                Layout.fillWidth: true
                Layout.preferredHeight: 46

                // Drag to move window
                DragHandler {
                    target: null
                    onActiveTranslationChanged: {
                        root.x += activeTranslation.x
                        root.y += activeTranslation.y
                    }
                }

                RowLayout {
                    anchors.fill: parent
                    anchors.leftMargin: 16
                    anchors.rightMargin: 16
                    spacing: 8

                    Label {
                        text: "自定义工具"
                        font.bold: true
                        font.pixelSize: 16
                        color: Theme.textPrimary
                    }

                    Item { Layout.fillWidth: true }

                    // Close button
                    Rectangle {
                        id: closeBtn
                        Layout.preferredWidth: 28
                        Layout.preferredHeight: 28
                        radius: Theme.radiusSmall
                        color: closeMouse.containsMouse ? Theme.menuButtonHoverBg : "transparent"

                        Text {
                            anchors.centerIn: parent
                            text: "✕"
                            font.pixelSize: 13
                            color: closeMouse.containsMouse ? Theme.error : Theme.iconMuted
                        }

                        MouseArea {
                            id: closeMouse
                            anchors.fill: parent
                            hoverEnabled: true
                            cursorShape: Qt.PointingHandCursor
                            onClicked: {
                                if (root.controller && root.controller.isDirty)
                                    discardDialog.open()
                                else
                                    root.close()
                            }
                        }
                    }
                }
            }

            // Title bar divider line matching main window menu/toolbar border
            Rectangle {
                Layout.fillWidth: true
                height: 1
                color: Theme.border
            }

            // ------------------------------------------------------------- 4-Column Layout Body
            Item {
                Layout.fillWidth: true
                Layout.fillHeight: true

                RowLayout {
                    anchors.fill: parent
                    anchors.leftMargin: 16
                    anchors.rightMargin: 16
                    anchors.topMargin: 12
                    anchors.bottomMargin: 12
                    spacing: 16

                    // ========================== Column 1: Tool Catalogue ==========================
                    Item {
                        Layout.preferredWidth: 230
                        Layout.fillHeight: true

                        ColumnLayout {
                            anchors.fill: parent
                            spacing: 12

                            // Search box matching Theme editor input tokens
                            Rectangle {
                                Layout.fillWidth: true
                                Layout.preferredHeight: 32
                                radius: Theme.radiusSmall
                                color: Theme.paramEditorBg
                                border.color: searchInput.activeFocus ? Theme.paramEditorFocusBorder : Theme.border
                                border.width: 1

                                RowLayout {
                                    anchors.fill: parent
                                    anchors.leftMargin: 8
                                    anchors.rightMargin: 8
                                    spacing: 6

                                    QuiTextIcon {
                                        iconSource: QuiFontIcon.Search
                                        iconSize: 13
                                        iconColor: Theme.iconMuted
                                    }

                                    TextInput {
                                        id: searchInput
                                        Layout.fillWidth: true
                                        font.pixelSize: 13
                                        color: Theme.textPrimary
                                        selectByMouse: true
                                        verticalAlignment: TextInput.AlignVCenter
                                        onTextChanged: {
                                            root.searchKeyword = text
                                            root.rebuildTree()
                                        }

                                        Text {
                                            anchors.fill: parent
                                            verticalAlignment: Text.AlignVCenter
                                            text: "搜索工具"
                                            font.pixelSize: 13
                                            color: Theme.disabledText
                                            visible: !searchInput.text && !searchInput.activeFocus
                                        }
                                    }

                                    QuiTextIconButton {
                                        visible: searchInput.text !== ""
                                        Layout.preferredWidth: 16
                                        Layout.preferredHeight: 16
                                        iconSource: QuiFontIcon.Cancel
                                        iconSize: 9
                                        normalColor: "transparent"
                                        hoverColor: Theme.menuButtonHoverBg
                                        iconColor: Theme.iconMuted
                                        onClicked: searchInput.text = ""
                                    }
                                }
                            }

                            // Reusing ToolTreeDelegate: identical category icons, diamond leaf icons, and Theme styling
                            ListView {
                                id: treeView
                                Layout.fillWidth: true
                                Layout.fillHeight: true
                                clip: true
                                spacing: 2
                                cacheBuffer: 5000
                                model: root.flatTree

                                delegate: ToolTreeDelegate {
                                    width: ListView.view ? ListView.view.width : (treeView ? treeView.width : 200)
                                    isEditor: true
                                    model: modelData
                                    row: index
                                    depth: modelData.depth
                                    hasChildren: modelData.hasChildren
                                    expanded: modelData.expanded
                                    selectedKey: root.controller ? root.controller.currentKey : ""
                                    onSelected: function(key) {
                                        root.selectKey(key)
                                    }
                                    onToggleExpandedRequested: {
                                        if (modelData.isFolder)
                                            root.toggleFolder(modelData.path)
                                    }
                                    onContextMenuRequested: function(mouse, itemModel) {
                                        root.menuItem = itemModel
                                        contextMenu.popup()
                                    }
                                }

                                TapHandler {
                                    acceptedButtons: Qt.RightButton
                                    onTapped: {
                                        root.menuItem = null
                                        contextMenu.popup()
                                    }
                                }
                            }
                        }
                    }

                    // Vertical separator line strictly using Theme.border (consistent with main window splitters)
                    Rectangle {
                        Layout.fillHeight: true
                        width: 1
                        color: Theme.border
                    }

                    // Empty placeholder when no tool is selected
                    Item {
                        Layout.fillWidth: true
                        Layout.fillHeight: true
                        visible: !(root.controller && root.controller.hasTool)

                        ColumnLayout {
                            anchors.centerIn: parent
                            spacing: 12
                            QuiTextIcon {
                                Layout.alignment: Qt.AlignHCenter
                                iconSource: QuiFontIcon.AllApps
                                iconSize: 44
                                iconColor: Theme.iconMuted
                            }
                            Label {
                                Layout.alignment: Qt.AlignHCenter
                                text: "请在左侧选择工具进行编辑，或右键添加新工具"
                                font.pixelSize: 14
                                color: Theme.textSecondary
                            }
                        }
                    }

                    // ========================== Column 2: Tool Properties ==========================
                    Item {
                        Layout.preferredWidth: 260
                        Layout.fillHeight: true
                        visible: root.controller && root.controller.hasTool

                        ColumnLayout {
                            anchors.fill: parent
                            spacing: 12

                            // Title bar matching design exactly in height & baseline
                            RowLayout {
                                Layout.fillWidth: true
                                Layout.preferredHeight: 32
                                Label {
                                    text: "工具"
                                    font.bold: true
                                    font.pixelSize: 15
                                    color: Theme.textPrimary
                                    verticalAlignment: Text.AlignVCenter
                                }
                                Item { Layout.fillWidth: true }
                            }

                            // Card with primary highlight border matching Theme.highlight
                            Rectangle {
                                Layout.fillWidth: true
                                Layout.fillHeight: true
                                radius: 8
                                color: Theme.surfaceBg
                                border.color: Theme.highlight
                                border.width: 1.5

                                ColumnLayout {
                                    anchors.fill: parent
                                    anchors.margins: 12
                                    spacing: 12

                                    // 中文名
                                    RowLayout {
                                        Layout.fillWidth: true
                                        spacing: 8
                                        Label {
                                            text: "中文名:"
                                            Layout.preferredWidth: 64
                                            horizontalAlignment: Text.AlignRight
                                            font.pixelSize: 13
                                            color: Theme.textSecondary
                                        }
                                        ParameterTextField {
                                            Layout.fillWidth: true
                                            flat: false
                                            implicitHeight: 32
                                            text: root.controller ? root.controller.currentToolName : ""
                                            readOnly: !(root.controller && root.controller.isEditable)
                                            onCommitRequested: if (root.controller) root.controller.currentToolName = text
                                            onCancelRequested: text = root.controller ? root.controller.currentToolName : ""
                                        }
                                    }

                                    // 英文名
                                    RowLayout {
                                        Layout.fillWidth: true
                                        spacing: 8
                                        Label {
                                            text: "英文名:"
                                            Layout.preferredWidth: 64
                                            horizontalAlignment: Text.AlignRight
                                            font.pixelSize: 13
                                            color: Theme.textSecondary
                                        }
                                        ParameterTextField {
                                            Layout.fillWidth: true
                                            flat: false
                                            implicitHeight: 32
                                            text: root.controller ? root.controller.currentToolId : ""
                                            readOnly: !(root.controller && root.controller.isToolIdEditable)
                                            onCommitRequested: if (root.controller) root.controller.currentToolId = text
                                            onCancelRequested: text = root.controller ? root.controller.currentToolId : ""
                                        }
                                    }

                                    // 注册接口 (下拉框)
                                    RowLayout {
                                        Layout.fillWidth: true
                                        spacing: 8
                                        Label {
                                            text: "注册接口:"
                                            Layout.preferredWidth: 64
                                            horizontalAlignment: Text.AlignRight
                                            font.pixelSize: 13
                                            color: Theme.textSecondary
                                        }
                                        QuiComboBox {
                                            id: regKindCombo
                                            Layout.fillWidth: true
                                            implicitHeight: 32
                                            normalColor: Theme.surfaceBg
                                            disableColor: Theme.surfaceBg
                                            background: Rectangle {
                                                radius: Theme.radiusSmall
                                                border.width: 1
                                                border.color: regKindCombo.activeFocus ? Theme.paramEditorFocusBorder : Theme.border
                                                color: Theme.surfaceBg
                                            }
                                            model: ["Python 函数", "Python 类", "C++ 注册工厂"]
                                            currentIndex: {
                                                if (!root.controller) return 0
                                                if (root.controller.currentToolKind === "python_class") return 1
                                                if (root.controller.currentToolKind === "cpp") return 2
                                                return 0
                                            }
                                        }
                                    }

                                    // 脚本文件
                                    RowLayout {
                                        Layout.fillWidth: true
                                        spacing: 8
                                        Label {
                                            text: "脚本文件:"
                                            Layout.preferredWidth: 64
                                            horizontalAlignment: Text.AlignRight
                                            font.pixelSize: 13
                                            color: Theme.textSecondary
                                        }
                                        ParameterTextField {
                                            Layout.fillWidth: true
                                            flat: false
                                            implicitHeight: 32
                                            text: root.controller ? root.controller.currentToolFile : ""
                                            readOnly: !(root.controller && root.controller.isEditable)
                                            onCommitRequested: if (root.controller) root.controller.currentToolFile = text
                                            onCancelRequested: text = root.controller ? root.controller.currentToolFile : ""
                                        }
                                    }

                                    // 函数名
                                    RowLayout {
                                        Layout.fillWidth: true
                                        spacing: 8
                                        Label {
                                            text: "函数名:"
                                            Layout.preferredWidth: 64
                                            horizontalAlignment: Text.AlignRight
                                            font.pixelSize: 13
                                            color: Theme.textSecondary
                                        }
                                        ParameterTextField {
                                            Layout.fillWidth: true
                                            flat: false
                                            implicitHeight: 32
                                            text: root.controller ? root.controller.currentToolSymbol : ""
                                            readOnly: !(root.controller && root.controller.isEditable)
                                            onCommitRequested: if (root.controller) root.controller.currentToolSymbol = text
                                            onCancelRequested: text = root.controller ? root.controller.currentToolSymbol : ""
                                        }
                                    }

                                    // 版本
                                    RowLayout {
                                        Layout.fillWidth: true
                                        spacing: 8
                                        Label {
                                            text: "版本:"
                                            Layout.preferredWidth: 64
                                            horizontalAlignment: Text.AlignRight
                                            font.pixelSize: 13
                                            color: Theme.textSecondary
                                        }
                                        ParameterTextField {
                                            Layout.fillWidth: true
                                            flat: false
                                            implicitHeight: 32
                                            text: root.controller ? root.controller.currentToolVersion : ""
                                            readOnly: !(root.controller && root.controller.isEditable)
                                            onCommitRequested: if (root.controller) root.controller.currentToolVersion = text
                                            onCancelRequested: text = root.controller ? root.controller.currentToolVersion : ""
                                        }
                                    }

                                    Item { Layout.fillHeight: true }
                                }
                            }
                        }
                    }

                    // ========================== Column 3: Input Parameters ==========================
                    Item {
                        Layout.fillWidth: true
                        Layout.fillHeight: true
                        Layout.minimumWidth: 260
                        visible: root.controller && root.controller.hasTool

                        ColumnLayout {
                            anchors.fill: parent
                            spacing: 12

                            // Title bar with "+ 参数" button - strict baseline alignment
                            RowLayout {
                                Layout.fillWidth: true
                                Layout.preferredHeight: 32
                                spacing: 8

                                Label {
                                    text: "输入参数"
                                    font.bold: true
                                    font.pixelSize: 15
                                    color: Theme.textPrimary
                                    verticalAlignment: Text.AlignVCenter
                                }

                                Item { Layout.fillWidth: true }

                                Rectangle {
                                    Layout.preferredWidth: 60
                                    Layout.preferredHeight: 26
                                    radius: Theme.radiusSmall
                                    border.color: Theme.border
                                    border.width: 1
                                    color: (root.controller && root.controller.isEditable && addInMouse.containsMouse) ? Theme.menuButtonHoverBg : Theme.surfaceBg
                                    opacity: (root.controller && root.controller.isEditable) ? 1 : 0.4

                                    RowLayout {
                                        anchors.centerIn: parent
                                        spacing: 3
                                        Text { text: "+"; font.pixelSize: 12; color: Theme.textSecondary }
                                        Text { text: "参数"; font.pixelSize: 12; color: Theme.textSecondary }
                                    }

                                    MouseArea {
                                        id: addInMouse
                                        anchors.fill: parent
                                        hoverEnabled: true
                                        enabled: root.controller && root.controller.isEditable
                                        cursorShape: (root.controller && root.controller.isEditable) ? Qt.PointingHandCursor : Qt.ArrowCursor
                                        onClicked: {
                                            if (!root.controller) return
                                            root.controller.inputModel.addPort("新参数", "int", "0")
                                            inputView.positionViewAtEnd()
                                        }
                                    }
                                }
                            }

                            // Parameter cards list directly placed - Top aligns strictly with Column 2
                            ListView {
                                id: inputView
                                Layout.fillWidth: true
                                Layout.fillHeight: true
                                clip: true
                                spacing: 10
                                topMargin: 0
                                model: root.controller ? root.controller.inputModel : null
                                delegate: ToolPortCard {
                                    width: inputView.width
                                    portsModel: root.controller ? root.controller.inputModel : null
                                    listView: inputView
                                    types: root.controller ? root.controller.supportedTypes : []
                                    editable: root.controller && root.controller.isEditable
                                    stage: "inputs"
                                }
                            }
                        }
                    }

                    // ========================== Column 4: Output Parameters ==========================
                    Item {
                        Layout.fillWidth: true
                        Layout.fillHeight: true
                        Layout.minimumWidth: 260
                        visible: root.controller && root.controller.hasTool

                        ColumnLayout {
                            anchors.fill: parent
                            spacing: 12

                            // Title bar with "+ 参数" button - strict baseline alignment
                            RowLayout {
                                Layout.fillWidth: true
                                Layout.preferredHeight: 32
                                spacing: 8

                                Label {
                                    text: "输出参数"
                                    font.bold: true
                                    font.pixelSize: 15
                                    color: Theme.textPrimary
                                    verticalAlignment: Text.AlignVCenter
                                }

                                Item { Layout.fillWidth: true }

                                Rectangle {
                                    Layout.preferredWidth: 60
                                    Layout.preferredHeight: 26
                                    radius: Theme.radiusSmall
                                    border.color: Theme.border
                                    border.width: 1
                                    color: (root.controller && root.controller.isEditable && addOutMouse.containsMouse) ? Theme.menuButtonHoverBg : Theme.surfaceBg
                                    opacity: (root.controller && root.controller.isEditable) ? 1 : 0.4

                                    RowLayout {
                                        anchors.centerIn: parent
                                        spacing: 3
                                        Text { text: "+"; font.pixelSize: 12; color: Theme.textSecondary }
                                        Text { text: "参数"; font.pixelSize: 12; color: Theme.textSecondary }
                                    }

                                    MouseArea {
                                        id: addOutMouse
                                        anchors.fill: parent
                                        hoverEnabled: true
                                        enabled: root.controller && root.controller.isEditable
                                        cursorShape: (root.controller && root.controller.isEditable) ? Qt.PointingHandCursor : Qt.ArrowCursor
                                        onClicked: {
                                            if (!root.controller) return
                                            root.controller.outputModel.addPort("新参数", "int", "")
                                            outputView.positionViewAtEnd()
                                        }
                                    }
                                }
                            }

                            // Parameter cards list directly placed - Top aligns strictly with Column 2 & 3
                            ListView {
                                id: outputView
                                Layout.fillWidth: true
                                Layout.fillHeight: true
                                clip: true
                                spacing: 10
                                topMargin: 0
                                model: root.controller ? root.controller.outputModel : null
                                delegate: ToolPortCard {
                                    width: outputView.width
                                    portsModel: root.controller ? root.controller.outputModel : null
                                    listView: outputView
                                    types: root.controller ? root.controller.supportedTypes : []
                                    editable: root.controller && root.controller.isEditable
                                    stage: "outputs"
                                }
                            }
                        }
                    }
                }
            }

            // Footer top separator line strictly using Theme.border
            Rectangle {
                Layout.fillWidth: true
                height: 1
                color: Theme.border
            }

            // ------------------------------------------------------------- Footer Actions (Item container preserves rounded corners)
            Item {
                Layout.fillWidth: true
                Layout.preferredHeight: 56

                RowLayout {
                    anchors.fill: parent
                    anchors.leftMargin: 20
                    anchors.rightMargin: 20
                    anchors.topMargin: 12
                    anchors.bottomMargin: 12
                    spacing: 12

                    Label {
                        Layout.fillWidth: true
                        elide: Text.ElideRight
                        text: (root.controller && root.controller.isStatusError) ? root.controller.statusMessage : ""
                        color: Theme.error
                        font.pixelSize: 12
                    }

                    // 取消按钮
                    Rectangle {
                        id: cancelBtn
                        Layout.preferredWidth: 72
                        Layout.preferredHeight: 32
                        radius: Theme.radiusSmall
                        border.color: Theme.border
                        border.width: 1
                        color: cancelMouse.containsMouse ? Theme.menuButtonHoverBg : Theme.surfaceBg

                        Text {
                            anchors.centerIn: parent
                            text: "取消"
                            font.pixelSize: 13
                            color: Theme.textPrimary
                        }

                        MouseArea {
                            id: cancelMouse
                            anchors.fill: parent
                            hoverEnabled: true
                            cursorShape: Qt.PointingHandCursor
                            onClicked: {
                                if (root.controller && root.controller.isDirty)
                                    discardDialog.open()
                                else
                                    root.close()
                            }
                        }
                    }

                    // 保存按钮 (始终保持主题高亮色 Theme.highlight)
                    Rectangle {
                        id: saveBtn
                        Layout.preferredWidth: 72
                        Layout.preferredHeight: 32
                        radius: Theme.radiusSmall
                        color: saveMouse.containsMouse ? Qt.lighter(Theme.highlight, 1.1) : Theme.highlight

                        Text {
                            anchors.centerIn: parent
                            text: "保存"
                            font.pixelSize: 13
                            font.bold: true
                            color: "#FFFFFF"
                        }

                        MouseArea {
                            id: saveMouse
                            anchors.fill: parent
                            hoverEnabled: true
                            cursorShape: Qt.PointingHandCursor
                            onClicked: {
                                if (root.controller) {
                                    root.controller.saveAll()
                                    root.controller.cancelAll()
                                    root.close()
                                }
                            }
                        }
                    }
                }
            }
        }
    }

    // Context menu matching design popup
    QuiMenu {
        id: contextMenu

        QuiMenuItem {
            text: "修改二级目录"
            visible: root.menuItem !== null && root.menuItem.kind === "folder" && root.menuItem.depth > 0
            onTriggered: root.openPrompt("rename", "", root.menuItem.path)
        }
        QuiMenuItem {
            text: "删除二级目录"
            visible: root.menuItem !== null && root.menuItem.kind === "folder" && root.menuItem.depth > 0
            onTriggered: if (root.controller) root.controller.removeFolder(root.menuItem.path)
        }
        QuiMenuItem {
            text: "添加二级目录"
            visible: root.menuItem !== null && root.menuItem.kind === "folder" && root.menuItem.depth === 0
            onTriggered: root.openPrompt("addChild", root.menuItem.path, "")
        }
        QuiMenuItem {
            text: "添加工具"
            visible: root.menuItem !== null && root.menuItem.kind === "folder"
            onTriggered: if (root.controller) root.controller.addTool(root.menuItem.path)
        }
        QuiMenuItem {
            text: "重命名目录"
            visible: root.menuItem !== null && root.menuItem.kind === "folder" && root.menuItem.depth === 0
            onTriggered: root.openPrompt("rename", "", root.menuItem.path)
        }
        QuiMenuItem {
            text: "删除目录"
            visible: root.menuItem !== null && root.menuItem.kind === "folder" && root.menuItem.depth === 0
            onTriggered: if (root.controller) root.controller.removeFolder(root.menuItem.path)
        }
        QuiMenuItem {
            text: "同目录添加工具"
            visible: root.menuItem !== null && root.menuItem.kind === "tool"
            onTriggered: if (root.controller) root.controller.addTool(root.menuItem.folder)
        }
        QuiMenuItem {
            text: "复制工具"
            visible: root.menuItem !== null && root.menuItem.kind === "tool"
            onTriggered: if (root.controller) {
                root.controller.selectKey(root.menuItem.key)
                root.controller.cloneTool(root.menuItem.key)
            }
        }
        QuiMenuItem {
            text: "删除工具"
            visible: root.menuItem !== null && root.menuItem.kind === "tool"
            onTriggered: {
                if (!root.controller) return
                root.controller.selectKey(root.menuItem.key)
                confirmDeleteDialog.open()
            }
        }
        QuiMenuItem {
            text: "添加一级目录"
            visible: root.menuItem === null
            onTriggered: root.openPrompt("addRoot", "", "")
        }
        QuiMenuItem {
            text: "添加工具"
            visible: root.menuItem === null
            onTriggered: if (root.controller) root.controller.addTool("")
        }
    }

    Dialog {
        id: promptDialog
        property string mode: ""
        property string parentPath: ""
        property string path: ""
        modal: true
        standardButtons: Dialog.Ok | Dialog.Cancel
        width: 360

        ColumnLayout {
            anchors.fill: parent
            spacing: 8
            Label { text: "目录名称"; font.pixelSize: 13; color: Theme.textPrimary }
            TextField {
                id: promptField
                Layout.fillWidth: true
                onAccepted: promptDialog.accept()
            }
        }

        onOpened: {
            promptField.forceActiveFocus()
            promptField.selectAll()
        }
        onAccepted: {
            if (!root.controller) return
            if (mode === "rename")
                root.controller.renameFolder(path, promptField.text)
            else
                root.controller.addFolder(parentPath, promptField.text)
        }
    }

    Dialog {
        id: confirmDeleteDialog
        modal: true
        title: "确认删除工具"
        standardButtons: Dialog.Yes | Dialog.No
        width: 420

        Label {
            width: 380
            wrapMode: Text.WordWrap
            color: Theme.textPrimary
            text: {
                if (!root.controller) return ""
                const count = root.controller.toolUsage(root.controller.currentToolId)
                if (count > 0)
                    return "当前工程有 " + count + " 个节点引用该工具。删除后这些节点保留为工具缺失，需要手动修复。确定删除？"
                return "确定删除工具声明及其专属脚本目录？保存后才会从工具目录移除。"
            }
        }

        onAccepted: if (root.controller) root.controller.deleteTool(root.controller.currentKey)
    }

    Dialog {
        id: discardDialog
        modal: true
        title: "放弃未保存的修改"
        standardButtons: Dialog.Yes | Dialog.No
        width: 420

        Label {
            width: 380
            wrapMode: Text.WordWrap
            color: Theme.textPrimary
            text: "本窗口有尚未保存的工具与目录修改。放弃后这些修改会全部丢弃，工具目录不会变化。"
        }

        onAccepted: {
            if (root.controller)
                root.controller.cancelAll()
            root.close()
        }
    }

    onClosing: function(close) {
        if (root.controller && root.controller.isDirty && visible) {
            close.accepted = false
            discardDialog.open()
            return
        }
        if (root.controller)
            root.controller.cancelAll()
    }
}
