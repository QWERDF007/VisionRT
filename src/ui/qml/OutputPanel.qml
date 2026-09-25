import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import QtQuick.Window
import quickui

Rectangle {
    id: root
    color: QuiColor.Primary
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
        RowLayout {
            Layout.fillWidth: true; Layout.preferredHeight: 32; spacing: 4
            QuiText { text: "输出参数"; Layout.leftMargin: 8 }
            Item { Layout.fillWidth: true }
            QuiTabButton {
                text: "数据"; checked: !root.relations
                implicitWidth: 46; implicitHeight: 24; radius: 3; font: QuiFont.Caption
                checkedColor: QuiColor.Highlight; checkedTextColor: "#ffffff"
                normalColor: "transparent"; normalTextColor: QuiColor.FontPrimary
                onClicked: root.relations = false
            }
            QuiTabButton {
                text: "关系"; checked: root.relations
                implicitWidth: 46; implicitHeight: 24; radius: 3; font: QuiFont.Caption
                checkedColor: QuiColor.Highlight; checkedTextColor: "#ffffff"
                normalColor: "transparent"; normalTextColor: QuiColor.FontPrimary
                Layout.rightMargin: 8
                onClicked: root.relations = true
            }
        }
        QuiTableView {
            id: table; objectName: "outputTable"
            Layout.fillWidth: true; Layout.fillHeight: true
            externalModel: app.outputModel
            externalCellDelegate: outputCell
            externalCellOptionsProvider: function(row, column, data) {
                return { row: row, column: column, display: data.display === undefined ? "" : String(data.display), portId: data.portId || "", name: data.name || "", tooltip: data.tooltip || "", count: data.count || 0, system: data.system === true }
            }
            columnSource: [
                { title: "参数名称", dataIndex: "name", readOnly: true },
                { title: "类型", dataIndex: "type", readOnly: true },
                { title: "数据", dataIndex: "preview", readOnly: true },
                { title: "引用数", dataIndex: "count", readOnly: true }
            ]
            view.columnWidthProvider: function(column) {
                if (root.relations) return column === 0 ? Math.max(140, table.width * 0.68) : column === 3 ? Math.max(80, table.width * 0.32) : 0
                return column === 0 ? Math.max(120, table.width * 0.32) : column === 1 ? Math.max(90, table.width * 0.20) : column === 2 ? Math.max(170, table.width * 0.48) : 0
            }
            resizableRows: false; resizableColumns: false
            rowHeight: 32; headerHeight: 30; showGridLines: true
            rowColor: QuiColor.Primary; zebraColor: "#f3f5f8"
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
                text: cell.options.column === 0 ? (cell.options.name || cell.options.display || "") + (!cell.options.system ? " (" + (cell.options.count || 0) + ")" : "") : cell.options.display || ""
            }
            MouseArea {
                id: cellPointer; anchors.fill: parent; hoverEnabled: true
                acceptedButtons: Qt.RightButton
                onClicked: function(mouse) { root.showMenu(cell.options, cell, Qt.point(mouse.x, mouse.y)) }
            }
            QuiToolTip { visible: cellPointer.containsMouse; text: cell.options.tooltip || cell.options.display || ""; delay: 500 }
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
