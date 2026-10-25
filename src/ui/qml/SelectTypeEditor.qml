import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import quickui

// Merge type for the host select node. The chosen TypeSpec replaces both branch
// inputs and the output; changing it requires the ports to be unbound first.
Dialog {
    id: root
    property string nodeId: ""
    property var current: ({})
    modal: true
    title: "合流类型"
    width: 420
    height: 300
    standardButtons: Dialog.NoButton
    padding: 12

    readonly property var typeNames: ["Int", "Float", "Bool", "String", "Array", "Image", "ROI"]
    readonly property var dtypeNames: ["int32", "int64", "float32", "float64", "bool", "string"]

    function openFor(id) {
        nodeId = id
        current = app.selectType(id)
        typeBox.currentIndex = Math.max(0, typeNames.indexOf(current.type || "Int"))
        dtypeBox.currentIndex = Math.max(0, dtypeNames.indexOf(current.dtype || "float64"))
        rankBox.value = Math.max(1, current.rank || 1)
        open()
    }

    contentItem: ColumnLayout {
        spacing: 10
        RowLayout {
            QuiText { text: "类型"; color: Theme.paramRowText }
            ComboBox { id: typeBox; Layout.preferredWidth: 160; model: root.typeNames }
        }
        RowLayout {
            visible: typeBox.currentText === "Array"
            QuiText { text: "元素"; color: Theme.paramRowText }
            ComboBox { id: dtypeBox; Layout.preferredWidth: 160; model: root.dtypeNames }
            QuiText { text: "维度"; color: Theme.paramRowText }
            SpinBox { id: rankBox; from: 1; to: 4 }
        }
        QuiText {
            Layout.fillWidth: true
            wrapMode: Text.WordWrap
            color: Theme.paramRowText
            text: "合流的两条输入与输出使用同一类型；未选中的分支必须是跳过或被阻塞，不能回退到另一侧。"
        }
        Item { Layout.fillHeight: true }
        RowLayout {
            Layout.fillWidth: true
            Item { Layout.fillWidth: true }
            Button { text: "取消"; onClicked: root.close() }
            Button {
                text: "应用"
                highlighted: true
                onClicked: {
                    if (app.applySelectType(root.nodeId, typeBox.currentText, dtypeBox.currentText, rankBox.value))
                        root.close()
                }
            }
        }
    }
}
