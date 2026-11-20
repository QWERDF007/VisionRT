import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import quickui
import VisionRT.Ui 1.0

WorkbenchWindow {
    id: root
    objectName: "customToolWindow"
    title: "自定义工具"
    width: 1280
    height: 720
    property var controller: null
    function openDialog() {
        showAndRaise();
    }
    body: Component {
        Item {
            anchors.fill: parent

            RowLayout {
                id: columns
                anchors.fill: parent
                anchors.leftMargin: 5
                anchors.rightMargin: 5
                anchors.topMargin: 0
                anchors.bottomMargin: 0
                spacing: 5
                readonly property real columnUnit: (width - 6 * spacing - 3) / 5
                readonly property real labelWidth: Math.max(toolInformation.implicitLabelWidth, inputPorts.implicitLabelWidth, outputPorts.implicitLabelWidth)

                ToolCatalogPane {
                    Layout.preferredWidth: 2 * parent.columnUnit
                    Layout.fillHeight: true
                    controller: root.controller
                }

                Rectangle {
                    Layout.fillHeight: true
                    width: 1
                    color: QuiColor.Border
                }

                ToolInformationPane {
                    id: toolInformation
                    Layout.preferredWidth: parent.columnUnit
                    Layout.fillHeight: true
                    controller: root.controller
                    labelWidth: columns.labelWidth
                }

                Rectangle {
                    Layout.fillHeight: true
                    width: 1
                    color: QuiColor.Border
                }

                ToolPortsPane {
                    id: inputPorts
                    Layout.preferredWidth: parent.columnUnit
                    Layout.fillHeight: true
                    portsModel: root.controller && root.controller.hasTool ? root.controller.inputModel : null
                    types: root.controller ? root.controller.supportedTypes : []
                    editable: root.controller && root.controller.hasTool && root.controller.isEditable
                    stage: PortStage.Inputs
                    labelWidth: columns.labelWidth
                }

                Rectangle {
                    Layout.fillHeight: true
                    width: 1
                    color: QuiColor.Border
                }

                ToolPortsPane {
                    id: outputPorts
                    Layout.preferredWidth: parent.columnUnit
                    Layout.fillHeight: true
                    portsModel: root.controller && root.controller.hasTool ? root.controller.outputModel : null
                    types: root.controller ? root.controller.supportedTypes : []
                    editable: root.controller && root.controller.hasTool && root.controller.isEditable
                    stage: PortStage.Outputs
                    labelWidth: columns.labelWidth
                }
            }
        }
    }
    footer: Component {
        RowLayout {
            anchors.fill: parent
            spacing: 5

            Label {
                Layout.fillWidth: true
                elide: Text.ElideRight
                text: (root.controller && root.controller.isStatusError) ? root.controller.statusMessage : ""
                color: QuiColor.Error
                font: QuiFont.Caption
            }

            // 取消按钮
            QuiButton {
                id: cancelBtn
                objectName: "cancelBtn"

                text: "取消"
                onClicked: root.close()
            }

            // 保存按钮
            QuiFilledButton {
                id: saveBtn
                objectName: "saveBtn"

                text: "保存"
                onClicked: {
                    if (root.controller) {
                        root.controller.saveAll();
                        root.controller.cancelAll();
                        root.close();
                    }
                }
            }
        }
    }
    WorkbenchDialog {
        id: discardDialog
        objectName: "discardConfirmation"
        title: "放弃未保存的修改"
        positiveText: "确定"
        negativeText: "取消"
        width: 420

        message: "本窗口有尚未保存的工具与目录修改。放弃后这些修改会全部丢弃，工具目录不会变化。"

        onPositiveClicked: {
            if (root.controller)
                root.controller.cancelAll();
            root.close();
        }
    }
    onClosing: function (event) {
        if (!root.applicationClosing && root.controller && root.controller.isDirty) {
            event.accepted = false;
            discardDialog.open();
            return;
        }
        if (root.controller)
            root.controller.cancelAll();
    }
}
