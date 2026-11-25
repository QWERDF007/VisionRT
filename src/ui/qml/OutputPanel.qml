import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import QtQuick.Window
import quickui
import VisionRT.Ui 1.0

Rectangle {
    id: root
    color: QuiColor.Primary
    border.width: 0
    property bool relations: false
    readonly property int nameColumnWidth: Math.max(130, Math.round(table.width * 0.35))
    readonly property int valueColumnWidth: Math.max(180, table.width - nameColumnWidth)
    function columnWidthFor(column) {
        if (column === 0)
            return root.nameColumnWidth;
        if ((root.relations && column === 3) || (!root.relations && column === 2))
            return root.valueColumnWidth;
        return 0;
    }
    property var selectedContext: ({})
    // The copy request is released only after the menu has actually closed, so the popup teardown
    // can never race the bounded readback that is about to capture the same publication.
    property var pendingContext: ({})
    property string selectedName: ""
    property bool selectedSystem: true
    property var cascadeTools: []
    function showNameMenu(options, item, point) {
        selectedContext = app.portDeclarationContext(options.portId, true);
        if (!app.portContextCurrent(selectedContext))
            return;
        selectedName = options.name;
        selectedSystem = options.system;
        // The name column is the connection menu: an empty candidate list opens nothing at all
        // instead of offering a placeholder row.
        cascadeTools = app.cascadeOutputCandidates(selectedContext);
        if (cascadeTools.length === 0)
            return;
        let position = item.mapToItem(root, point.x, point.y);
        nameMenu.popup(root, position.x, position.y);
    }
    function showDataMenu(options, item, point) {
        // A row without a published value still opens its menu so that copy is visibly disabled
        // rather than silently missing.
        const resultContext = app.portResultContext(options.portId);
        selectedContext = resultContext;
        selectedName = options.name;
        selectedSystem = options.system;
        pendingContext = ({});
        let position = item.mapToItem(root, point.x, point.y);
        copyMenu.popup(root, position.x, position.y);
    }
    function copyEnabled() {
        return app.workspaceEnabled && !isEmptyContext(selectedContext) && selectedContext.available !== false && app.portContextCurrent(selectedContext);
    }
    function isEmptyContext(context) {
        return !context || Object.keys(context).length === 0;
    }
    function invalidateMenu() {
        pendingContext = ({});
        selectedContext = ({});
        copyMenu.close();
        nameMenu.close();
    }
    function validateMenu() {
        if (!app.workspaceEnabled || isEmptyContext(selectedContext) || !app.portContextCurrent(selectedContext))
            invalidateMenu();
    }
    ColumnLayout {
        anchors.fill: parent
        spacing: 0
        Rectangle {
            Layout.fillWidth: true
            Layout.preferredHeight: VisionRTSize.BarSize
            color: QuiColor.Primary
            Rectangle {
                anchors.left: parent.left
                anchors.right: parent.right
                anchors.bottom: parent.bottom
                height: 2
                color: QuiColor.Border
            }
            RowLayout {
                anchors.fill: parent
                anchors.leftMargin: 5
                anchors.rightMargin: 5
                spacing: 5
                QuiText {
                    text: app.outputTitle
                    font: QuiFont.Body
                    color: QuiColor.FontPrimary
                }
                Item {
                    Layout.fillWidth: true
                }
                Rectangle {
                    Layout.preferredWidth: VisionRTSize.ParamLabelWidth
                    Layout.preferredHeight: VisionRTSize.ContentSize
                    radius: 3
                    color: QuiColor.Button
                    border.color: QuiColor.Highlight
                    border.width: 1
                    clip: true
                    Row {
                        anchors.fill: parent
                        Rectangle {
                            width: parent.width / 2
                            height: parent.height
                            color: !root.relations ? QuiColor.Highlight : "transparent"
                            QuiText {
                                anchors.centerIn: parent
                                text: "数据"
                                font: QuiFont.Body
                                color: !root.relations ? QuiColor.Button : QuiColor.Highlight
                            }
                            MouseArea {
                                anchors.fill: parent
                                cursorShape: Qt.PointingHandCursor
                                onClicked: root.relations = false
                            }
                        }
                        Rectangle {
                            width: parent.width / 2
                            height: parent.height
                            color: root.relations ? QuiColor.Highlight : "transparent"
                            QuiText {
                                anchors.centerIn: parent
                                text: "关系"
                                font: QuiFont.Body
                                color: root.relations ? QuiColor.Button : QuiColor.Highlight
                            }
                            MouseArea {
                                anchors.fill: parent
                                cursorShape: Qt.PointingHandCursor
                                onClicked: root.relations = true
                            }
                        }
                    }
                }
            }
        }
        QuiTableView {
            id: table
            view.ScrollBar.vertical: WorkbenchScrollBar {}
            view.ScrollBar.horizontal: WorkbenchScrollBar {}
            objectName: "outputTable"
            Layout.fillWidth: true
            Layout.fillHeight: true
            boundsBehavior: Flickable.StopAtBounds
            externalModel: app.outputModel
            externalCellDelegate: outputCell
            externalCellOptionsProvider: function (row, column, data) {
                return {
                    row: row,
                    column: column,
                    display: data.display === undefined ? "" : String(data.display),
                    portId: data.portId || 0,
                    name: data.name || "",
                    type: data.typeDisplay || data.type || "",
                    typeMachine: data.type || "",
                    tooltip: data.tooltip || "",
                    count: data.count || 0,
                    relations: data.relations || [],
                    connectable: data.connectable === true,
                    system: data.system === true
                };
            }
            columnSource: [
                {
                    title: "参数名称",
                    dataIndex: "name",
                    readOnly: true
                },
                {
                    title: "类型",
                    dataIndex: "type",
                    readOnly: true
                },
                {
                    title: "数据",
                    dataIndex: "preview",
                    readOnly: true
                },
                {
                    title: "关系",
                    dataIndex: "relations",
                    readOnly: true
                }
            ]
            view.columnWidthProvider: root.columnWidthFor
            rowHeightProvider: function (row) {
                if (!root.relations || !app.outputModel)
                    return VisionRTSize.ContentSize;
                let item = app.outputModel.get(row);
                let rels = item && item.relations ? item.relations : [];
                return Math.max(1, rels.length) * VisionRTSize.ContentSize;
            }
            resizableRows: false
            resizableColumns: false
            rowHeight: VisionRTSize.ContentSize
            headerHeight: VisionRTSize.ContentSize
            headerColor: QuiColor.Primary
            horizontalHeader.delegate: Rectangle {
                id: headerCell
                required property int column
                implicitWidth: root.columnWidthFor(column)
                implicitHeight: table.headerHeight
                color: table.headerColor
                QuiText {
                    anchors.fill: parent
                    text: table.columnSource[headerCell.column].title
                    color: table.headerTextColor
                    horizontalAlignment: Text.AlignHCenter
                    verticalAlignment: Text.AlignVCenter
                    elide: Text.ElideRight
                }
                Rectangle {
                    anchors.left: parent.left
                    height: parent.height
                    width: 1
                    color: table.borderColor
                }
                Rectangle {
                    anchors.right: parent.right
                    height: parent.height
                    width: 1
                    color: table.borderColor
                }
                Rectangle {
                    anchors.bottom: parent.bottom
                    width: parent.width
                    height: 1
                    color: table.borderColor
                }
            }
            showGridLines: true
            border.width: 0
            rowColor: QuiColor.Primary
            zebraColor: QuiColor.Background
            selectedColor: QuiColor.ItemHover
            selectedBorderColor: QuiColor.Highlight
            hoverColor: QuiColor.ItemHover
        }
    }
    onRelationsChanged: table.forceLayout()
    Component {
        id: outputCell
        Item {
            id: cell
            property var options: ({})
            MouseArea {
                id: cellPointer
                anchors.fill: parent
                hoverEnabled: true
                acceptedButtons: Qt.RightButton
                onClicked: function (mouse) {
                    // Column 0 is the connection menu; the data column only ever offers copy.
                    if (cell.options.column === 0)
                        root.showNameMenu(cell.options, cell, Qt.point(mouse.x, mouse.y));
                    else if (cell.options.column === 2)
                        root.showDataMenu(cell.options, cell, Qt.point(mouse.x, mouse.y));
                }
            }
            RowLayout {
                anchors.fill: parent
                anchors.leftMargin: 5
                anchors.rightMargin: 5
                spacing: 4
                visible: cell.options.column === 0
                QuiText {
                    Layout.fillWidth: true
                    Layout.fillHeight: true
                    verticalAlignment: Text.AlignVCenter
                    elide: Text.ElideRight
                    font: QuiFont.Body
                    // Only a port with a legal, still free downstream input looks actionable.
                    color: cell.options.connectable ? QuiColor.Highlight : QuiColor.FontPrimary
                    text: cell.options.name || cell.options.display || ""
                }
                QuiText {
                    visible: !cell.options.system && root.relations && (cell.options.count || 0) > 0
                    Layout.fillHeight: true
                    verticalAlignment: Text.AlignVCenter
                    font: QuiFont.Body
                    color: QuiColor.Highlight
                    text: "(" + (cell.options.count || 0) + ")"
                }
            }
            QuiText {
                anchors.fill: parent
                anchors.leftMargin: 5
                anchors.rightMargin: 5
                visible: cell.options.column !== 0 && cell.options.column !== 3
                verticalAlignment: Text.AlignVCenter
                elide: Text.ElideRight
                font: QuiFont.Body
                color: QuiColor.FontPrimary
                text: cell.options.display || ""
            }
            Column {
                anchors.fill: parent
                visible: cell.options.column === 3
                Repeater {
                    model: cell.options.relations || []
                    delegate: Item {
                        required property var modelData
                        required property int index
                        width: parent ? parent.width : 0
                        height: VisionRTSize.ContentSize
                        RowLayout {
                            anchors.fill: parent
                            anchors.leftMargin: 5
                            anchors.rightMargin: 5
                            spacing: 4
                            QuiText {
                                Layout.fillWidth: true
                                Layout.fillHeight: true
                                verticalAlignment: Text.AlignVCenter
                                elide: Text.ElideRight
                                font: QuiFont.Body
                                color: QuiColor.FontPrimary
                                text: modelData.label || ""
                            }
                            WorkbenchActionButton {
                                objectName: "disconnectRelationButton_" + cell.options.portId + "_" + modelData.node + "_" + modelData.port
                                Layout.preferredWidth: VisionRTSize.SmallIconButtonSize
                                Layout.preferredHeight: VisionRTSize.SmallIconButtonSize
                                Layout.alignment: Qt.AlignVCenter
                                iconSize: VisionRTSize.SmallIconSize
                                normalColor: QuiColor.Transparent
                                hoverColor: QuiColor.Transparent
                                pressedColor: QuiColor.Transparent
                                iconColor: !enabled ? QuiColor.FontDark : hovered ? QuiColor.Highlight : QuiColor.FontCaption
                                text: "断开连接"
                                iconSource: QuiFontIcon.ChromeClose
                                enabled: app.workspaceEnabled
                                onClicked: {
                                    let ctx = app.portDeclarationContext(cell.options.portId, true);
                                    if (app.portContextCurrent(ctx))
                                        app.unbindOutputFrom(ctx, modelData.node, modelData.port);
                                }
                            }
                        }
                        Rectangle {
                            anchors.left: parent.left
                            anchors.right: parent.right
                            anchors.bottom: parent.bottom
                            height: 1
                            color: table.borderColor
                            visible: index < ((cell.options.relations && cell.options.relations.length) ? cell.options.relations.length : 0) - 1
                        }
                    }
                }
            }
            QuiToolTip {
                visible: cellPointer.containsMouse && text.length > 0
                text: {
                    if (cell.options.column === 0) {
                        let n = cell.options.name || String(cell.options.portId);
                        let t = cell.options.type || "";
                        return t.length > 0 ? (n + "（" + t + "）") : n;
                    }
                    if (cell.options.column === 2)
                        return cell.options.tooltip || cell.options.display || "";
                    if (cell.options.column === 1)
                        return cell.options.type || cell.options.display || "";
                    if (cell.options.column === 3) {
                        let rels = cell.options.relations || [];
                        return rels.map(function (r) { return r.label || ""; }).filter(function (s) { return s.length > 0; }).join("\n");
                    }
                    return cell.options.display || "";
                }
                delay: 500
            }
        }
    }
    QuiMenu {
        id: copyMenu
        objectName: "outputMenu"
        parent: root
        onClosed: {
            const pending = root.pendingContext;
            root.pendingContext = ({});
            root.selectedContext = ({});
            if (root.isEmptyContext(pending) || !app.portContextCurrent(pending))
                return;
            // Copy stays a bounded background task with a readback acknowledgement; the GUI thread
            // never serialises the value and never truncates it into something that looks complete.
            app.requestOutputText(pending);
        }
        QuiMenuItem {
            objectName: "outputCopyAction"
            text: "复制"
            enabled: root.copyEnabled()
            onTriggered: root.pendingContext = root.selectedContext
        }
    }
    QuiMenu {
        id: nameMenu
        objectName: "outputNameMenu"
        parent: root
        property string search: ""
        property var filteredTools: {
            if (!search || search.trim() === "")
                return root.cascadeTools;
            let q = search.trim().toLowerCase();
            let res = [];
            for (let tool of root.cascadeTools) {
                let toolMatch = (tool.title || "").toLowerCase().indexOf(q) >= 0;
                let matchedInputs = [];
                for (let input of tool.inputs) {
                    let inputName = (input.title || input.name || "").toLowerCase();
                    let portName = String(input.port);
                    if (toolMatch || inputName.indexOf(q) >= 0 || portName.indexOf(q) >= 0) {
                        matchedInputs.push(input);
                    }
                }
                if (matchedInputs.length > 0) {
                    res.push({
                        node: tool.node,
                        title: tool.title,
                        inputs: matchedInputs
                    });
                }
            }
            return res;
        }
        onAboutToShow: search = ""
        onClosed: search = ""
        Item {
            implicitWidth: 180
            implicitHeight: VisionRTSize.ContentSize
            WorkbenchTextField {
                id: nameSearchField
                anchors.fill: parent
                anchors.margins: 5
                placeholderText: "搜索工具或输入..."
                text: nameMenu.search
                onTextChanged: nameMenu.search = text
            }
        }
        Instantiator {
            model: nameMenu.filteredTools
            delegate: QuiMenu {
                id: toolSubMenu
                required property var modelData
                property string targetNode: modelData.node
                title: modelData.title
                Instantiator {
                    model: toolSubMenu.modelData.inputs
                    delegate: QuiMenuItem {
                        required property var modelData
                        text: (modelData.title || modelData.name) + " (" + (modelData.typeText || modelData.type) + ")"
                        enabled: app.workspaceEnabled
                        onTriggered: app.bindOutputTo(root.selectedContext, toolSubMenu.targetNode, modelData.port)
                    }
                    onObjectAdded: function (index, object) {
                        toolSubMenu.insertItem(index, object);
                    }
                    onObjectRemoved: function (index, object) {
                        toolSubMenu.removeItem(object);
                    }
                }
            }
            onObjectAdded: function (index, object) {
                nameMenu.insertMenu(index + 1, object);
            }
            onObjectRemoved: function (index, object) {
                nameMenu.removeMenu(object);
            }
        }
    }
    Connections {
        target: app
        function onSelectionChanged() {
            root.validateMenu();
        }
        function onNodesChanged() {
            root.validateMenu();
        }
        function onParametersChanged() {
            root.validateMenu();
        }
        function onRuntimeChanged() {
            root.validateMenu();
        }
        function onResultsChanged() {
            root.validateMenu();
        }
        function onProjectReplaced() {
            root.invalidateMenu();
        }
    }
    Connections {
        target: app.outputModel
        function onModelAboutToBeReset() {
            root.invalidateMenu();
        }
        function onModelReset() {
            Qt.callLater(table.forceLayout);
        }
        function onRowsInserted() {
            Qt.callLater(table.forceLayout);
        }
        function onRowsRemoved() {
            Qt.callLater(table.forceLayout);
        }
        function onLayoutChanged() {
            Qt.callLater(table.forceLayout);
        }
    }
    Connections {
        target: root.Window.window
        function onActiveChanged() {
            if (target && !target.active)
                root.invalidateMenu();
        }
    }
}
