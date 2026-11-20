import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import quickui
import VisionRT.Ui 1.0

FocusScope {
    id: root
    property var selection: []
    property real zoom: 1
    property real panX: 0
    property real panY: 0
    onZoomChanged: root.hideHover()
    onPanXChanged: root.hideHover()
    onPanYChanged: root.hideHover()
    property var flowViews: ({})
    property bool fitRequested: true
    property string viewedFlow: ""
    property real dragX: 0
    property real dragY: 0
    property point pressPoint: Qt.point(0, 0)
    property point lastPoint: Qt.point(0, 0)
    enum Gesture {
        None,
        Pan,
        Nodes,
        Select
    }
    enum NameTarget {
        Node,
        Flow
    }
    property int gesture: FlowCanvas.Gesture.None
    property bool moved: false
    property string contextNode: ""
    property string contextFlow: ""
    property point contextPoint: Qt.point(0, 0)
    property int nameOperation: FlowCanvas.NameTarget.Node
    property string nameDraft: ""
    property string hoverNodeId: ""
    property string selectedWire: ""
    readonly property var selectedConnection: {
        for (const wire of routedWires)
            if (wire.source + ":" + wire.target === selectedWire)
                return wire;
        return null;
    }
    property bool hoverCardVisible: false
    // Every model row notification bumps this, so a hover card never keeps a copy taken before the
    // current execution snapshot arrived.
    property int recordRevision: 0
    readonly property var hoverNode: {
        root.recordRevision;
        return hoverNodeId === "" ? null : app.nodeRecord(hoverNodeId);
    }
    readonly property int cellWidth: 180
    readonly property int cellHeight: 80
    property var routedWires: []
    readonly property var routeGeometry: {
        if (app.wires.length === 0)
            return [];
        const geometry = [];
        for (let index = 0; index < nodeRepeater.count; ++index) {
            const item = nodeRepeater.itemAt(index);
            if (!item)
                return [];
            geometry.push({
                id: item.nodeId,
                rectangle: Qt.rect(item.x, item.y, item.width, item.height)
            });
        }
        return geometry;
    }

    onRouteGeometryChanged: Qt.callLater(root.updateRoutes)
    function updateRoutes() {
        routedWires = app.routeWires(routeGeometry);
    }

    function selected(id) {
        return selection.indexOf(id) !== -1;
    }
    function enterNode(id) {
        if (root.gesture !== FlowCanvas.Gesture.None)
            return;
        if (root.hoverNodeId === id && (root.hoverCardVisible || hoverTimer.running))
            return;
        root.hoverNodeId = id;
        root.hoverCardVisible = false;
        hoverTimer.restart();
    }
    function leaveNode(id) {
        if (root.hoverNodeId === id)
            root.hideHover();
    }
    function hideHover() {
        hoverTimer.stop();
        root.hoverNodeId = "";
        root.hoverCardVisible = false;
    }
    function hoverCardX() {
        let n = root.hoverNode;
        if (!n)
            return 4;
        let right = root.nodeX(n.id) * root.zoom + root.panX + VisionRTSize.NodeWidth * root.zoom + 10;
        let left = root.nodeX(n.id) * root.zoom + root.panX - hoverCard.width - 10;
        let maxX = Math.max(4, surface.width - hoverCard.width - 4);
        return Math.max(4, Math.min(maxX, right <= maxX ? right : left));
    }
    function hoverCardY() {
        let n = root.hoverNode;
        if (!n)
            return 4;
        let maxY = Math.max(4, surface.height - hoverCard.height - 4);
        return Math.max(4, Math.min(maxY, root.nodeY(n.id) * root.zoom + root.panY));
    }
    // One id-indexed lookup in the node model; no scan over a copy of the whole table.
    function nodeX(id) {
        const p = app.nodePosition(id);
        return p.x * cellWidth + (selected(id) ? dragX : 0);
    }
    function nodeY(id) {
        const p = app.nodePosition(id);
        return p.y * cellHeight + (selected(id) ? dragY : 0);
    }
    function focusSearch() {
        flowToolbar.focusSearch();
    }
    function centerNode(id) {
        const p = app.nodePosition(id);
        if (p.x === 0 && p.y === 0 && !app.nodeRecord(id).id)
            return;
        selection = [id];
        panX = surface.width / 2 - (p.x * cellWidth + VisionRTSize.NodeWidth / 2) * zoom;
        panY = surface.height / 2 - (p.y * cellHeight + VisionRTSize.NodeHeight / 2) * zoom;
        surface.forceActiveFocus();
    }
    function fit() {
        if (surface.width <= 0 || surface.height <= 0)
            return;
        fitRequested = false;
        const positions = app.nodePositions();
        if (!positions.length) {
            zoom = 1;
            panX = surface.width / 2;
            panY = surface.height / 2;
            return;
        }
        let minX = Infinity, minY = Infinity, maxX = -Infinity, maxY = -Infinity;
        for (let n of positions) {
            minX = Math.min(minX, n.x * cellWidth);
            minY = Math.min(minY, n.y * cellHeight);
            maxX = Math.max(maxX, n.x * cellWidth + VisionRTSize.NodeWidth);
            maxY = Math.max(maxY, n.y * cellHeight + VisionRTSize.NodeHeight);
        }
        zoom = Math.min(1.5, Math.max(1, surface.width - 64) / Math.max(1, maxX - minX), Math.max(1, surface.height - 64) / Math.max(1, maxY - minY));
        panX = surface.width / 2 - (minX + maxX) * zoom / 2;
        panY = surface.height / 2 - (minY + maxY) * zoom / 2;
    }
    function zoomAt(factor, px, py) {
        let next = Math.max(0.15, Math.min(3, zoom * factor));
        panX = px - (px - panX) * next / zoom;
        panY = py - (py - panY) * next / zoom;
        zoom = next;
    }
    function beginNode(mouse, item) {
        if (!app.workspaceEnabled)
            return;
        root.hideHover();
        root.selectedWire = "";
        surface.forceActiveFocus();
        let p = item.mapToItem(surface, mouse.x, mouse.y);
        pressPoint = p;
        lastPoint = p;
        moved = false;
        if (mouse.button === Qt.MiddleButton || mouse.button === Qt.RightButton) {
            gesture = FlowCanvas.Gesture.Pan;
            contextNode = item.nodeId;
            return;
        }
        if (mouse.modifiers & (Qt.ControlModifier | Qt.ShiftModifier)) {
            let ids = selection.slice(), at = ids.indexOf(item.nodeId);
            if (at >= 0)
                ids.splice(at, 1);
            else
                ids.push(item.nodeId);
            selection = ids;
        } else if (!selected(item.nodeId))
            selection = [item.nodeId];
        app.selectNode(item.nodeId);
        gesture = app.workspaceEnabled && selected(item.nodeId) ? FlowCanvas.Gesture.Nodes : FlowCanvas.Gesture.None;
    }
    function moveNode(mouse, item) {
        updateGesture(item.mapToItem(surface, mouse.x, mouse.y));
    }
    function updateGesture(p) {
        if (Math.abs(p.x - pressPoint.x) + Math.abs(p.y - pressPoint.y) > Qt.styleHints.startDragDistance)
            moved = true;
        if (gesture === FlowCanvas.Gesture.Pan && moved) {
            panX += p.x - lastPoint.x;
            panY += p.y - lastPoint.y;
        }
        if (gesture === FlowCanvas.Gesture.Nodes && moved) {
            dragX = Math.round((p.x - pressPoint.x) / zoom / cellWidth) * cellWidth;
            dragY = Math.round((p.y - pressPoint.y) / zoom / cellHeight) * cellHeight;
        }
        lastPoint = p;
    }
    function endNode(mouse, item) {
        if (!app.workspaceEnabled) {
            gesture = FlowCanvas.Gesture.None;
            dragX = 0;
            dragY = 0;
            return;
        }
        if (gesture === FlowCanvas.Gesture.Nodes && moved && (dragX || dragY))
            app.moveNodes(selection, Math.round(dragX / cellWidth), Math.round(dragY / cellHeight));
        if (mouse.button === Qt.RightButton && !moved) {
            if (!selected(item.nodeId))
                selection = [item.nodeId];
            app.selectNode(item.nodeId);
            contextNode = item.nodeId;
            nodeMenu.popup();
        }
        gesture = FlowCanvas.Gesture.None;
        dragX = 0;
        dragY = 0;
    }
    function openName(operation, value) {
        nameOperation = operation;
        nameDraft = value || "";
        nameDialog.open();
    }
    function gridPointAt(x, y) {
        return {
            x: Math.round((x - panX) / zoom / cellWidth),
            y: Math.round((y - panY) / zoom / cellHeight)
        };
    }
    function rememberView() {
        if (!viewedFlow)
            return;
        let views = flowViews;
        views[viewedFlow] = {
            zoom: zoom,
            centerX: (surface.width / 2 - panX) / zoom,
            centerY: (surface.height / 2 - panY) / zoom,
            selection: selection
        };
        flowViews = views;
    }
    Component.onCompleted: {
        viewedFlow = app.currentFlow;
        fitTimer.restart();
    }
    Timer {
        id: fitTimer
        interval: 1
        onTriggered: root.fit()
    }
    Connections {
        target: app
        function onSelectionChanged() {
            root.hideHover();
            if (app.selectedNode || root.viewedFlow !== app.currentFlow)
                root.selectedWire = "";
            if (root.viewedFlow === app.currentFlow) {
                if (app.selectedNode && !root.selected(app.selectedNode))
                    root.selection = [app.selectedNode];
                return;
            }
            root.rememberView();
            root.viewedFlow = app.currentFlow;
            let view = root.flowViews[app.currentFlow];
            root.zoom = view ? view.zoom : 1;
            root.panX = surface.width / 2 - (view ? view.centerX * root.zoom : 0);
            root.panY = surface.height / 2 - (view ? view.centerY * root.zoom : 0);
            root.selection = view ? view.selection : [];
        }
        function onNodeLocated(id) {
            root.centerNode(id);
        }
        function onProjectReplaced() {
            root.hideHover();
            root.selectedWire = "";
            root.flowViews = ({});
            root.selection = [];
            root.viewedFlow = app.currentFlow;
            root.fitRequested = true;
            fitTimer.restart();
        }
        function onNodesChanged() {
            if (!root.selectedConnection)
                root.selectedWire = "";
            root.selection = root.selection.filter(function (id) {
                return !!app.nodeRecord(id).id;
            });
            if (root.hoverNodeId !== "" && !app.nodeRecord(root.hoverNodeId).id)
                root.hideHover();
        }
        function onCatalogChanged() {
            root.hideHover();
        }
    }
    Connections {
        target: app.nodeListModel
        function onDataChanged() {
            root.recordRevision++;
        }
        function onRowsInserted() {
            root.recordRevision++;
        }
        function onRowsRemoved() {
            root.recordRevision++;
        }
        function onRowsMoved() {
            root.recordRevision++;
        }
        function onModelReset() {
            root.recordRevision++;
        }
    }
    Connections {
        target: root.Window.window
        function onActiveChanged() {
            if (root.Window.window && !root.Window.window.active)
                root.hideHover();
        }
    }
    Timer {
        id: hoverTimer
        interval: 500
        repeat: false
        onTriggered: {
            if (root.hoverNodeId !== "" && root.hoverNode !== null && root.gesture === FlowCanvas.Gesture.None)
                root.hoverCardVisible = true;
        }
    }
    Rectangle {
        anchors.fill: parent
        color: QuiColor.Primary
    }
    ColumnLayout {
        anchors.fill: parent
        spacing: 0
        FlowToolbar {
            id: flowToolbar
            Layout.fillWidth: true
            z: 20
            flows: app.flows
            searchResults: app.searchResults
            workspaceEnabled: app.workspaceEnabled
            onSearchRequested: function (text) {
                app.searchNodes(text);
            }
            onLocateRequested: function (result) {
                app.locateNode(result.flow, result.node);
                surface.forceActiveFocus();
            }
            onFitRequested: root.fit()
            onFlowSelectionRequested: function (id, selected) {
                app.setFlowSelected(id, selected);
            }
            onCanvasFocusRequested: surface.forceActiveFocus()
        }
        FlowTabs {
            Layout.fillWidth: true
            flows: app.flows
            currentFlow: app.currentFlow
            workspaceEnabled: app.workspaceEnabled
            onAddRequested: {
                app.addFlow("");
                surface.forceActiveFocus();
            }
            onSelectRequested: function (id) {
                app.selectFlow(id);
            }
            onContextRequested: function (id) {
                root.contextFlow = id;
                flowMenu.popup();
            }
            onDeleteRequested: function (id) {
                root.contextFlow = id;
                deleteFlowDialog.open();
            }
        }
        FocusScope {
            id: surface
            objectName: "flowSurface"
            Layout.fillWidth: true
            Layout.fillHeight: true
            clip: true
            property real previousWidth: 0
            property real previousHeight: 0
            onWidthChanged: {
                root.panX += (width - previousWidth) / 2;
                previousWidth = width;
                if (root.fitRequested)
                    fitTimer.restart();
            }
            onHeightChanged: {
                root.panY += (height - previousHeight) / 2;
                previousHeight = height;
                if (root.fitRequested)
                    fitTimer.restart();
            }
            Keys.onPressed: function (event) {
                if (!surface.activeFocus || !app.workspaceEnabled)
                    return;
                if (event.matches(StandardKey.Copy)) {
                    app.copyNodes(root.selection);
                    event.accepted = true;
                } else if (event.matches(StandardKey.Paste)) {
                    app.pasteNodes(Math.round((width / 2 - root.panX) / root.zoom / root.cellWidth), Math.round((height / 2 - root.panY) / root.zoom / root.cellHeight));
                    event.accepted = true;
                } else if (event.key === Qt.Key_Delete || event.key === Qt.Key_Backspace) {
                    app.deleteNodes(root.selection);
                    root.selection = [];
                    event.accepted = true;
                } else if (event.key === Qt.Key_Escape) {
                    root.selection = [];
                    root.selectedWire = "";
                    root.gesture = FlowCanvas.Gesture.None;
                    root.dragX = 0;
                    root.dragY = 0;
                    event.accepted = true;
                }
            }
            Rectangle {
                anchors.fill: parent
                color: QuiColor.Primary
            }
            MouseArea {
                id: backgroundMouse
                anchors.fill: parent
                acceptedButtons: Qt.LeftButton | Qt.RightButton | Qt.MiddleButton
                preventStealing: true
                enabled: app.workspaceEnabled
                onPressed: function (mouse) {
                    surface.forceActiveFocus();
                    root.hideHover();
                    root.selectedWire = "";
                    root.pressPoint = Qt.point(mouse.x, mouse.y);
                    root.lastPoint = root.pressPoint;
                    root.moved = false;
                    root.gesture = mouse.button === Qt.LeftButton ? FlowCanvas.Gesture.Select : FlowCanvas.Gesture.Pan;
                    if (mouse.button === Qt.LeftButton && !(mouse.modifiers & (Qt.ControlModifier | Qt.ShiftModifier))) {
                        root.selection = [];
                        app.selectNode("");
                    }
                }
                onPositionChanged: function (mouse) {
                    if (pressed)
                        root.updateGesture(Qt.point(mouse.x, mouse.y));
                }
                onReleased: function (mouse) {
                    if (root.gesture === FlowCanvas.Gesture.Select && root.moved) {
                        let left = (Math.min(root.pressPoint.x, mouse.x) - root.panX) / root.zoom;
                        let top = (Math.min(root.pressPoint.y, mouse.y) - root.panY) / root.zoom;
                        let right = (Math.max(root.pressPoint.x, mouse.x) - root.panX) / root.zoom;
                        let bottom = (Math.max(root.pressPoint.y, mouse.y) - root.panY) / root.zoom;
                        let ids = root.selection.slice();
                        for (let n of app.nodePositions())
                            if (n.x * root.cellWidth + VisionRTSize.NodeWidth >= left && n.x * root.cellWidth <= right && n.y * root.cellHeight + VisionRTSize.NodeHeight >= top && n.y * root.cellHeight <= bottom && ids.indexOf(n.id) < 0)
                                ids.push(n.id);
                        root.selection = ids;
                        if (ids.length)
                            app.selectNode(ids[ids.length - 1]);
                    } else if (mouse.button === Qt.RightButton && !root.moved) {
                        root.contextPoint = Qt.point(mouse.x, mouse.y);
                        blankMenu.popup();
                    }
                    root.gesture = FlowCanvas.Gesture.None;
                }
                onDoubleClicked: function (mouse) {
                    if (mouse.button === Qt.LeftButton)
                        root.fit();
                }
            }
            WheelHandler {
                target: null
                enabled: app.workspaceEnabled
                onWheel: function (event) {
                    root.zoomAt(Math.pow(1.0015, event.angleDelta.y), event.x, event.y);
                    event.accepted = true;
                }
            }
            Item {
                id: world
                x: root.panX
                y: root.panY
                scale: root.zoom
                transformOrigin: Item.TopLeft
                Repeater {
                    model: root.routedWires
                    WireItem {
                        required property var modelData
                        objectName: "flowWire_" + modelData.source + "_" + modelData.target
                        route: modelData.points
                        viewScale: root.zoom
                        connectionCount: modelData.count
                        lineColor: modelData.error ? QuiColor.Error : root.selectedWire === modelData.source + ":" + modelData.target ? VisionRTColor.NodeOutputLine : root.selected(modelData.target) ? VisionRTColor.NodeInputLine : root.selected(modelData.source) ? VisionRTColor.NodeOutputLine : VisionRTColor.NodeLine
                        opacity: root.selection.length && !root.selected(modelData.target) && !root.selected(modelData.source) ? 0.35 : 1
                        onActivated: {
                            root.hideHover();
                            surface.forceActiveFocus();
                            root.selection = [];
                            app.selectNode("");
                            root.selectedWire = modelData.source + ":" + modelData.target;
                        }
                    }
                }
                Repeater {
                    id: nodeRepeater
                    // Roles are the single source for the delegate: no per-frame map is rebuilt.
                    model: app.nodeListModel
                    NodeDelegate {
                        selected: root.selected(nodeId)
                        viewScale: root.zoom
                        x: root.nodeX(nodeId)
                        y: root.nodeY(nodeId)
                        onPointerPressed: function (mouse, item) {
                            root.beginNode(mouse, item);
                        }
                        onPointerMoved: function (mouse, item) {
                            root.moveNode(mouse, item);
                        }
                        onPointerReleased: function (mouse, item) {
                            root.endNode(mouse, item);
                        }
                        onExecuteRequested: {
                            app.selectNode(nodeId);
                            app.runNode();
                        }
                        onHoverEntered: root.enterNode(nodeId)
                        onHoverExited: root.leaveNode(nodeId)
                    }
                }
            }
            Rectangle {
                id: wireRelations
                objectName: "wireRelations"
                visible: root.selectedConnection !== null
                anchors.left: parent.left
                anchors.bottom: parent.bottom
                anchors.margins: 5
                width: Math.min(420, parent.width - 16)
                height: relationsColumn.implicitHeight + 16
                color: QuiColor.Primary
                border.color: QuiColor.Border
                radius: 4
                ColumnLayout {
                    id: relationsColumn
                    anchors.fill: parent
                    anchors.margins: 5
                    spacing: 5
                    QuiText {
                        Layout.fillWidth: true
                        font: QuiFont.Caption
                        elide: Text.ElideRight
                        text: {
                            const wire = root.selectedConnection;
                            if (!wire)
                                return "";
                            const source = app.nodeRecord(wire.source);
                            const target = app.nodeRecord(wire.target);
                            return (source ? source.title : wire.source) + " → " + (target ? target.title : wire.target);
                        }
                    }
                    Repeater {
                        model: root.selectedConnection ? root.selectedConnection.ports : []
                        QuiText {
                            required property var modelData
                            Layout.fillWidth: true
                            font: QuiFont.Caption
                            elide: Text.ElideRight
                            text: modelData.output + " → " + modelData.input
                        }
                    }
                }
            }
            Rectangle {
                id: hoverCard
                objectName: "nodeHoverCard"
                visible: root.hoverCardVisible && root.hoverNode !== null
                z: 30
                width: VisionRTSize.NodeTooltipWidth
                height: hoverColumn.implicitHeight + 16
                x: root.hoverCardX()
                y: root.hoverCardY()
                radius: 4
                color: QuiColor.ToolTip
                border.color: QuiColor.Border
                border.width: 1
                enabled: false
                ColumnLayout {
                    id: hoverColumn
                    anchors.fill: parent
                    anchors.margins: 5
                    spacing: 5
                    QuiText {
                        Layout.fillWidth: true
                        text: root.hoverNode ? root.hoverNode.title : ""
                        wrapMode: Text.Wrap
                        font: QuiFont.Body
                        color: QuiColor.FontPrimary
                    }
                    Repeater {
                        model: root.hoverNode ? root.hoverNode.timingPhases : []
                        RowLayout {
                            required property var modelData
                            Layout.fillWidth: true
                            QuiText {
                                text: modelData.label
                                font: QuiFont.Body
                                color: QuiColor.FontCaption
                            }
                            Item {
                                Layout.fillWidth: true
                            }
                            QuiText {
                                text: modelData.value
                                font: QuiFont.Body
                                color: QuiColor.FontPrimary
                            }
                        }
                    }
                }
            }
            Rectangle {
                visible: root.gesture === FlowCanvas.Gesture.Select && root.moved
                x: Math.min(root.pressPoint.x, root.lastPoint.x)
                y: Math.min(root.pressPoint.y, root.lastPoint.y)
                width: Math.abs(root.lastPoint.x - root.pressPoint.x)
                height: Math.abs(root.lastPoint.y - root.pressPoint.y)
                color: QuiColor.ItemCheck
                border.color: QuiColor.Highlight
            }
            DropArea {
                anchors.fill: parent
                keys: ["visionrt/tool"]
                onEntered: function (drag) {
                    drag.accepted = app.workspaceEnabled;
                }
                onDropped: function (drop) {
                    if (!app.workspaceEnabled || !drop.source || !drop.source.toolId)
                        return;
                    app.addNode(drop.source.toolId, Math.round((drop.x - root.panX) / root.zoom / root.cellWidth), Math.round((drop.y - root.panY) / root.zoom / root.cellHeight));
                    drop.acceptProposedAction();
                    surface.forceActiveFocus();
                }
            }
        }
    }

    QuiMenu {
        id: nodeMenu
        QuiMenuItem {
            text: "复制"
            enabled: app.workspaceEnabled
            onTriggered: app.copyNodes(root.selection)
        }
        QuiMenuItem {
            text: "粘贴"
            enabled: app.canPaste && app.workspaceEnabled
            onTriggered: {
                const p = app.nodePosition(root.contextNode);
                if (app.nodeRecord(root.contextNode).id)
                    app.pasteNodes(p.x, p.y);
            }
        }
        QuiMenuItem {
            text: "删除"
            enabled: app.workspaceEnabled
            onTriggered: {
                app.deleteNodes(root.selection);
                root.selection = [];
            }
        }
        QuiMenuItem {
            text: "重命名备注…"
            enabled: app.workspaceEnabled
            onTriggered: root.openName(FlowCanvas.NameTarget.Node, app.nodeRecord(root.contextNode).alias || "")
        }
    }
    QuiMenu {
        id: blankMenu
        objectName: "flowBlankMenu"
        QuiMenuItem {
            objectName: "flowBlankPaste"
            text: "粘贴"
            enabled: app.canPaste && app.workspaceEnabled
            onTriggered: {
                let p = root.gridPointAt(root.contextPoint.x, root.contextPoint.y);
                app.pasteNodes(p.x, p.y);
            }
        }
    }
    QuiMenu {
        id: flowMenu
        objectName: "flowContextMenu"
        QuiMenuItem {
            text: "重命名流程…"
            enabled: app.workspaceEnabled
            onTriggered: {
                for (let f of app.flows)
                    if (f.id === root.contextFlow)
                        root.openName(FlowCanvas.NameTarget.Flow, f.name);
            }
        }
        QuiMenuItem {
            text: "复制流程"
            enabled: app.workspaceEnabled
            onTriggered: app.copyFlow(root.contextFlow)
        }
        QuiMenuItem {
            text: "删除流程…"
            enabled: app.workspaceEnabled && app.flows.length > 1
            onTriggered: deleteFlowDialog.open()
        }
    }
    WorkbenchDialog {
        id: deleteFlowDialog
        objectName: "flowDeleteDialog"
        title: "删除流程"
        message: "删除该流程及全部节点？此操作无法撤销。"
        onPositiveClicked: app.deleteFlow(root.contextFlow)
    }
    WorkbenchDialog {
        id: nameDialog
        objectName: "flowNameDialog"
        title: root.nameOperation === FlowCanvas.NameTarget.Node ? "节点备注" : "流程名称"
        positiveButtonEnabled: app.workspaceEnabled && root.nameDraft.trim().length > 0
        contentDelegate: Component {
            WorkbenchTextField {
                text: root.nameDraft
                implicitHeight: 44
                onTextEdited: root.nameDraft = text
                Component.onCompleted: {
                    forceActiveFocus();
                    selectAll();
                }
            }
        }
        onPositiveClicked: {
            if (root.nameOperation === FlowCanvas.NameTarget.Node)
                app.renameNode(root.contextNode, root.nameDraft);
            else
                app.renameFlow(root.contextFlow, root.nameDraft);
        }
    }
}
