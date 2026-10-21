import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import quickui

FocusScope {
    id: root
    property var selection: []
    property var nodeIds: []
    property real zoom: 1
    property real panX: 0
    property real panY: 0
    property var flowViews: ({})
    property bool fitRequested: true
    property string viewedFlow: ""
    property real dragX: 0
    property real dragY: 0
    property point pressPoint: Qt.point(0, 0)
    property point lastPoint: Qt.point(0, 0)
    property string gesture: ""
    property bool moved: false
    property string contextNode: ""
    property string contextFlow: ""
    property string nameOperation: ""
    property string nameDraft: ""
    property int searchIndex: 0
    property string hoverNodeId: ""
    property string selectedWire: ""
    readonly property var selectedConnection: {
        for (const wire of routedWires)
            if (wire.source + ":" + wire.target === selectedWire) return wire
        return null
    }
    property bool hoverCardVisible: false
    readonly property var hoverNode: hoverNodeId === "" ? null : findNode(hoverNodeId)
    readonly property int cellWidth: 180
    readonly property int cellHeight: 80
    readonly property var routedWires: {
        if (app.wires.length === 0) return []
        const geometry = []
        for (let index = 0; index < nodeRepeater.count; ++index) {
            const loader = nodeRepeater.itemAt(index)
            if (!loader || !loader.item) return []
            const item = loader.item
            geometry.push({id: loader.modelData, rectangle: Qt.rect(item.x, item.y, item.width, item.height)})
        }
        return app.routeWires(geometry)
    }

    function selected(id) { return selection.indexOf(id) !== -1 }
    function findNode(id) { for (let n of app.nodes) if (n.id === id) return n; return null }
    function refreshNodeIds() {
        const ids = app.nodes.map(function(node) { return node.id })
        if (ids.length !== nodeIds.length || ids.some(function(v, i) { return v !== nodeIds[i] })) nodeIds = ids
    }
    function enterNode(id) {
        if (root.gesture !== "" || root.hoverNodeId === id) return
        root.hoverNodeId = id
        root.hoverCardVisible = false
        hoverTimer.restart()
    }
    function leaveNode(id) {
        if (root.hoverNodeId === id) root.hideHover()
    }
    function hideHover() {
        hoverTimer.stop()
        root.hoverNodeId = ""
        root.hoverCardVisible = false
    }
    function hoverCardX() {
        let n = root.hoverNode
        if (!n) return 4
        let right = root.nodeX(n.id) * root.zoom + root.panX + 156 * root.zoom + 10
        let left = root.nodeX(n.id) * root.zoom + root.panX - hoverCard.width - 10
        let maxX = Math.max(4, surface.width - hoverCard.width - 4)
        return Math.max(4, Math.min(maxX, right <= maxX ? right : left))
    }
    function hoverCardY() {
        let n = root.hoverNode
        if (!n) return 4
        let maxY = Math.max(4, surface.height - hoverCard.height - 4)
        return Math.max(4, Math.min(maxY, root.nodeY(n.id) * root.zoom + root.panY))
    }
    function nodeX(id) { let n = findNode(id); return n ? n.x * cellWidth + (selected(id) ? dragX : 0) : 0 }
    function nodeY(id) { let n = findNode(id); return n ? n.y * cellHeight + (selected(id) ? dragY : 0) : 0 }
    function focusSearch() { search.forceActiveFocus(); search.selectAll() }
    function locate(result) { search.text = ""; app.locateNode(result.flow, result.node); surface.forceActiveFocus() }
    function centerNode(id) {
        let n = findNode(id)
        if (!n) return
        selection = [id]
        panX = surface.width / 2 - (n.x * cellWidth + 78) * zoom
        panY = surface.height / 2 - (n.y * cellHeight + 34) * zoom
        surface.forceActiveFocus()
    }
    function fit() {
        if (surface.width <= 0 || surface.height <= 0) return
        fitRequested = false
        if (!app.nodes.length) { zoom = 1; panX = surface.width / 2; panY = surface.height / 2; return }
        let minX = Infinity, minY = Infinity, maxX = -Infinity, maxY = -Infinity
        for (let n of app.nodes) {
            minX = Math.min(minX, n.x * cellWidth); minY = Math.min(minY, n.y * cellHeight)
            maxX = Math.max(maxX, n.x * cellWidth + 156); maxY = Math.max(maxY, n.y * cellHeight + 52)
        }
        zoom = Math.min(1.5, Math.max(1, surface.width - 64) / Math.max(1, maxX - minX), Math.max(1, surface.height - 64) / Math.max(1, maxY - minY))
        panX = surface.width / 2 - (minX + maxX) * zoom / 2
        panY = surface.height / 2 - (minY + maxY) * zoom / 2
    }
    function zoomAt(factor, px, py) {
        let next = Math.max(0.15, Math.min(3, zoom * factor))
        panX = px - (px - panX) * next / zoom
        panY = py - (py - panY) * next / zoom
        zoom = next
    }
    function beginNode(mouse, item) {
        root.hideHover()
        root.selectedWire = ""
        surface.forceActiveFocus()
        let p = item.mapToItem(surface, mouse.x, mouse.y)
        pressPoint = p; lastPoint = p; moved = false
        if (mouse.button === Qt.MiddleButton || mouse.button === Qt.RightButton) {
            gesture = "pan"; contextNode = item.node.id; return
        }
        if (mouse.modifiers & (Qt.ControlModifier | Qt.ShiftModifier)) {
            let ids = selection.slice(), at = ids.indexOf(item.node.id)
            if (at >= 0) ids.splice(at, 1); else ids.push(item.node.id)
            selection = ids
        } else if (!selected(item.node.id)) selection = [item.node.id]
        app.selectNode(item.node.id)
        gesture = !app.busy && selected(item.node.id) ? "nodes" : ""
    }
    function moveNode(mouse, item) { updateGesture(item.mapToItem(surface, mouse.x, mouse.y)) }
    function updateGesture(p) {
        if (Math.abs(p.x - pressPoint.x) + Math.abs(p.y - pressPoint.y) > Qt.styleHints.startDragDistance) moved = true
        if (gesture === "pan" && moved) { panX += p.x - lastPoint.x; panY += p.y - lastPoint.y }
        if (gesture === "nodes" && moved) {
            dragX = Math.round((p.x - pressPoint.x) / zoom / cellWidth) * cellWidth
            dragY = Math.round((p.y - pressPoint.y) / zoom / cellHeight) * cellHeight
        }
        lastPoint = p
    }
    function endNode(mouse, item) {
        if (gesture === "nodes" && moved && (dragX || dragY)) app.moveNodes(selection, Math.round(dragX / cellWidth), Math.round(dragY / cellHeight))
        if (mouse.button === Qt.RightButton && !moved) {
            if (!selected(item.node.id)) selection = [item.node.id]
            app.selectNode(item.node.id); contextNode = item.node.id
            nodeMenu.popup()
        }
        gesture = ""; dragX = 0; dragY = 0
    }
    function openName(operation, value) { nameOperation = operation; nameDraft = value || ""; nameDialog.open() }
    function rememberView() {
        if (!viewedFlow) return
        let views = flowViews
        views[viewedFlow] = { zoom: zoom, centerX: (surface.width / 2 - panX) / zoom,
                            centerY: (surface.height / 2 - panY) / zoom, selection: selection }
        flowViews = views
    }
    Component.onCompleted: { refreshNodeIds(); viewedFlow = app.currentFlow; fitTimer.restart() }
    Timer {
        id: fitTimer
        interval: 1
        onTriggered: root.fit()
    }
    Connections {
        target: app
        function onSelectionChanged() {
            root.hideHover()
            if (app.selectedNode || root.viewedFlow !== app.currentFlow) root.selectedWire = ""
            if (root.viewedFlow === app.currentFlow) {
                if (app.selectedNode && !root.selected(app.selectedNode)) root.selection = [app.selectedNode]
                return
            }
            root.rememberView(); root.viewedFlow = app.currentFlow
            let view = root.flowViews[app.currentFlow]
            root.zoom = view ? view.zoom : 1
            root.panX = surface.width / 2 - (view ? view.centerX * root.zoom : 0)
            root.panY = surface.height / 2 - (view ? view.centerY * root.zoom : 0)
            root.selection = view ? view.selection : []
        }
        function onNodeLocated(id) { root.centerNode(id) }
        function onProjectReplaced() { root.hideHover(); root.selectedWire = ""; root.nodeIds = []; root.refreshNodeIds(); root.flowViews = ({}); root.selection = []; root.viewedFlow = app.currentFlow; root.fitRequested = true; fitTimer.restart() }
        function onNodesChanged() {
            root.refreshNodeIds()
            if (!root.selectedConnection) root.selectedWire = ""
            root.selection = root.selection.filter(function(id) { return root.findNode(id) !== null })
            if (root.hoverNodeId !== "" && root.findNode(root.hoverNodeId) === null) root.hideHover()
        }
        function onCatalogChanged() { root.hideHover() }
    }
    Connections {
        target: root.Window.window
        function onActiveChanged() {
            if (root.Window.window && !root.Window.window.active) root.hideHover()
        }
    }
    Timer {
        id: hoverTimer
        interval: 500
        repeat: false
        onTriggered: {
            if (root.hoverNodeId !== "" && root.hoverNode !== null && root.gesture === ""
                    && (!root.Window.window || root.Window.window.active))
                root.hoverCardVisible = true
        }
    }
    Rectangle {
        anchors.fill: parent; color: QuiColor.Primary; border.color: "#d0d7de"; border.width: 1
    }
    ColumnLayout {
        anchors.fill: parent; anchors.margins: 1; spacing: 0
        Rectangle {
            Layout.fillWidth: true; Layout.preferredHeight: 34; color: "#f6f8fa"
            Rectangle {
                anchors.left: parent.left; anchors.right: parent.right; anchors.bottom: parent.bottom
                height: 1; color: "#d0d7de"
            }
            RowLayout {
                anchors.fill: parent; anchors.leftMargin: 8; anchors.rightMargin: 8; spacing: 6
                QuiText {
                    text: "流程图绘制区"
                    font: Qt.font({ family: "Microsoft YaHei UI", pixelSize: 12, bold: true })
                    color: "#24292f"
                }
                QuiTextIconButton {
                    iconSource: QuiFontIcon.Settings
                    implicitWidth: 24; implicitHeight: 24
                    text: "流程图设置"
                }
                Item { Layout.fillWidth: true }
                Rectangle {
                    id: searchBox
                    Layout.preferredWidth: Math.min(200, root.width * 0.35)
                    Layout.preferredHeight: 24
                    color: "#ffffff"; radius: 3
                    border.color: search.activeFocus ? QuiColor.Highlight : "#c5ccd5"; border.width: 1
                    RowLayout {
                        anchors.fill: parent; anchors.leftMargin: 6; anchors.rightMargin: 6; spacing: 4
                        QuiTextIcon {
                            iconSource: QuiFontIcon.Search; iconSize: Theme.iconSizeSmall; iconColor: Theme.iconMuted
                        }
                        TextInput {
                            id: search; objectName: "flowSearch"
                            Layout.fillWidth: true
                            font: QuiFont.Body; color: QuiColor.FontPrimary; selectByMouse: true
                            verticalAlignment: Text.AlignVCenter
                            onTextChanged: { app.searchNodes(text); root.searchIndex = 0 }
                            Keys.onDownPressed: root.searchIndex = Math.min(app.searchResults.length - 1, root.searchIndex + 1)
                            Keys.onUpPressed: root.searchIndex = Math.max(0, root.searchIndex - 1)
                            Keys.onReturnPressed: { if (app.searchResults.length) root.locate(app.searchResults[root.searchIndex]) }
                            Keys.onEnterPressed: { if (app.searchResults.length) root.locate(app.searchResults[root.searchIndex]) }
                            Keys.onEscapePressed: { text = ""; surface.forceActiveFocus() }
                            Text {
                                anchors.fill: parent; text: "搜索工具名"
                                color: Theme.iconMuted; font: QuiFont.Body
                                visible: !search.text && !search.activeFocus
                                verticalAlignment: Text.AlignVCenter
                            }
                        }
                        QuiTextIcon {
                            iconSource: QuiFontIcon.Clear; iconSize: Theme.iconSizeTiny; iconColor: Theme.iconMuted
                            visible: search.text.length > 0
                            MouseArea {
                                anchors.fill: parent
                                onClicked: { search.text = ""; surface.forceActiveFocus() }
                            }
                        }
                    }
                }
                Rectangle {
                    id: flowDropdown
                    Layout.preferredWidth: 120; Layout.preferredHeight: 24; radius: 3
                    color: "#ffffff"; border.color: "#c5ccd5"; border.width: 1
                    RowLayout {
                        anchors.fill: parent; anchors.leftMargin: 6; anchors.rightMargin: 6; spacing: 4
                        QuiText {
                            Layout.fillWidth: true; elide: Text.ElideRight
                            text: {
                                let idx = 1
                                for (let i = 0; i < app.flows.length; ++i) {
                                    if (app.flows[i].id === app.currentFlow) { idx = i + 1; break; }
                                }
                                return "选择流程: " + idx + "/" + (app.flows.length || 1)
                            }
                            font: QuiFont.Caption; color: QuiColor.FontPrimary
                        }
                        QuiTextIcon {
                            iconSource: QuiFontIcon.ChevronDown; iconSize: Theme.iconSizeTiny; iconColor: Theme.iconSecondary
                        }
                    }
                    MouseArea {
                        anchors.fill: parent; cursorShape: Qt.PointingHandCursor
                        onClicked: selectFlowMenu.popup(flowDropdown, 0, flowDropdown.height + 2)
                    }
                }
                QuiMenu {
                    id: selectFlowMenu
                    Repeater {
                        model: app.flows
                        QuiMenuItem {
                            required property var modelData
                            text: modelData.name + (modelData.id === app.currentFlow ? " ✓" : "")
                            onTriggered: app.selectFlow(modelData.id)
                        }
                    }
                }
                QuiTextIconButton {
                    iconSource: QuiFontIcon.FullScreen
                    implicitWidth: 26; implicitHeight: 24
                    text: "适应全部节点"
                    onClicked: root.fit()
                }
            }
        }
        Rectangle {
            Layout.fillWidth: true; Layout.preferredHeight: 28; color: "#f6f8fa"
            Rectangle {
                anchors.left: parent.left; anchors.right: parent.right; anchors.bottom: parent.bottom
                height: 1; color: "#d0d7de"
            }
            RowLayout {
                anchors.fill: parent; anchors.leftMargin: 6; anchors.rightMargin: 6; spacing: 4
                Rectangle {
                    Layout.preferredWidth: 22; Layout.preferredHeight: 22; radius: 3
                    color: addMouse.containsMouse ? "#f0f2f5" : "#ffffff"
                    border.color: "#d0d7de"; border.width: 1
                    enabled: !app.busy
                    QuiTextIcon {
                        anchors.centerIn: parent
                        iconSource: QuiFontIcon.Add; iconSize: Theme.iconSizeSmall; iconColor: Theme.iconSecondary
                    }
                    MouseArea {
                        id: addMouse; anchors.fill: parent; hoverEnabled: true
                        cursorShape: Qt.PointingHandCursor
                        onClicked: root.openName("addFlow", "")
                    }
                }
                Flickable {
                    Layout.fillWidth: true; Layout.fillHeight: true; clip: true
                    contentWidth: flowTabs.width; contentHeight: height
                    Row {
                        id: flowTabs; height: parent.height; spacing: 4
                        Repeater {
                            model: app.flows
                            Rectangle {
                                id: flowTabItem
                                required property var modelData
                                readonly property bool isActive: app.currentFlow === modelData.id
                                height: 25; width: tabRow.width + 16
                                anchors.bottom: parent.bottom
                                radius: 3
                                color: isActive ? "#ffffff" : "#f6f8fa"
                                border.color: isActive ? "#d0d7de" : "#e1e4e8"
                                border.width: 1
                                Rectangle {
                                    visible: flowTabItem.isActive
                                    anchors.left: parent.left; anchors.right: parent.right; anchors.bottom: parent.bottom
                                    height: 2; color: QuiColor.Highlight
                                }
                                RowLayout {
                                    id: tabRow
                                    anchors.centerIn: parent; spacing: 6
                                    Rectangle {
                                        width: 7; height: 7; radius: 3.5
                                        color: flowTabItem.modelData.selected ? "#2da44e" : "transparent"
                                        border.color: flowTabItem.modelData.selected ? "#2da44e" : "#8c959f"
                                        border.width: 1
                                        MouseArea {
                                            anchors.fill: parent; anchors.margins: -4
                                            enabled: !app.busy
                                            onClicked: app.setFlowSelected(flowTabItem.modelData.id, !flowTabItem.modelData.selected)
                                        }
                                    }
                                    QuiText {
                                        text: flowTabItem.modelData.name
                                        font: Qt.font({ family: "Microsoft YaHei UI", pixelSize: 12, bold: flowTabItem.isActive })
                                        color: flowTabItem.isActive ? QuiColor.Highlight : "#57606a"
                                    }
                                    QuiTextIcon {
                                        iconSource: QuiFontIcon.Cancel; iconSize: Theme.iconSizeTiny
                                        iconColor: tabCloseMouse.containsMouse ? Theme.error : Theme.iconMuted
                                        visible: true
                                        opacity: app.flows.length > 1 ? 1.0 : 0.4
                                        MouseArea {
                                            id: tabCloseMouse; anchors.fill: parent; anchors.margins: -3; hoverEnabled: true
                                            enabled: !app.busy && app.flows.length > 1
                                            onClicked: {
                                                root.contextFlow = flowTabItem.modelData.id
                                                deleteFlowDialog.open()
                                            }
                                        }
                                    }
                                }
                                MouseArea {
                                    anchors.fill: parent; acceptedButtons: Qt.LeftButton | Qt.RightButton
                                    onClicked: function(mouse) {
                                        if (mouse.button === Qt.LeftButton) {
                                            app.selectFlow(flowTabItem.modelData.id)
                                        } else if (mouse.button === Qt.RightButton) {
                                            root.contextFlow = flowTabItem.modelData.id
                                            flowMenu.popup()
                                        }
                                    }
                                }
                            }
                        }
                    }
                }
            }
        }
        FocusScope {
            id: surface; objectName: "flowSurface"
            Layout.fillWidth: true; Layout.fillHeight: true; clip: true
            property real previousWidth: 0
            property real previousHeight: 0
            onWidthChanged: {
                root.panX += (width - previousWidth) / 2; previousWidth = width
                if (root.fitRequested) fitTimer.restart()
            }
            onHeightChanged: {
                root.panY += (height - previousHeight) / 2; previousHeight = height
                if (root.fitRequested) fitTimer.restart()
            }
            Keys.onPressed: function(event) {
                if (!surface.activeFocus || app.busy) return
                if (event.matches(StandardKey.Copy)) { app.copyNodes(root.selection); event.accepted = true }
                else if (event.matches(StandardKey.Paste)) {
                    app.pasteNodes(Math.round((width / 2 - root.panX) / root.zoom / root.cellWidth), Math.round((height / 2 - root.panY) / root.zoom / root.cellHeight)); event.accepted = true
                } else if (event.key === Qt.Key_Delete || event.key === Qt.Key_Backspace) { app.deleteNodes(root.selection); root.selection = []; event.accepted = true }
                else if (event.key === Qt.Key_Escape) { root.selection = []; root.selectedWire = ""; root.gesture = ""; root.dragX = 0; root.dragY = 0; event.accepted = true }
            }
            Rectangle { anchors.fill: parent; color: "#f6f8fa" }
            Canvas {
                id: grid; anchors.fill: parent
                onWidthChanged: requestPaint(); onHeightChanged: requestPaint()
                Connections {
                    target: root
                    function onZoomChanged() { grid.requestPaint() }
                    function onPanXChanged() { grid.requestPaint() }
                    function onPanYChanged() { grid.requestPaint() }
                }
                onPaint: {
                    let ctx = getContext("2d"); ctx.clearRect(0, 0, width, height); ctx.fillStyle = "#d0d7de"
                    let step = 24 * root.zoom
                    for (let x = ((root.panX % step) + step) % step; x < width; x += step)
                        for (let y = ((root.panY % step) + step) % step; y < height; y += step) ctx.fillRect(x, y, 1, 1)
                }
            }
            MouseArea {
                id: backgroundMouse; anchors.fill: parent
                acceptedButtons: Qt.LeftButton | Qt.RightButton | Qt.MiddleButton; preventStealing: true
                onPressed: function(mouse) {
                    surface.forceActiveFocus(); root.hideHover()
                    root.selectedWire = ""
                    root.pressPoint = Qt.point(mouse.x, mouse.y); root.lastPoint = root.pressPoint; root.moved = false
                    root.gesture = mouse.button === Qt.LeftButton ? "select" : "pan"
                    if (mouse.button === Qt.LeftButton && !(mouse.modifiers & (Qt.ControlModifier | Qt.ShiftModifier))) { root.selection = []; app.selectNode("") }
                }
                onPositionChanged: function(mouse) { if (pressed) root.updateGesture(Qt.point(mouse.x, mouse.y)) }
                onReleased: function(mouse) {
                    if (root.gesture === "select" && root.moved) {
                        let left = (Math.min(root.pressPoint.x, mouse.x) - root.panX) / root.zoom
                        let top = (Math.min(root.pressPoint.y, mouse.y) - root.panY) / root.zoom
                        let right = (Math.max(root.pressPoint.x, mouse.x) - root.panX) / root.zoom
                        let bottom = (Math.max(root.pressPoint.y, mouse.y) - root.panY) / root.zoom
                        let ids = root.selection.slice()
                        for (let n of app.nodes)
                            if (n.x * root.cellWidth + 156 >= left && n.x * root.cellWidth <= right && n.y * root.cellHeight + 52 >= top && n.y * root.cellHeight <= bottom && ids.indexOf(n.id) < 0) ids.push(n.id)
                        root.selection = ids
                        if (ids.length) app.selectNode(ids[ids.length - 1])
                    }
                    root.gesture = ""
                }
                onDoubleClicked: function(mouse) { if (mouse.button === Qt.LeftButton) root.fit() }
            }
            WheelHandler {
                target: null
                onWheel: function(event) {
                    root.zoomAt(Math.pow(1.0015, event.angleDelta.y), event.x, event.y)
                    event.accepted = true
                }
            }
            Item {
                id: world; x: root.panX; y: root.panY; scale: root.zoom; transformOrigin: Item.TopLeft
                Repeater {
                    model: root.routedWires
                    WireItem {
                        required property var modelData
                        objectName: "flowWire_" + modelData.source + "_" + modelData.target
                        route: modelData.points
                        viewScale: root.zoom
                        connectionCount: modelData.count
                        lineColor: root.selectedWire === modelData.source + ":" + modelData.target ? "#0969da"
                            : root.selected(modelData.target) ? "#f08800"
                            : root.selected(modelData.source) ? "#1677ff" : "#8c959f"
                        opacity: root.selection.length && !root.selected(modelData.target) && !root.selected(modelData.source) ? 0.35 : 1
                        onActivated: {
                            root.hideHover()
                            surface.forceActiveFocus()
                            root.selection = []
                            app.selectNode("")
                            root.selectedWire = modelData.source + ":" + modelData.target
                        }
                    }
                }
                Repeater {
                    id: nodeRepeater
                    model: root.nodeIds
                    Loader {
                        id: nodeLoader
                        required property string modelData
                        readonly property var currentNode: root.findNode(modelData)
                        active: currentNode !== null
                        sourceComponent: NodeDelegate {
                            node: nodeLoader.currentNode; selected: root.selected(nodeLoader.modelData); viewScale: root.zoom
                            x: root.nodeX(nodeLoader.modelData); y: root.nodeY(nodeLoader.modelData)
                            onPointerPressed: function(mouse, item) { root.beginNode(mouse, item) }
                            onPointerMoved: function(mouse, item) { root.moveNode(mouse, item) }
                            onPointerReleased: function(mouse, item) { root.endNode(mouse, item) }
                            onExecuteRequested: { app.selectNode(nodeLoader.modelData); app.runNode() }
                            onHoverEntered: root.enterNode(nodeLoader.modelData)
                            onHoverExited: root.leaveNode(nodeLoader.modelData)
                        }
                    }
                }
            }
            Rectangle {
                id: wireRelations
                objectName: "wireRelations"
                visible: root.selectedConnection !== null
                anchors.left: parent.left
                anchors.bottom: parent.bottom
                anchors.margins: 8
                width: Math.min(420, parent.width - 16)
                height: relationsColumn.implicitHeight + 16
                color: "#ffffff"
                border.color: "#d0d7de"
                radius: Theme.radiusBase
                ColumnLayout {
                    id: relationsColumn
                    anchors.fill: parent
                    anchors.margins: 8
                    spacing: 4
                    QuiText {
                        Layout.fillWidth: true
                        font: QuiFont.Caption
                        elide: Text.ElideRight
                        text: {
                            const wire = root.selectedConnection
                            if (!wire) return ""
                            const source = root.findNode(wire.source)
                            const target = root.findNode(wire.target)
                            return (source ? source.title : wire.source) + " → " + (target ? target.title : wire.target)
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
                width: 232
                height: hoverColumn.implicitHeight + 16
                x: root.hoverCardX()
                y: root.hoverCardY()
                radius: Theme.radiusBase
                color: "#ffffff"
                border.color: Theme.border
                border.width: 1
                enabled: false
                ColumnLayout {
                    id: hoverColumn
                    anchors.fill: parent; anchors.margins: 8; spacing: 3
                    QuiText {
                        Layout.fillWidth: true
                        text: root.hoverNode ? root.hoverNode.title : ""
                        wrapMode: Text.Wrap
                        font: Qt.font({ family: "Microsoft YaHei UI", pixelSize: 12, bold: true })
                        color: Theme.textPrimary
                    }
                    Repeater {
                        model: [
                            { label: "算子", value: root.hoverNode ? root.hoverNode.timingOperatorText : "" },
                            { label: "预处理", value: root.hoverNode ? root.hoverNode.timingPreprocessText : "" },
                            { label: "后处理", value: root.hoverNode ? root.hoverNode.timingPostprocessText : "" }
                        ]
                        RowLayout {
                            required property var modelData
                            Layout.fillWidth: true
                            QuiText {
                                text: modelData.label
                                font: Qt.font({ family: "Microsoft YaHei UI", pixelSize: 11 })
                                color: Theme.textSecondary
                            }
                            Item { Layout.fillWidth: true }
                            QuiText {
                                text: modelData.value
                                font: Qt.font({ family: "Microsoft YaHei UI", pixelSize: 11 })
                                color: Theme.textPrimary
                            }
                        }
                    }
                }
            }
            Rectangle {
                visible: root.gesture === "select" && root.moved
                x: Math.min(root.pressPoint.x, root.lastPoint.x); y: Math.min(root.pressPoint.y, root.lastPoint.y)
                width: Math.abs(root.lastPoint.x - root.pressPoint.x); height: Math.abs(root.lastPoint.y - root.pressPoint.y)
                color: "#251887d8"; border.color: QuiColor.Highlight
            }
            DropArea {
                anchors.fill: parent; keys: ["visionrt/tool"]
                onEntered: function(drag) { drag.accepted = !app.busy }
                onDropped: function(drop) {
                    if (app.busy || !drop.source || !drop.source.toolId) return
                    app.addNode(drop.source.toolId, Math.round((drop.x - root.panX) / root.zoom / root.cellWidth), Math.round((drop.y - root.panY) / root.zoom / root.cellHeight))
                    drop.acceptProposedAction(); surface.forceActiveFocus()
                }
            }
        }
    }
    Rectangle {
        visible: search.text.length > 0 && search.activeFocus
        z: 20; x: 4; y: 34; width: parent.width - 8; height: Math.min(280, searchList.contentHeight + 8)
        color: QuiColor.Primary; border.color: QuiColor.Border; radius: 4
        ListView {
            id: searchList; anchors.fill: parent; anchors.margins: 4; clip: true
            model: app.searchResults; currentIndex: root.searchIndex
            section.property: "flowName"; section.criteria: ViewSection.FullString
            section.delegate: QuiText { required property string section; text: section; height: 26; font: QuiFont.Caption; color: QuiColor.FontCaption }
            delegate: QuiItemDelegate {
                required property var modelData; required property int index
                width: searchList.width; height: 34; text: modelData.title
                highlighted: index === root.searchIndex
                onClicked: root.locate(modelData)
            }
        }
    }
    QuiMenu {
        id: nodeMenu
        QuiMenuItem { text: "复制"; enabled: !app.busy; onTriggered: app.copyNodes(root.selection) }
        QuiMenuItem { text: "删除"; enabled: !app.busy; onTriggered: { app.deleteNodes(root.selection); root.selection = [] } }
        QuiMenuItem { text: "重命名备注…"; enabled: !app.busy; onTriggered: { let n = root.findNode(root.contextNode); root.openName("node", n ? n.alias : "") } }
    }
    QuiMenu {
        id: flowMenu
        QuiMenuItem { text: "重命名流程…"; enabled: !app.busy; onTriggered: { for (let f of app.flows) if (f.id === root.contextFlow) root.openName("flow", f.name) } }
        QuiMenuItem { text: "复制流程"; enabled: !app.busy; onTriggered: app.copyFlow(root.contextFlow) }
        QuiMenuItem { text: "删除流程…"; enabled: !app.busy && app.flows.length > 1; onTriggered: deleteFlowDialog.open() }
    }
    WorkbenchDialog {
        id: deleteFlowDialog; title: "删除流程"; message: "删除该流程及全部节点？此操作无法撤销。"
        onPositiveClicked: app.deleteFlow(root.contextFlow)
    }
    WorkbenchDialog {
        id: nameDialog; title: root.nameOperation === "node" ? "节点备注" : "流程名称"
        positiveButtonEnabled: !app.busy && (root.nameOperation === "node" || root.nameDraft.trim().length > 0)
        contentDelegate: Component {
            QuiTextField { text: root.nameDraft; implicitHeight: 44; onTextEdited: root.nameDraft = text; Component.onCompleted: { forceActiveFocus(); selectAll() } }
        }
        onPositiveClicked: {
            if (root.nameOperation === "node") app.renameNode(root.contextNode, root.nameDraft)
            else if (root.nameOperation === "flow") app.renameFlow(root.contextFlow, root.nameDraft)
            else app.addFlow(root.nameDraft)
        }
    }
}
