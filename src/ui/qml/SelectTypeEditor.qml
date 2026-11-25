import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import quickui
import VisionRT.Ui 1.0

// Merge type for the host select node. The chosen TypeSpec replaces both branch
// inputs and the output; changing it requires the ports to be unbound first.
Dialog {
    id: root
    property string nodeId: ""
    property var current: ({})
    font: QuiFont.Body
    modal: true
    title: "合流类型"
    parent: Overlay.overlay ? Overlay.overlay : undefined
    x: Math.round(((parent ? parent.width : 0) - width) / 2)
    y: Math.round(((parent ? parent.height : 0) - height) / 2)
    width: 440
    height: 320
    standardButtons: Dialog.NoButton
    padding: 12
    header: QuiText {
        text: root.title
        font: QuiFont.BodyStrong
        color: QuiColor.FontPrimary
        padding: 12
    }

    background: Rectangle {
        color: QuiColor.Primary
        border.color: QuiColor.Border
        border.width: 1
        radius: 6
    }

    readonly property var typeList: app.parameterTypes
    readonly property var dtypeList: app.arrayElementTypes

    function openFor(id) {
        nodeId = id;
        current = app.selectType(id);
        let tIdx = 0;
        for (let i = 0; i < typeList.length; ++i) {
            if (typeList[i].name === (current.type ?? PortType.Int)) {
                tIdx = i;
                break;
            }
        }
        typeBox.currentIndex = tIdx;
        let dIdx = dtypeList.findIndex(function (item) {
            return item.name === DType.Float64;
        });
        for (let j = 0; j < dtypeList.length; ++j) {
            if (dtypeList[j].name === (current.dtype ?? DType.Float64)) {
                dIdx = j;
                break;
            }
        }
        dtypeBox.currentIndex = dIdx;
        rankBox.value = Math.max(1, current.rank || 1);
        open();
    }

    Connections {
        target: app
        function onSelectedNodeChanged() {
            if (root.visible && app.selectedNode !== root.nodeId)
                root.close();
        }
        function onRuntimeBusyChanged() {
            if (root.visible && app.runtimeBusy)
                root.close();
        }
    }

    readonly property int selectedTypeName: typeList[typeBox.currentIndex] ? typeList[typeBox.currentIndex].name : PortType.Int
    readonly property int selectedDtypeName: dtypeList[dtypeBox.currentIndex] ? dtypeList[dtypeBox.currentIndex].name : DType.Float64
    readonly property int selectedRank: rankBox.value
    readonly property string previewDescription: app.typeDescription(selectedTypeName, selectedTypeName === PortType.Array ? selectedDtypeName : -1, selectedTypeName === PortType.Array ? selectedRank : -1)

    contentItem: ColumnLayout {
        spacing: 5
        RowLayout {
            spacing: 5
            QuiText {
                text: "数据类型"
                color: QuiColor.FontCaption
            }
            ParameterComboBox {
                id: typeBox
                Layout.preferredWidth: 180
                model: root.typeList
                textRole: "label"
            }
        }
        RowLayout {
            spacing: 5
            visible: root.selectedTypeName === PortType.Array
            QuiText {
                text: "元素类型"
                color: QuiColor.FontCaption
            }
            ParameterComboBox {
                id: dtypeBox
                Layout.preferredWidth: 140
                model: root.dtypeList
                textRole: "label"
            }
            QuiText {
                text: "数组维度"
                color: QuiColor.FontCaption
            }
            QuiSpinEditor {
                id: rankBox
                minValue: 1
                maxValue: 4
                buttonWidth: VisionRTSize.ContentSize
                iconSize: VisionRTSize.IconSize
            }
        }
        RowLayout {
            spacing: 5
            QuiText {
                text: "类型预览："
                font: QuiFont.BodyStrong
                color: QuiColor.FontPrimary
            }
            QuiText {
                text: root.previewDescription
                color: QuiColor.Highlight
                font: QuiFont.BodyStrong
            }
        }
        QuiText {
            Layout.fillWidth: true
            wrapMode: Text.WordWrap
            color: QuiColor.FontCaption
            font: QuiFont.Caption
            text: "合流的输入与输出使用同一类型。"
        }
        Item {
            Layout.fillHeight: true
        }
        RowLayout {
            Layout.fillWidth: true
            Item {
                Layout.fillWidth: true
            }
            QuiButton {
                text: "取消"
                onClicked: root.close()
            }
            QuiFilledButton {
                text: "应用"
                enabled: !app.runtimeBusy
                onClicked: {
                    if (app.applySelectType(root.nodeId, root.selectedTypeName, root.selectedTypeName === PortType.Array ? root.selectedDtypeName : -1, root.selectedTypeName === PortType.Array ? root.selectedRank : -1))
                        root.close();
                }
            }
        }
    }
}
