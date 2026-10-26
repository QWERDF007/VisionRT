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
    parent: Overlay.overlay ? Overlay.overlay : undefined
    x: Math.round(((parent ? parent.width : 0) - width) / 2)
    y: Math.round(((parent ? parent.height : 0) - height) / 2)
    width: 440
    height: 320
    standardButtons: Dialog.NoButton
    padding: 12

    background: Rectangle {
        color: QuiColor.CardBackground
        border.color: QuiColor.CardBorder
        border.width: 1
        radius: 6
    }

    readonly property var typeList: [
        { name: "Int", label: "整数" },
        { name: "Float", label: "浮点数" },
        { name: "Bool", label: "布尔" },
        { name: "String", label: "字符串" },
        { name: "Array", label: "数组" },
        { name: "Image", label: "图像" },
        { name: "ROI", label: "区域 (ROI)" }
    ]
    readonly property var dtypeList: [
        { name: "int32", label: "32位整数" },
        { name: "int64", label: "64位整数" },
        { name: "float32", label: "32位浮点" },
        { name: "float64", label: "64位浮点" },
        { name: "bool", label: "布尔" },
        { name: "string", label: "字符串" }
    ]

    function openFor(id) {
        nodeId = id
        current = app.selectType(id)
        let tIdx = 0
        for (let i = 0; i < typeList.length; ++i) {
            if (typeList[i].name === (current.type || "Int")) {
                tIdx = i
                break
            }
        }
        typeBox.currentIndex = tIdx

        let dIdx = 3 // default float64
        for (let j = 0; j < dtypeList.length; ++j) {
            if (dtypeList[j].name === (current.dtype || "float64")) {
                dIdx = j
                break
            }
        }
        dtypeBox.currentIndex = dIdx
        rankBox.value = Math.max(1, current.rank || 1)
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

    readonly property string selectedTypeName: typeList[typeBox.currentIndex] ? typeList[typeBox.currentIndex].name : "Int"
    readonly property string selectedDtypeName: dtypeList[dtypeBox.currentIndex] ? dtypeList[dtypeBox.currentIndex].name : "float64"
    readonly property int selectedRank: rankBox.value
    readonly property string previewDescription: app.typeDescription(selectedTypeName, selectedTypeName === "Array" ? selectedDtypeName : "", selectedTypeName === "Array" ? selectedRank : -1)

    contentItem: ColumnLayout {
        spacing: 10
        RowLayout {
            spacing: 8
            QuiText { text: "数据类型"; color: Theme.textSecondary }
            ComboBox {
                id: typeBox
                Layout.preferredWidth: 180
                model: root.typeList
                textRole: "label"
            }
        }
        RowLayout {
            spacing: 8
            visible: root.selectedTypeName === "Array"
            QuiText { text: "元素类型"; color: Theme.textSecondary }
            ComboBox {
                id: dtypeBox
                Layout.preferredWidth: 140
                model: root.dtypeList
                textRole: "label"
            }
            QuiText { text: "数组维度"; color: Theme.textSecondary }
            SpinBox { id: rankBox; from: 1; to: 4 }
        }
        RowLayout {
            spacing: 8
            QuiText { text: "类型预览："; font: Qt.font({ bold: true }); color: Theme.textPrimary }
            QuiText { text: root.previewDescription; color: Theme.highlight; font: Qt.font({ bold: true }) }
        }
        QuiText {
            Layout.fillWidth: true
            wrapMode: Text.WordWrap
            color: Theme.textSecondary
            font: Qt.font({ pixelSize: 11 })
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
                enabled: !app.runtimeBusy
                onClicked: {
                    if (app.applySelectType(root.nodeId, root.selectedTypeName,
                                           root.selectedTypeName === "Array" ? root.selectedDtypeName : "",
                                           root.selectedTypeName === "Array" ? root.selectedRank : -1))
                        root.close()
                }
            }
        }
    }
}
