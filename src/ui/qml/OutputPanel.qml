import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import QtQuick.Window
import quickui

Rectangle {
    id: root
    color: QuiColor.Primary
    border.width: 0
    property bool relations: false
    property var selectedContext: ({})
    property var pendingAction: null
    property string selectedName: ""
    property bool selectedSystem: true
    property bool selectedDiagnostic: false
    property string fullText: ""
    property var cascadeTools: []
    function showNameMenu(options, item, point) {
        selectedContext = app.portContext(options.portId, true)
        if (!app.portContextCurrent(selectedContext)) return
        selectedName = options.name; selectedSystem = options.system
        cascadeTools = app.cascadeOutputCandidates(selectedContext)
        let position = item.mapToItem(root, point.x, point.y)
        nameMenu.popup(root, position.x, position.y)
    }
    function showDataMenu(options, item, point) {
        selectedContext = app.portContext(options.portId, true)
        if (!app.portContextCurrent(selectedContext)) return
        selectedName = options.name; selectedSystem = options.system
        selectedDiagnostic = options.diagnostic || false
        pendingAction = null
        let position = item.mapToItem(root, point.x, point.y)
        outputMenu.popup(root, position.x, position.y)
    }
    function queueAction(action) {
        if (!app.portContextCurrent(selectedContext)) return
        pendingAction = { action: action, context: selectedContext, name: selectedName }
        outputMenu.close()
    }
    function invalidateMenu() {
        pendingAction = null; selectedContext = ({})
        outputMenu.close(); nameMenu.close(); binding.close()
    }
    function validateMenu() {
        if (app.busy || !app.portContextCurrent(selectedContext)) invalidateMenu()
    }
    ColumnLayout {
        anchors.fill: parent; spacing: 0
        Rectangle {
            Layout.fillWidth: true; Layout.preferredHeight: 34; color: "#f6f8fa"
            Rectangle {
                anchors.left: parent.left; anchors.right: parent.right; anchors.bottom: parent.bottom
                height: 1; color: "#d0d7de"
            }
            RowLayout {
                anchors.fill: parent; anchors.leftMargin: 8; anchors.rightMargin: 8; spacing: 6
                QuiText {
                    text: "输出参数"
                    font: Qt.font({ family: "Microsoft YaHei UI", pixelSize: 12, bold: true })
                    color: "#24292f"
                }
                Item { Layout.fillWidth: true }
                Rectangle {
                    Layout.preferredWidth: 92; Layout.preferredHeight: 22
                    radius: 3; color: "#ffffff"; border.color: QuiColor.Highlight; border.width: 1
                    clip: true
                    Row {
                        anchors.fill: parent
                        Rectangle {
                            width: parent.width / 2; height: parent.height
                            color: !root.relations ? QuiColor.Highlight : "transparent"
                            QuiText {
                                anchors.centerIn: parent; text: "数据"
                                font: Qt.font({ family: "Microsoft YaHei UI", pixelSize: 11, bold: !root.relations })
                                color: !root.relations ? "#ffffff" : QuiColor.Highlight
                            }
                            MouseArea {
                                anchors.fill: parent; cursorShape: Qt.PointingHandCursor
                                onClicked: root.relations = false
                            }
                        }
                        Rectangle {
                            width: parent.width / 2; height: parent.height
                            color: root.relations ? QuiColor.Highlight : "transparent"
                            QuiText {
                                anchors.centerIn: parent; text: "关系"
                                font: Qt.font({ family: "Microsoft YaHei UI", pixelSize: 11, bold: root.relations })
                                color: root.relations ? "#ffffff" : QuiColor.Highlight
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
                    diagnostic: data.diagnostic || false,
                    count: data.count || 0,
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
            rowHeight: 28; headerHeight: 28; showGridLines: true
            rowColor: "#ffffff"; zebraColor: "#f8f9fa"
            selectedColor: "#dceefa"; selectedBorderColor: QuiColor.Highlight; hoverColor: "#eaf4fc"
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
                font: QuiFont.Body
                color: cell.options.column === 0 && !cell.options.system ? QuiColor.Highlight : QuiColor.FontPrimary
                text: cell.options.column === 0 ? (cell.options.name || cell.options.display || "") + (!cell.options.system && root.relations ? " (" + (cell.options.count || 0) + ")" : "") : cell.options.display || ""
            }
            MouseArea {
                id: cellPointer; anchors.fill: parent; hoverEnabled: true
                acceptedButtons: Qt.RightButton
                onClicked: function(mouse) {
                    if (cell.options.column === 0 && !cell.options.system) {
                        root.showNameMenu(cell.options, cell, Qt.point(mouse.x, mouse.y))
                    } else {
                        root.showDataMenu(cell.options, cell, Qt.point(mouse.x, mouse.y))
                    }
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
        id: outputMenu
        objectName: "outputMenu"
        parent: root
        onClosed: {
            let pending = root.pendingAction
            root.pendingAction = null; root.selectedContext = ({})
            if (!pending || !app.portContextCurrent(pending.context)) return
            if (pending.action === "bind") binding.openOutput(pending.context, pending.name)
            else app.requestOutputText(pending.context, pending.action.endsWith("copy"), pending.action.startsWith("diagnostic"))
        }
        QuiMenuItem { text: "查看数据"; enabled: app.portContextCurrent(root.selectedContext); onTriggered: root.queueAction("view") }
        QuiMenuItem { text: "复制数据"; enabled: app.portContextCurrent(root.selectedContext); onTriggered: root.queueAction("copy") }
        QuiMenuItem { text: "查看诊断 JSON"; visible: root.selectedSystem; enabled: app.portContextCurrent(root.selectedContext) && root.selectedDiagnostic; onTriggered: root.queueAction("diagnostic-view") }
        QuiMenuItem { text: "复制诊断 JSON"; visible: root.selectedSystem; enabled: app.portContextCurrent(root.selectedContext) && root.selectedDiagnostic; onTriggered: root.queueAction("diagnostic-copy") }
        QuiMenuItem { text: "关联已有节点…"; enabled: !app.busy && !root.selectedSystem; onTriggered: root.queueAction("bind") }
    }
    BindingPopup { id: binding; objectName: "bindingPopup" }
    QuiMenu {
        id: nameMenu
        objectName: "outputNameMenu"
        parent: root
        Instantiator {
            model: root.cascadeTools
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
                            : modelData.occupied ? ((modelData.title || modelData.name) + " (" + (modelData.typeText || modelData.type) + " · 已被占用)")
                            : ((modelData.title || modelData.name) + " (" + (modelData.typeText || modelData.type) + ")")
                        enabled: !app.busy && !modelData.occupied
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
            onObjectAdded: function(index, object) { nameMenu.insertMenu(index, object) }
            onObjectRemoved: function(index, object) { nameMenu.removeMenu(object) }
        }
        QuiMenuItem {
            visible: root.cascadeTools.length === 0
            text: "无可连接的下层输入"
            enabled: false
        }
    }
    WorkbenchTextDialog {
        id: dataDialog; objectName: "outputDataDialog"; title: "输出数据 · 点击时的快照"
        text: root.fullText
        monospace: true
        description: "超大数据会先提示预计体积；确认需要后关闭此窗，再次点击原“查看数据 / 复制数据”操作。"
    }
    Connections {
        target: app
        function onOutputTextReady(text) { root.fullText = text; dataDialog.open() }
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
