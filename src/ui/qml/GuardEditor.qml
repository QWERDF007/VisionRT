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
    modal: true
    title: "执行条件"
    width: 520
    height: 420
    standardButtons: Dialog.NoButton
    padding: 12

    function openFor(id) {
        nodeId = id
        candidates = app.guardCandidates(id)
        open()
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
                required property var modelData
                width: ListView.view.width
                text: modelData.title + " · " + modelData.portName + (modelData.selected ? "（当前）" : "")
                onClicked: {
                    if (app.applyExecutionGuard(root.nodeId, modelData.node, modelData.port))
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
                enabled: app.executionGuardText.length > 0
                onClicked: { if (app.applyExecutionGuard(root.nodeId, "", "")) root.close() }
            }
            Item { Layout.fillWidth: true }
            Button { text: "关闭"; onClicked: root.close() }
        }
    }
}
