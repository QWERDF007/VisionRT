import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import QtQuick.Window
import quickui

QuiPopup {
    id: root
    property var context: ({})
    property string title: "选择上层输入"
    property var candidates: []
    property string query: ""
    width: Math.min(480, parent ? parent.width - 32 : 480)
    height: Math.min(480, parent ? parent.height - 32 : 480)
    closePolicy: Popup.CloseOnEscape | Popup.CloseOnPressOutside
    focus: true
    objectName: "bindingPopup"
    function refresh() {
        if (!visible) return
        if (app.busy || !app.portContextCurrent(context)) { close(); return }
        candidates = app.relationTargets(context)
    }
    function openOutput(source, name) {
        if (app.busy || !source.output || !app.portContextCurrent(source)) return
        context = source; title = name + " · 关联已有节点"
        candidates = app.relationTargets(context); query = ""; open()
    }
    onOpened: search.forceActiveFocus()
    onClosed: { context = ({}); candidates = [] }
    Connections {
        target: app
        function onSelectionChanged() { root.refresh() }
        function onProjectChanged() { root.refresh() }
        function onNodesChanged() { root.refresh() }
        function onParametersChanged() { root.refresh() }
        function onResultsChanged() { root.refresh() }
        function onRuntimeChanged() { root.refresh() }
    }
    Connections {
        target: root.parent ? root.parent.Window.window : null
        function onActiveChanged() { if (target && !target.active) root.close() }
    }
    ColumnLayout {
        anchors.fill: parent; anchors.margins: 14; spacing: 8
        QuiText { text: root.title; font: QuiFont.BodyStrong; Layout.fillWidth: true; elide: Text.ElideRight }
        QuiTextField { id: search; Layout.fillWidth: true; placeholderText: "搜索节点或端口"; text: root.query; onTextChanged: root.query = text }
        ListView {
            id: choices; Layout.fillWidth: true; Layout.fillHeight: true; clip: true
            model: root.candidates.filter(function(c) { return (c.title + " " + c.name).toLowerCase().indexOf(root.query.toLowerCase()) >= 0 })
            section.property: "title"; section.criteria: ViewSection.FullString
            section.delegate: QuiText { required property string section; text: section; height: 28; font: QuiFont.Caption; color: QuiColor.FontCaption }
            ScrollBar.vertical: QuiScrollBar {}
            delegate: QuiItemDelegate {
                required property var modelData
                id: targetRow
                width: choices.width
                text: modelData.name + "  ·  " + modelData.type + (modelData.connected ? " · 已连接" : "")
                rightPadding: modelData.connected ? 76 : 10
                enabled: !app.busy
                contentItem: QuiText {
                    text: targetRow.text; font: targetRow.font
                    elide: Text.ElideRight
                }
                onClicked: {
                    if (modelData.connected) return
                    let ok = app.bindOutputTo(root.context, modelData.node, modelData.port)
                    if (ok) root.close()
                }
                QuiButton {
                    objectName: "disconnectOutput_" + targetRow.modelData.node + "_" + targetRow.modelData.port
                    anchors.right: parent.right; anchors.rightMargin: 8; anchors.verticalCenter: parent.verticalCenter
                    visible: targetRow.modelData.connected
                    text: "断开"; implicitHeight: 26; focusPolicy: Qt.TabFocus
                    onClicked: {
                        if (app.unbindOutputFrom(root.context, targetRow.modelData.node, targetRow.modelData.port)) root.close()
                    }
                }
            }
            QuiText { anchors.centerIn: parent; visible: choices.count === 0; text: "没有兼容输入或已有连接"; color: QuiColor.FontCaption }
        }
        RowLayout {
            Layout.fillWidth: true
            Item { Layout.fillWidth: true }
            QuiButton { text: "关闭"; focusPolicy: Qt.TabFocus; onClicked: root.close() }
        }
    }
}
