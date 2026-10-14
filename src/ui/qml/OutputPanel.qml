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
    property int selectedRow: -1
    property string selectedPort: ""
    property string selectedName: ""
    property bool selectedSystem: true
    property string fullText: ""
    function showMenu(options, item, point) {
        selectedRow = options.row; selectedPort = options.portId
        selectedName = options.name; selectedSystem = options.system
        outputMenu.popup(item, point.x, point.y)
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
                    type: data.type || "",
                    tooltip: data.tooltip || "",
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
                onClicked: function(mouse) { root.showMenu(cell.options, cell, Qt.point(mouse.x, mouse.y)) }
            }
            QuiToolTip {
                visible: cellPointer.containsMouse
                text: {
                    let lines = []
                    if (cell.options.name) {
                        let nameLine = "参数: " + cell.options.name
                        if (cell.options.portId && cell.options.portId !== cell.options.name) {
                            nameLine += " (" + cell.options.portId + ")"
                        }
                        lines.push(nameLine)
                    }
                    if (cell.options.type) lines.push("类型: " + cell.options.type)
                    if (!cell.options.system && cell.options.count !== undefined) lines.push("引用: " + cell.options.count + " 次")
                    let dataText = cell.options.tooltip || (cell.options.column === 2 ? cell.options.display : "")
                    if (dataText) {
                        if (dataText.length > 50 && dataText.indexOf("\n") < 0) {
                            let tokens = dataText.split(/(?<=[,; /])/)
                            let parts = [], current = ""
                            for (let t of tokens) {
                                if (current.length + t.length > 50) {
                                    if (current.length > 0) { parts.push(current); current = "" }
                                    while (t.length > 50) { parts.push(t.slice(0, 50)); t = t.slice(50) }
                                    current = t
                                } else { current += t }
                            }
                            if (current.length > 0) parts.push(current)
                            lines.push("数据:\n" + parts.join("\n"))
                        } else {
                            lines.push("数据: " + dataText)
                        }
                    }
                    return lines.length > 0 ? lines.join("\n") : (cell.options.display || "")
                }
                delay: 500
            }
        }
    }
    QuiMenu {
        id: outputMenu
        QuiMenuItem { text: "查看数据"; enabled: root.selectedRow >= 0; onTriggered: app.requestOutputText(root.selectedRow, false) }
        QuiMenuItem { text: "复制数据"; enabled: root.selectedRow >= 0; onTriggered: app.requestOutputText(root.selectedRow, true) }
        QuiMenuItem { text: "关联已有节点…"; enabled: !app.busy && !root.selectedSystem; onTriggered: binding.openOutput(root.selectedPort, root.selectedName) }
    }
    BindingPopup { id: binding }
    WorkbenchDialog {
        id: dataDialog; title: "输出数据 · 点击时的快照"
        width: Math.min(900, parent ? parent.width - 40 : 900)
        useNegativeButton: false; positiveText: "关闭"
        contentDelegate: Component {
            ColumnLayout {
                implicitHeight: Math.min(520, root.Window.window ? root.Window.window.height - 170 : 520)
                QuiText { text: "超大数据会先提示预计体积；确认需要后关闭此窗，再次点击原“查看数据 / 复制数据”操作。"; wrapMode: Text.Wrap; Layout.fillWidth: true; color: QuiColor.FontCaption }
                ScrollView {
                    Layout.fillWidth: true; Layout.fillHeight: true
                    QuiTextArea { text: root.fullText; readOnly: true; selectByMouse: true; wrapMode: TextEdit.NoWrap; font.family: "Consolas" }
                }
            }
        }
    }
    Connections { target: app; function onOutputTextReady(text) { root.fullText = text; dataDialog.open() } }
}
