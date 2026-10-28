import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import QtQuick.Window
import quickui
import VisionRT.Style 1.0

Rectangle {
    id: root
    color: QuiColor.Primary
    border.width: 0
    property bool relations: false
    property var selectedContext: ({})
    // The copy request is released only after the menu has actually closed, so the popup teardown
    // can never race the bounded readback that is about to capture the same publication.
    property var pendingContext: ({})
    property string selectedName: ""
    property bool selectedSystem: true
    property var cascadeTools: []
    function showNameMenu(options, item, point) {
        selectedContext = app.portDeclarationContext(options.portId, true)
        if (!app.portContextCurrent(selectedContext)) return
        selectedName = options.name; selectedSystem = options.system
        // The name column is the connection menu: an empty candidate list opens nothing at all
        // instead of offering a placeholder row.
        cascadeTools = app.cascadeOutputCandidates(selectedContext)
        if (cascadeTools.length === 0) return
        let position = item.mapToItem(root, point.x, point.y)
        nameMenu.popup(root, position.x, position.y)
    }
    function showDataMenu(options, item, point) {
        // A row without a published value still opens its menu so that copy is visibly disabled
        // rather than silently missing.
        const resultContext = app.portResultContext(options.portId)
        selectedContext = resultContext
        selectedName = options.name; selectedSystem = options.system
        pendingContext = ({})
        let position = item.mapToItem(root, point.x, point.y)
        copyMenu.popup(root, position.x, position.y)
    }
    function copyEnabled() {
        return !app.busy && !isEmptyContext(selectedContext) && selectedContext.available !== false
                && app.portContextCurrent(selectedContext)
    }
    function isEmptyContext(context) {
        return !context || Object.keys(context).length === 0
    }
    function invalidateMenu() {
        pendingContext = ({})
        selectedContext = ({})
        copyMenu.close(); nameMenu.close()
    }
    function validateMenu() {
        if (app.busy || isEmptyContext(selectedContext) || !app.portContextCurrent(selectedContext))
            invalidateMenu()
    }
    ColumnLayout {
        anchors.fill: parent; spacing: 0
        Rectangle {
            Layout.fillWidth: true; Layout.preferredHeight: Theme.tabBarHeight; color: Theme.paramGroupHeaderBg
            Rectangle {
                anchors.left: parent.left; anchors.right: parent.right; anchors.bottom: parent.bottom
                height: 1; color: Theme.paramGroupHeaderBorder
            }
            RowLayout {
                anchors.fill: parent; anchors.leftMargin: 8; anchors.rightMargin: 8; spacing: 6
                QuiText {
                    text: "输出参数"
                    font: Theme.bodyFont
                    color: Theme.paramGroupHeaderText
                }
                Item { Layout.fillWidth: true }
                Rectangle {
                    Layout.preferredWidth: 92; Layout.preferredHeight: 22
                    radius: Theme.radiusSmall; color: Theme.surfaceBg; border.color: Theme.highlight; border.width: 1
                    clip: true
                    Row {
                        anchors.fill: parent
                        Rectangle {
                            width: parent.width / 2; height: parent.height
                            color: !root.relations ? Theme.highlight : "transparent"
                            QuiText {
                                anchors.centerIn: parent; text: "数据"
                                font: Theme.captionFont
                                color: !root.relations ? Theme.surfaceBg : Theme.highlight
                            }
                            MouseArea {
                                anchors.fill: parent; cursorShape: Qt.PointingHandCursor
                                onClicked: root.relations = false
                            }
                        }
                        Rectangle {
                            width: parent.width / 2; height: parent.height
                            color: root.relations ? Theme.highlight : "transparent"
                            QuiText {
                                anchors.centerIn: parent; text: "关系"
                                font: Theme.captionFont
                                color: root.relations ? Theme.surfaceBg : Theme.highlight
                            }
                            MouseArea {
                                anchors.fill: parent; cursorShape: Qt.PointingHandCursor
                                onClicked: root.relations = true
                            }
                        }
                    }
                }
            }
        }
        QuiTableView {
            id: table; objectName: "outputTable"
            Layout.fillWidth: true; Layout.fillHeight: true
            boundsBehavior: Flickable.StopAtBounds
            externalModel: app.outputModel
            externalCellDelegate: outputCell
            externalCellOptionsProvider: function(row, column, data) {
                return {
                    row: row,
                    column: column,
                    display: data.display === undefined ? "" : String(data.display),
                    portId: data.portId || "",
                    name: data.name || "",
                    type: data.typeDisplay || data.type || "",
                    typeMachine: data.type || "",
                    tooltip: data.tooltip || "",
                    count: data.count || 0,
                    connectable: data.connectable === true,
                    system: data.system === true
                }
            }
            columnSource: [
                { title: "参数名称", dataIndex: "name", readOnly: true },
                { title: "类型", dataIndex: "type", readOnly: true },
                { title: "数据", dataIndex: "preview", readOnly: true },
                { title: "引用数", dataIndex: "count", readOnly: true }
            ]
            view.columnWidthProvider: function(column) {
                if (root.relations) return column === 0 ? Math.max(140, Math.round(table.width * 0.65)) : column === 3 ? Math.max(80, table.width - Math.max(140, Math.round(table.width * 0.65)) - 2) : 0
                let col0 = Math.max(130, Math.round(table.width * 0.35))
                return column === 0 ? col0 : column === 2 ? Math.max(180, table.width - col0 - 2) : 0
            }
            resizableRows: false; resizableColumns: false
            rowHeight: Theme.parameterRowHeight; headerHeight: Theme.parameterRowHeight; showGridLines: true
            rowColor: Theme.surfaceBg; zebraColor: Theme.paramPanelBg
            selectedColor: Theme.paramRowHoverBg; selectedBorderColor: Theme.highlight; hoverColor: Theme.paramRowHoverBg
        }
    }
    onRelationsChanged: table.forceLayout()
    Component {
        id: outputCell
        Item {
            id: cell
            property var options: ({})
            QuiText {
                anchors.fill: parent; anchors.leftMargin: 8; anchors.rightMargin: 8
                verticalAlignment: Text.AlignVCenter; elide: Text.ElideRight
                font: Theme.parameterFont
                // Only a port with a legal, still free downstream input looks actionable. An existing
                // connection keeps its reference count but never stands in for a new target.
                color: cell.options.column === 0 && cell.options.connectable ? Theme.highlight : Theme.parameterTextColor
                text: cell.options.column === 0 ? (cell.options.name || cell.options.display || "") + (!cell.options.system && root.relations ? " (" + (cell.options.count || 0) + ")" : "") : cell.options.display || ""
            }
            MouseArea {
                id: cellPointer; anchors.fill: parent; hoverEnabled: true
                acceptedButtons: Qt.RightButton
                onClicked: function(mouse) {
                    // Column 0 is the connection menu; the data column only ever offers copy.
                    if (cell.options.column === 0)
                        root.showNameMenu(cell.options, cell, Qt.point(mouse.x, mouse.y))
                    else
                        root.showDataMenu(cell.options, cell, Qt.point(mouse.x, mouse.y))
                }
            }
            QuiToolTip {
                visible: cellPointer.containsMouse && text.length > 0
                text: {
                    if (cell.options.column === 0) {
                        let n = cell.options.name || cell.options.portId || ""
                        let t = cell.options.type || ""
                        return t.length > 0 ? (n + "（" + t + "）") : n
                    }
                    if (cell.options.column === 2)
                        return cell.options.tooltip || cell.options.display || ""
                    if (cell.options.column === 1)
                        return cell.options.type || cell.options.display || ""
                    if (cell.options.column === 3)
                        return String(cell.options.count || 0)
                    return cell.options.display || ""
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
            const pending = root.pendingContext
            root.pendingContext = ({})
            root.selectedContext = ({})
            if (root.isEmptyContext(pending) || !app.portContextCurrent(pending)) return
            // Copy stays a bounded background task with a readback acknowledgement; the GUI thread
            // never serialises the value and never truncates it into something that looks complete.
            app.requestOutputText(pending, true, false)
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
            if (!search || search.trim() === "") return root.cascadeTools
            let q = search.trim().toLowerCase()
            let res = []
            for (let tool of root.cascadeTools) {
                let toolMatch = (tool.title || "").toLowerCase().indexOf(q) >= 0
                let matchedInputs = []
                for (let input of tool.inputs) {
                    let inputName = (input.title || input.name || "").toLowerCase()
                    let portName = (input.port || "").toLowerCase()
                    if (toolMatch || inputName.indexOf(q) >= 0 || portName.indexOf(q) >= 0) {
                        matchedInputs.push(input)
                    }
                }
                if (matchedInputs.length > 0) {
                    res.push({
                        node: tool.node,
                        title: tool.title,
                        inputs: matchedInputs
                    })
                }
            }
            return res
        }
        onAboutToShow: search = ""
        onClosed: search = ""
        Item {
            implicitWidth: 180
            implicitHeight: 32
            QuiTextField {
                id: nameSearchField
                anchors.fill: parent
                anchors.margins: 4
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
                        text: modelData.connected ? ("✓ " + (modelData.title || modelData.name) + " (" + (modelData.typeText || modelData.type) + ")")
                            : ((modelData.title || modelData.name) + " (" + (modelData.typeText || modelData.type) + ")")
                        enabled: !app.busy
                        onTriggered: {
                            if (modelData.connected) {
                                app.unbindOutputFrom(root.selectedContext, toolSubMenu.targetNode, modelData.port)
                            } else {
                                app.bindOutputTo(root.selectedContext, toolSubMenu.targetNode, modelData.port)
                            }
                        }
                    }
                    onObjectAdded: function(index, object) { toolSubMenu.insertItem(index, object) }
                    onObjectRemoved: function(index, object) { toolSubMenu.removeItem(object) }
                }
            }
            onObjectAdded: function(index, object) { nameMenu.insertMenu(index + 1, object) }
            onObjectRemoved: function(index, object) { nameMenu.removeMenu(object) }
        }
    }
    Connections {
        target: app
        function onSelectionChanged() { root.validateMenu() }
        function onNodesChanged() { root.validateMenu() }
        function onParametersChanged() { root.validateMenu() }
        function onRuntimeChanged() { root.validateMenu() }
        function onResultsChanged() { root.validateMenu() }
        function onProjectReplaced() { root.invalidateMenu() }
    }
    Connections { target: app.outputModel; function onModelAboutToBeReset() { root.invalidateMenu() } }
    Connections {
        target: root.Window.window
        function onActiveChanged() { if (target && !target.active) root.invalidateMenu() }
    }
}
