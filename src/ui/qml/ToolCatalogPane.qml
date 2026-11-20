import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import quickui
import VisionRT.Ui 1.0

Item {
    id: root
    objectName: "toolCatalog"
    property var controller: null
    property var flatTree: []
    property var expandedFolders: ({})
    property var menuItem: null
    property string searchKeyword: ""
    function rebuildTree() {
        let rows = [];
        const nodes = root.controller ? root.controller.tree : [];
        const filter = root.searchKeyword.trim().toLowerCase();
        function matchNode(node) {
            if (!filter)
                return true;
            if (node.kind === CatalogEntryKind.Tool) {
                const name = (node.name || "").toLowerCase();
                const id = (node.toolId || "").toLowerCase();
                return name.indexOf(filter) >= 0 || id.indexOf(filter) >= 0;
            }
            if (node.children) {
                for (let i = 0; i < node.children.length; ++i) {
                    if (matchNode(node.children[i]))
                        return true;
                }
            }
            return false;
        }
        function walk(list, depth) {
            for (let i = 0; i < list.length; ++i) {
                const node = list[i];
                if (filter && !matchNode(node))
                    continue;
                const isFolder = node.kind === CatalogEntryKind.Folder;
                const hasChildren = (node.children || []).length > 0;
                const isExpanded = filter ? true : (isFolder && root.expandedFolders[node.path] === true);
                rows.push({
                    kind: node.kind,
                    display: node.name,
                    name: node.name,
                    path: node.path || "",
                    key: node.key || "",
                    stableKey: node.key || "",
                    toolId: node.toolId || "",
                    folder: node.folder || "",
                    toolKind: node.toolKind ?? ToolKind.PythonFunction,
                    draft: node.draft === true,
                    depth: depth,
                    isFolder: isFolder,
                    hasChildren: hasChildren,
                    leaf: !isFolder,
                    available: true,
                    status: "",
                    expanded: isExpanded
                });
                if (isFolder && isExpanded && hasChildren)
                    walk(node.children, depth + 1);
            }
        }
        walk(nodes, 0);
        root.flatTree = rows;
    }

    function toggleFolder(path) {
        if (root.expandedFolders[path])
            delete root.expandedFolders[path];
        else
            root.expandedFolders[path] = true;
        root.rebuildTree();
    }

    function selectKey(key) {
        if (root.controller)
            root.controller.selectKey(key);
    }

    function openPrompt(mode, parentPath, path) {
        promptDialog.mode = mode;
        promptDialog.parentPath = parentPath || "";
        promptDialog.path = path || "";
        promptDialog.value = mode === "rename" ? root.controller.folderName(path) : "";
        promptDialog.title = mode === "rename" ? "修改二级目录" : (mode === "addChild" ? "添加二级目录" : "添加一级目录");
        promptDialog.open();
    }

    Component.onCompleted: {
        rebuildTree();
    }
    onVisibleChanged: {
        if (visible) {
            rebuildTree();
        }
    }

    Connections {
        target: root.controller
        function onTreeChanged() {
            if (root.controller && root.controller.hasTool && root.controller.currentToolCategory) {
                let parts = root.controller.currentToolCategory.split('/');
                let prefix = "";
                for (let p = 0; p < parts.length; ++p) {
                    prefix = prefix ? prefix + '/' + parts[p] : parts[p];
                    root.expandedFolders[prefix] = true;
                }
            }
            root.rebuildTree();
        }
    }

    ColumnLayout {
        anchors.fill: parent
        anchors.topMargin: 5
        anchors.bottomMargin: 5
        spacing: 5

        Rectangle {
            Layout.fillWidth: true
            Layout.preferredHeight: VisionRTSize.ContentSize
            radius: 3
            color: QuiColor.Primary
            border.color: searchInput.activeFocus ? QuiColor.Highlight : QuiColor.Border
            border.width: 1

            RowLayout {
                anchors.fill: parent
                anchors.leftMargin: 5
                anchors.rightMargin: 5
                spacing: 5

                QuiTextIcon {
                    iconSource: QuiFontIcon.Search
                    iconSize: VisionRTSize.IconSize
                    iconColor: QuiColor.FontCaption
                }

                TextInput {
                    id: searchInput
                    Layout.fillWidth: true
                    font: QuiFont.Body
                    color: QuiColor.FontPrimary
                    selectByMouse: true
                    verticalAlignment: TextInput.AlignVCenter
                    onTextChanged: {
                        root.searchKeyword = text;
                        root.rebuildTree();
                    }

                    Text {
                        anchors.fill: parent
                        verticalAlignment: Text.AlignVCenter
                        text: "搜索工具"
                        font: QuiFont.Body
                        color: QuiColor.FontCaption
                        visible: !searchInput.text && !searchInput.activeFocus
                    }
                }

                WorkbenchActionButton {
                    visible: searchInput.text !== ""
                    Layout.preferredWidth: implicitHeight
                    iconSource: QuiFontIcon.Cancel
                    normalColor: "transparent"
                    iconColor: QuiColor.FontCaption
                    onClicked: searchInput.text = ""
                }
            }
        }

        ListView {
            id: treeView
            Layout.fillWidth: true
            Layout.fillHeight: true
            clip: true
            spacing: 5
            cacheBuffer: 5000
            model: root.flatTree

            // Background click area to deselect when clicking on empty space
            MouseArea {
                anchors.fill: parent
                z: -1
                acceptedButtons: Qt.LeftButton
                onClicked: {
                    if (root.controller)
                        root.controller.selectKey("");
                }
            }

            delegate: ToolTreeDelegate {
                required property int index
                required property var modelData

                width: ListView.view ? ListView.view.width : (treeView ? treeView.width : 200)
                isEditor: true
                row: index
                column: 0
                isTreeNode: true
                depth: modelData.depth
                hasChildren: modelData.hasChildren
                expanded: modelData.expanded
                model: modelData
                selectedKey: root.controller ? root.controller.currentKey : ""
                onSelected: function (key) {
                    root.selectKey(key);
                }
                onToggleExpandedRequested: {
                    if (modelData.isFolder)
                        root.toggleFolder(modelData.path);
                }
                onContextMenuRequested: function (mouse, itemModel) {
                    root.menuItem = itemModel;
                    contextMenu.popup();
                }
            }

            TapHandler {
                acceptedButtons: Qt.RightButton
                onTapped: {
                    root.menuItem = null;
                    contextMenu.popup();
                }
            }
        }
    }
    QuiMenu {
        id: contextMenu

        QuiMenuItem {
            text: "修改二级目录"
            visible: root.menuItem !== null && root.menuItem.kind === CatalogEntryKind.Folder && root.menuItem.depth > 0
            onTriggered: root.openPrompt("rename", "", root.menuItem.path)
        }
        QuiMenuItem {
            text: "删除二级目录"
            visible: root.menuItem !== null && root.menuItem.kind === CatalogEntryKind.Folder && root.menuItem.depth > 0
            onTriggered: if (root.controller)
                root.controller.removeFolder(root.menuItem.path)
        }
        QuiMenuItem {
            text: "添加二级目录"
            visible: root.menuItem !== null && root.menuItem.kind === CatalogEntryKind.Folder && root.menuItem.depth === 0
            onTriggered: root.openPrompt("addChild", root.menuItem.path, "")
        }
        QuiMenuItem {
            text: "添加工具"
            visible: root.menuItem !== null && root.menuItem.kind === CatalogEntryKind.Folder
            onTriggered: if (root.controller)
                root.controller.addTool(root.menuItem.path)
        }
        QuiMenuItem {
            text: "重命名目录"
            visible: root.menuItem !== null && root.menuItem.kind === CatalogEntryKind.Folder && root.menuItem.depth === 0
            onTriggered: root.openPrompt("rename", "", root.menuItem.path)
        }
        QuiMenuItem {
            text: "删除目录"
            visible: root.menuItem !== null && root.menuItem.kind === CatalogEntryKind.Folder && root.menuItem.depth === 0
            onTriggered: if (root.controller)
                root.controller.removeFolder(root.menuItem.path)
        }
        QuiMenuItem {
            text: "同目录添加工具"
            visible: root.menuItem !== null && root.menuItem.kind === CatalogEntryKind.Tool
            onTriggered: if (root.controller)
                root.controller.addTool(root.menuItem.folder)
        }
        QuiMenuItem {
            text: "复制工具"
            visible: root.menuItem !== null && root.menuItem.kind === CatalogEntryKind.Tool
            onTriggered: if (root.controller) {
                root.controller.selectKey(root.menuItem.key);
                root.controller.cloneTool(root.menuItem.key);
            }
        }
        QuiMenuItem {
            text: "删除工具"
            visible: root.menuItem !== null && root.menuItem.kind === CatalogEntryKind.Tool
            onTriggered: {
                if (!root.controller)
                    return;
                root.controller.selectKey(root.menuItem.key);
                confirmDeleteDialog.open();
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
            onTriggered: if (root.controller)
                root.controller.addTool("")
        }
    }

    WorkbenchDialog {
        id: promptDialog
        property string mode: ""
        property string value: ""
        property string parentPath: ""
        property string path: ""
        positiveText: "确定"
        negativeText: "取消"
        width: 360
        contentDelegate: Component {
            ColumnLayout {
                width: parent.width
                spacing: 5
                QuiText {
                    text: "目录名称"
                    color: QuiColor.FontCaption
                }
                ParameterTextField {
                    objectName: "folderNameField"
                    Layout.fillWidth: true
                    flat: false
                    text: promptDialog.value
                    onTextChanged: promptDialog.value = text
                    Component.onCompleted: {
                        promptDialog.initialFocusItem = inputItem;
                        inputItem.selectAll();
                    }
                    onCommitRequested: {
                        promptDialog.positiveClicked();
                        promptDialog.close();
                    }
                }
            }
        }
        onPositiveClicked: {
            if (mode === "rename")
                root.controller.renameFolder(path, value);
            else
                root.controller.addFolder(parentPath, value);
        }
    }
    WorkbenchDialog {
        id: confirmDeleteDialog
        title: "确认删除工具"
        positiveText: "确定"
        negativeText: "取消"
        width: 420
        message: {
            if (!root.controller)
                return "";
            const count = root.controller.toolUsage(root.controller.currentToolId);
            if (count > 0)
                return "当前工程有 " + count + " 个节点引用该工具。删除后这些节点保留为工具缺失，需要手动修复。确定删除？";
            return "确定删除工具声明及其专属脚本目录？保存后才会从工具目录移除。";
        }
        onPositiveClicked: root.controller.deleteTool(root.controller.currentKey)
    }
}
