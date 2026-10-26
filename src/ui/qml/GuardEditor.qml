import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import quickui

// Selects one Bool control dependency for the current node. Only nodes strictly
// above the target (smaller row) are offered, matching data-binding rules.
Dialog {
    id: root
    property string nodeId: ""
    property var candidates: []
    property string selectedNode: ""
    property string selectedPort: ""
    modal: true
    title: "执行条件"
    width: 520
    height: 420
    standardButtons: Dialog.NoButton
    padding: 12

    function openFor(id) {
        nodeId = id
        candidates = app.guardCandidates(id)
        selectedNode = ""
        selectedPort = ""
        for (let i = 0; i < candidates.length; ++i) {
            if (candidates[i].selected) {
                selectedNode = candidates[i].node
                selectedPort = candidates[i].port
                break
            }
        }
        open()
    }

    Connections {
        target: app
        function onSelectedNodeChanged() {
            if (root.visible && app.selectedNode !== root.nodeId)
                root.close()
        }
        function onRuntimeBusyChanged() {
            if (root.visible && app.runtimeBusy)
                root.close()
        }
    }

    contentItem: ColumnLayout {
        spacing: 8
        QuiText {
            Layout.fillWidth: true
            wrapMode: Text.WordWrap
            color: Theme.paramRowText
            text: "条件为假时本节点本轮不执行；条件来源跳过时本节点同样跳过；条件来源失败或缺失时本节点被阻塞，不会被当成假分支。"
        }
        ListView {
            Layout.fillWidth: true
            Layout.fillHeight: true
            clip: true
            model: root.candidates
            delegate: ItemDelegate {
                id: itemDelegate
                required property var modelData
                width: ListView.view.width
                highlighted: root.selectedNode === modelData.node && root.selectedPort === modelData.port
                text: modelData.title + " · " + modelData.portName + (modelData.selected ? "（已应用）" : "")
                onClicked: {
                    root.selectedNode = modelData.node
                    root.selectedPort = modelData.port
                }
                onDoubleClicked: {
                    root.selectedNode = modelData.node
                    root.selectedPort = modelData.port
                    if (app.applyExecutionGuard(root.nodeId, root.selectedNode, root.selectedPort))
                        root.close()
                }
            }
            QuiText {
                anchors.centerIn: parent
                visible: root.candidates.length === 0
                color: Theme.paramRowText
                text: "没有可选的上游布尔输出"
            }
        }
        RowLayout {
            Layout.fillWidth: true
            Button {
                text: "清除条件"
                enabled: !app.runtimeBusy && (app.executionGuardText.length > 0 || root.selectedNode.length > 0)
                onClicked: {
                    if (app.applyExecutionGuard(root.nodeId, "", ""))
                        root.close()
                }
            }
            Item { Layout.fillWidth: true }
            Button {
                text: "取消"
                onClicked: root.close()
            }
            Button {
                text: "确定"
                highlighted: true
                enabled: !app.runtimeBusy && (root.selectedNode.length > 0 || app.executionGuardText.length > 0)
                onClicked: {
                    if (app.applyExecutionGuard(root.nodeId, root.selectedNode, root.selectedPort))
                        root.close()
                }
            }
        }
    }
}
