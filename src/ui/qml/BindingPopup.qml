import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import quickui

QuiPopup {
    id: root
    property string inputId: ""
    property string outputId: ""
    property string title: "选择上层输入"
    property bool outputMode: false
    property var candidates: []
    property string query: ""
    width: Math.min(480, parent ? parent.width - 32 : 480)
    height: Math.min(480, parent ? parent.height - 32 : 480)
    closePolicy: Popup.CloseOnEscape | Popup.CloseOnPressOutside
    function openInput(id, name) {
        inputId = id; outputMode = false; title = name + " · 上层输入"
        candidates = app.bindingCandidates(id); query = ""; open()
    }
    function openOutput(id, name) {
        outputId = id; outputMode = true; title = name + " · 关联已有节点"
        candidates = app.relationTargets(id); query = ""; open()
    }
    ColumnLayout {
        anchors.fill: parent; anchors.margins: 14; spacing: 8
        QuiText { text: root.title; font: QuiFont.BodyStrong; Layout.fillWidth: true; elide: Text.ElideRight }
        QuiTextField { Layout.fillWidth: true; placeholderText: "搜索节点或端口"; text: root.query; onTextChanged: root.query = text; Component.onCompleted: forceActiveFocus() }
        ListView {
            id: choices; Layout.fillWidth: true; Layout.fillHeight: true; clip: true
            model: root.candidates.filter(function(c) { return (c.title + " " + c.name).toLowerCase().indexOf(root.query.toLowerCase()) >= 0 })
            section.property: "title"; section.criteria: ViewSection.FullString
            section.delegate: QuiText { required property string section; text: section; height: 28; font: QuiFont.Caption; color: QuiColor.FontCaption }
            ScrollBar.vertical: QuiScrollBar {}
            delegate: QuiItemDelegate {
                required property var modelData
                width: choices.width; text: modelData.name + "  ·  " + modelData.type
                enabled: !app.busy
                onClicked: {
                    let ok = root.outputMode ? app.bindOutputTo(root.outputId, modelData.node, modelData.port) : app.bindInput(root.inputId, modelData.node, modelData.port)
                    if (ok) root.close()
                }
            }
            QuiText { anchors.centerIn: parent; visible: choices.count === 0; text: root.outputMode ? "没有兼容的未占用输入" : "没有兼容且不会成环的输出"; color: QuiColor.FontCaption }
        }
        RowLayout {
            Layout.fillWidth: true
            QuiButton { text: "解除绑定"; visible: !root.outputMode; enabled: !app.busy; focusPolicy: Qt.TabFocus; onClicked: { app.unbindInput(root.inputId); root.close() } }
            Item { Layout.fillWidth: true }
            QuiButton { text: "关闭"; focusPolicy: Qt.TabFocus; onClicked: root.close() }
        }
    }
}
