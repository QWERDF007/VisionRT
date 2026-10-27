import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import quickui
import VisionRT.Style 1.0

// Node-model entry points above the ordinary parameter groups. The editors are
// created on demand so that an idle workbench opens no extra popup windows.
Rectangle {
    id: root
    visible: app.nodeModel !== "fixed"
    implicitHeight: visible ? 38 : 0
    color: Theme.paramRowHoverBg

    function openEditor(source, nodeId) {
        if (!editorLoader.active)
            editorLoader.setSource(source)
        if (editorLoader.item)
            editorLoader.item.openNode(nodeId)
        else
            Qt.callLater(function() { if (editorLoader.item) editorLoader.item.openNode(nodeId) })
    }

    RowLayout {
        anchors.fill: parent
        anchors.leftMargin: 8
        anchors.rightMargin: 8
        spacing: 8
        Button {
            visible: app.nodeModel === "expression" || app.nodeModel === "condition"
            text: app.nodeModel === "condition" ? "条件公式…" : "变量与公式…"
            onClicked: root.openEditor("ExpressionEditor.qml", app.selectedNode)
        }
        Button {
            text: app.executionGuardText.length > 0 ? "执行条件：" + app.executionGuardText : "设置执行条件"
            onClicked: root.openEditor("GuardEditor.qml", app.selectedNode)
        }
        Button {
            visible: app.nodeModel === "select"
            text: app.selectTypeText.length > 0 ? "合流类型：" + app.selectTypeText : "选择合流类型"
            onClicked: root.openEditor("SelectTypeEditor.qml", app.selectedNode)
        }
        Item { Layout.fillWidth: true }
    }

    Loader { id: editorLoader; active: false }
}
