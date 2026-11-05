import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import QtQuick.Window
import quickui
import VisionRT.Style 1.0

// Custom tool authoring: catalogue and tool card, then the declared inputs and outputs, each in its
// own scrolling column. Every edit is a draft; the footer decides whether the window writes or
// discards the whole set.
Window {
    id: root

    title: "自定义工具"
    flags: Qt.Dialog | Qt.WindowTitleHint | Qt.WindowSystemMenuHint | Qt.WindowMinMaxButtonsHint | Qt.WindowCloseButtonHint
    modality: Qt.NonModal
    color: QuiColor.WindowBackground
    width: 1280
    height: 760
    minimumWidth: 1024
    minimumHeight: 600

    // Injected by the workbench so the window can also be exercised without the whole application.
    property var controller: null
    property var flatTree: []
    // Folders start expanded; the map only remembers the ones the user closed.
    property var collapsed: ({})
    property var menuItem: null

    function openDialog() {
        if (root.visibility === Window.Minimized)
            root.showNormal()
        root.show()
        root.raise()
        root.requestActivate()
    }

    function rebuildTree() {
        let rows = []
        const nodes = root.controller ? root.controller.tree : []
        function walk(list, depth) {
            for (let i = 0; i < list.length; ++i) {
                const node = list[i]
                const isFolder = node.kind === "folder"
                const hasChildren = (node.children || []).length > 0
                rows.push({
                    kind: node.kind,
                    name: node.name,
                    path: node.path || "",
                    key: node.key || "",
                    toolId: node.toolId || "",
                    folder: node.folder || "",
                    toolKind: node.toolKind || "",
                    draft: node.draft === true,
                    depth: depth,
                    isFolder: isFolder,
                    hasChildren: hasChildren,
                    expanded: isFolder && !root.collapsed[node.path]
                })
                if (isFolder && !root.collapsed[node.path] && hasChildren)
                    walk(node.children, depth + 1)
            }
        }
        walk(nodes, 0)
        root.flatTree = rows
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
        promptDialog.title = mode === "rename" ? "重命名目录"
                            : (mode === "addChild" ? "添加二级目录" : "添加一级目录")
        promptDialog.open()
    }

    Component.onCompleted: rebuildTree()
    onVisibleChanged: if (visible) rebuildTree()

    Connections {
        target: root.controller
        function onTreeChanged() { root.rebuildTree() }
    }

    ColumnLayout {
        anchors.fill: parent
        spacing: 0

        RowLayout {
            Layout.fillWidth: true
            Layout.fillHeight: true
            spacing: 0

            // ---------------------------------------------------------------- catalogue
            Rectangle {
                Layout.preferredWidth: 230
                Layout.fillHeight: true
                color: QuiColor.CardBackground
                border.color: QuiColor.Border
                border.width: 1

                ColumnLayout {
                    anchors.fill: parent
                    anchors.margins: 8
                    spacing: 6

                    Label {
                        text: "工具库"
                        font.bold: true
                        font.pixelSize: Theme.fontSizeMedium
                        color: QuiColor.FontPrimary
                    }

                    ListView {
                        id: treeView
                        Layout.fillWidth: true
                        Layout.fillHeight: true
                        clip: true
                        model: root.flatTree

                        delegate: Rectangle {
                            required property var modelData
                            required property int index

                            width: treeView.width
                            height: Theme.treeRowHeight
                            color: modelData.kind === "tool" && root.controller
                                   && root.controller.currentKey === modelData.key
                                   ? Theme.treeItemSelectedBg
                                   : (rowMouse.containsMouse ? Theme.treeItemHoverBg : "transparent")

                            MouseArea {
                                id: rowMouse
                                anchors.fill: parent
                                hoverEnabled: true
                                acceptedButtons: Qt.LeftButton | Qt.RightButton
                                onClicked: function(mouse) {
                                    if (mouse.button === Qt.RightButton) {
                                        root.menuItem = modelData
                                        contextMenu.popup()
                                        return
                                    }
                                    if (modelData.kind === "folder")
                                        root.toggleFolder(modelData.path)
                                    else
                                        root.selectKey(modelData.key)
                                }
                            }

                            RowLayout {
                                anchors.fill: parent
                                anchors.leftMargin: 6 + modelData.depth * Theme.treeIndent
                                anchors.rightMargin: 6
                                spacing: 5

                                Item {
                                    Layout.preferredWidth: Theme.treeArrowWidth
                                    Layout.fillHeight: true
                                    visible: modelData.hasChildren
                                    QuiTextIcon {
                                        anchors.centerIn: parent
                                        iconSource: modelData.expanded ? QuiFontIcon.ChevronDownSmall
                                                                       : QuiFontIcon.ChevronRightSmall
                                        iconSize: Theme.treeArrowSize
                                        iconColor: Theme.treeArrowColor
                                    }
                                }

                                QuiTextIcon {
                                    visible: modelData.kind === "folder" || modelData.hasChildren
                                    iconSource: modelData.expanded ? QuiFontIcon.FolderOpen : QuiFontIcon.Folder
                                    iconSize: Theme.treeFolderIconSize
                                    iconColor: Theme.treeFolderIconColor
                                }

                                Label {
                                    Layout.fillWidth: true
                                    text: modelData.name
                                    elide: Text.ElideRight
                                    font.pixelSize: Theme.treeItemFontSize
                                    color: modelData.editable === false && modelData.kind === "tool"
                                           ? Theme.treeItemUnavailableText
                                           : Theme.treeItemText
                                }

                                Label {
                                    visible: modelData.draft
                                    text: "草稿"
                                    font.pixelSize: Theme.fontSizeCaption
                                    color: QuiColor.FontCaption
                                }
                            }
                        }

                        // Empty space between rows still belongs to the catalogue. A row's own
                        // mouse area consumes its click, so only the gaps reach this handler.
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

            // ---------------------------------------------------------------- tool card
            Rectangle {
                Layout.preferredWidth: 300
                Layout.fillHeight: true
                color: QuiColor.CardBackground
                border.color: QuiColor.Border
                border.width: 1
                visible: root.controller && root.controller.hasTool

                ScrollView {
                    id: toolCardScroll
                    anchors.fill: parent
                    anchors.margins: 12
                    clip: true
                    contentWidth: availableWidth

                    ColumnLayout {
                        width: toolCardScroll.availableWidth
                        spacing: 8

                        Label {
                            text: "工具定义"
                            font.bold: true
                            font.pixelSize: Theme.fontSizeMedium
                            color: QuiColor.FontPrimary
                        }

                        Label { text: "中文名"; font.pixelSize: Theme.fontSizeCaption; color: QuiColor.FontCaption }
                        ParameterTextField {
                            Layout.fillWidth: true
                            text: root.controller ? root.controller.currentToolName : ""
                            readOnly: !(root.controller && root.controller.isEditable)
                            onCommitRequested: if (root.controller) root.controller.currentToolName = text
                            onCancelRequested: text = root.controller ? root.controller.currentToolName : ""
                        }

                        Label {
                            text: (root.controller && root.controller.isToolIdEditable)
                                  ? "英文注册标识（首次保存前可修改）"
                                  : "英文注册标识"
                            font.pixelSize: Theme.fontSizeCaption
                            color: QuiColor.FontCaption
                        }
                        ParameterTextField {
                            Layout.fillWidth: true
                            text: root.controller ? root.controller.currentToolId : ""
                            readOnly: !(root.controller && root.controller.isToolIdEditable)
                            onCommitRequested: if (root.controller) root.controller.currentToolId = text
                            onCancelRequested: text = root.controller ? root.controller.currentToolId : ""
                        }

                        Label { text: "版本"; font.pixelSize: Theme.fontSizeCaption; color: QuiColor.FontCaption }
                        ParameterTextField {
                            Layout.fillWidth: true
                            text: root.controller ? root.controller.currentToolVersion : ""
                            readOnly: !(root.controller && root.controller.isEditable)
                            onCommitRequested: if (root.controller) root.controller.currentToolVersion = text
                            onCancelRequested: text = root.controller ? root.controller.currentToolVersion : ""
                        }

                        Label { text: "分类目录"; font.pixelSize: Theme.fontSizeCaption; color: QuiColor.FontCaption }
                        ParameterTextField {
                            Layout.fillWidth: true
                            text: root.controller ? root.controller.currentToolCategory : ""
                            readOnly: !(root.controller && root.controller.isEditable)
                            onCommitRequested: if (root.controller) root.controller.currentToolCategory = text
                            onCancelRequested: text = root.controller ? root.controller.currentToolCategory : ""
                        }

                        Label { text: "注册接口"; font.pixelSize: Theme.fontSizeCaption; color: QuiColor.FontCaption }
                        Label {
                            Layout.fillWidth: true
                            wrapMode: Text.WordWrap
                            font.pixelSize: Theme.fontSizeCaption
                            color: QuiColor.FontPrimary
                            text: {
                                if (!root.controller) return ""
                                if (root.controller.currentToolKind === "python_function") return "Python 函数"
                                if (root.controller.currentToolKind === "python_class") return "Python 类"
                                if (root.controller.currentToolKind === "cpp") return "C++ 注册工厂"
                                return "宿主内置"
                            }
                        }

                        ColumnLayout {
                            Layout.fillWidth: true
                            spacing: 4
                            visible: root.controller && root.controller.isPython

                            Label { text: "Python 相对脚本"; font.pixelSize: Theme.fontSizeCaption; color: QuiColor.FontCaption }
                            ParameterTextField {
                                Layout.fillWidth: true
                                text: root.controller ? root.controller.currentToolFile : ""
                                readOnly: !(root.controller && root.controller.isEditable)
                                onCommitRequested: if (root.controller) root.controller.currentToolFile = text
                                onCancelRequested: text = root.controller ? root.controller.currentToolFile : ""
                            }

                            Label { text: "函数名或类名"; font.pixelSize: Theme.fontSizeCaption; color: QuiColor.FontCaption }
                            ParameterTextField {
                                Layout.fillWidth: true
                                text: root.controller ? root.controller.currentToolSymbol : ""
                                readOnly: !(root.controller && root.controller.isEditable)
                                onCommitRequested: if (root.controller) root.controller.currentToolSymbol = text
                                onCancelRequested: text = root.controller ? root.controller.currentToolSymbol : ""
                            }

                            RowLayout {
                                Layout.fillWidth: true
                                spacing: 6
                                Button {
                                    text: "打开脚本"
                                    enabled: root.controller && root.controller.currentToolScript !== ""
                                    onClicked: if (root.controller) root.controller.openCodeFile()
                                }
                                Button {
                                    text: "脚本目录"
                                    enabled: root.controller && root.controller.currentToolScript !== ""
                                    onClicked: if (root.controller) root.controller.openCodeDirectory()
                                }
                            }
                        }

                        Label {
                            Layout.fillWidth: true
                            visible: root.controller && !root.controller.isEditable
                            wrapMode: Text.WordWrap
                            font.pixelSize: Theme.fontSizeCaption
                            color: QuiColor.FontCaption
                            text: "内置工具：定义只读，可查看并复制参数。需要修改时先在右键菜单复制为自有工具，再在副本上编辑并保存。"
                        }

                        Label {
                            Layout.fillWidth: true
                            visible: root.controller && root.controller.isDirty
                            text: root.controller ? root.controller.impactSummary : ""
                            wrapMode: Text.WordWrap
                            font.pixelSize: Theme.fontSizeCaption
                            color: QuiColor.FontCaption
                        }

                        RowLayout {
                            Layout.fillWidth: true
                            spacing: 6
                            visible: root.controller && root.controller.isPython
                            Button {
                                Layout.fillWidth: true
                                text: "复制为自有工具"
                                enabled: root.controller && root.controller.canClone
                                onClicked: if (root.controller) root.controller.cloneTool(root.controller.currentKey)
                            }
                            Button {
                                Layout.fillWidth: true
                                text: "删除工具"
                                enabled: root.controller && root.controller.isEditable
                                onClicked: confirmDeleteDialog.open()
                            }
                        }

                        Item { Layout.fillHeight: true }
                    }
                }
            }

            // ---------------------------------------------------------------- inputs
            ColumnLayout {
                Layout.fillWidth: true
                Layout.fillHeight: true
                Layout.minimumWidth: 280
                spacing: 0
                visible: root.controller && root.controller.hasTool

                Rectangle {
                    Layout.fillWidth: true
                    Layout.preferredHeight: 34
                    color: QuiColor.WindowBackground
                    border.color: QuiColor.Border
                    RowLayout {
                        anchors.fill: parent
                        anchors.leftMargin: 10
                        anchors.rightMargin: 6
                        Label {
                            text: "输入参数"
                            font.bold: true
                            font.pixelSize: Theme.fontSizeBase
                            color: QuiColor.FontPrimary
                        }
                        Item { Layout.fillWidth: true }
                        Button {
                            text: "＋ 参数"
                            flat: true
                            enabled: root.controller && root.controller.isEditable
                            onClicked: {
                                if (!root.controller) return
                                root.controller.inputModel.addPort("新参数", "int", "0")
                                inputView.positionViewAtEnd()
                            }
                        }
                    }
                }

                // Class tools initialise before they execute; those parameters are a separate stage
                // and never appear among the execute inputs.
                Rectangle {
                    Layout.fillWidth: true
                    Layout.preferredHeight: initColumn.implicitHeight + 12
                    visible: root.controller && root.controller.showsInit
                    color: QuiColor.CardBackground
                    border.color: QuiColor.Border

                    ColumnLayout {
                        id: initColumn
                        anchors.fill: parent
                        anchors.margins: 6
                        spacing: 4
                        Label {
                            text: "初始化参数"
                            font.pixelSize: Theme.fontSizeCaption
                            color: QuiColor.FontCaption
                        }
                        ListView {
                            id: initView
                            Layout.fillWidth: true
                            Layout.preferredHeight: Math.min(contentHeight, 180)
                            interactive: contentHeight > height
                            clip: true
                            spacing: 6
                            model: root.controller ? root.controller.initModel : null
                            delegate: ToolPortCard {
                                width: initView.width
                                portsModel: root.controller ? root.controller.initModel : null
                                listView: initView
                                types: root.controller ? root.controller.supportedTypes : []
                                editable: root.controller && root.controller.isEditable
                                stage: "init"
                            }
                        }
                    }
                }

                ListView {
                    id: inputView
                    Layout.fillWidth: true
                    Layout.fillHeight: true
                    clip: true
                    spacing: 6
                    topMargin: 6
                    bottomMargin: 6
                    leftMargin: 8
                    rightMargin: 8
                    model: root.controller ? root.controller.inputModel : null
                    delegate: ToolPortCard {
                        width: inputView.width - 16
                        portsModel: root.controller ? root.controller.inputModel : null
                        listView: inputView
                        types: root.controller ? root.controller.supportedTypes : []
                        editable: root.controller && root.controller.isEditable
                        stage: "inputs"
                    }
                }
            }

            // ---------------------------------------------------------------- outputs
            ColumnLayout {
                Layout.fillWidth: true
                Layout.fillHeight: true
                Layout.minimumWidth: 280
                spacing: 0
                visible: root.controller && root.controller.hasTool

                Rectangle {
                    Layout.fillWidth: true
                    Layout.preferredHeight: 34
                    color: QuiColor.WindowBackground
                    border.color: QuiColor.Border
                    RowLayout {
                        anchors.fill: parent
                        anchors.leftMargin: 10
                        anchors.rightMargin: 6
                        Label {
                            text: "输出参数"
                            font.bold: true
                            font.pixelSize: Theme.fontSizeBase
                            color: QuiColor.FontPrimary
                        }
                        Item { Layout.fillWidth: true }
                        Button {
                            text: "＋ 参数"
                            flat: true
                            enabled: root.controller && root.controller.isEditable
                            onClicked: {
                                if (!root.controller) return
                                root.controller.outputModel.addPort("新参数", "int", "")
                                outputView.positionViewAtEnd()
                            }
                        }
                    }
                }

                ListView {
                    id: outputView
                    Layout.fillWidth: true
                    Layout.fillHeight: true
                    clip: true
                    spacing: 6
                    topMargin: 6
                    bottomMargin: 6
                    leftMargin: 8
                    rightMargin: 8
                    model: root.controller ? root.controller.outputModel : null
                    delegate: ToolPortCard {
                        width: outputView.width - 16
                        portsModel: root.controller ? root.controller.outputModel : null
                        listView: outputView
                        types: root.controller ? root.controller.supportedTypes : []
                        editable: root.controller && root.controller.isEditable
                        stage: "outputs"
                    }
                }
            }
        }

        // ---------------------------------------------------------------- footer
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
                    Layout.fillWidth: true
                    elide: Text.ElideRight
                    text: root.controller ? root.controller.statusMessage : ""
                    color: (root.controller && root.controller.isStatusError) ? QuiColor.Error : QuiColor.FontCaption
                }

                Button {
                    text: "取消"
                    onClicked: {
                        if (root.controller && root.controller.isDirty)
                            discardDialog.open()
                        else
                            root.close()
                    }
                }

                Button {
                    text: "保存"
                    enabled: root.controller && root.controller.canSave
                    onClicked: {
                        if (root.controller && root.controller.saveAll()) {
                            root.controller.cancelAll()
                            root.close()
                        }
                    }
                }
            }
        }
    }

    Menu {
        id: contextMenu

        MenuItem {
            text: "添加一级目录"
            visible: root.menuItem === null
            onTriggered: root.openPrompt("addRoot", "", "")
        }
        MenuItem {
            text: "添加工具"
            visible: root.menuItem === null
            onTriggered: if (root.controller) root.controller.addTool("")
        }
        MenuItem {
            text: "添加二级目录"
            visible: root.menuItem !== null && root.menuItem.kind === "folder" && root.menuItem.depth === 0
            onTriggered: root.openPrompt("addChild", root.menuItem.path, "")
        }
        MenuItem {
            text: "添加工具"
            visible: root.menuItem !== null && root.menuItem.kind === "folder"
            onTriggered: if (root.controller) root.controller.addTool(root.menuItem.path)
        }
        MenuItem {
            text: "重命名目录"
            visible: root.menuItem !== null && root.menuItem.kind === "folder"
            onTriggered: root.openPrompt("rename", "", root.menuItem.path)
        }
        MenuItem {
            text: "删除空目录"
            visible: root.menuItem !== null && root.menuItem.kind === "folder"
            onTriggered: if (root.controller) root.controller.removeFolder(root.menuItem.path)
        }
        MenuSeparator {
            visible: root.menuItem !== null
        }
        MenuItem {
            text: "同目录添加工具"
            visible: root.menuItem !== null && root.menuItem.kind === "tool"
            onTriggered: if (root.controller) root.controller.addTool(root.menuItem.folder)
        }
        MenuItem {
            text: "复制工具"
            visible: root.menuItem !== null && root.menuItem.kind === "tool"
            onTriggered: if (root.controller) {
                root.controller.selectKey(root.menuItem.key)
                root.controller.cloneTool(root.menuItem.key)
            }
        }
        MenuItem {
            text: "删除工具"
            visible: root.menuItem !== null && root.menuItem.kind === "tool"
            onTriggered: {
                if (!root.controller) return
                root.controller.selectKey(root.menuItem.key)
                confirmDeleteDialog.open()
            }
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
            Label { text: "目录名称"; font.pixelSize: Theme.fontSizeCaption }
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
