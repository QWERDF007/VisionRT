import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import quickui
import VisionRT.Style 1.0

WorkbenchWindow {
    id: root
    objectName: "customToolWindow"
    title: "自定义工具"
    width: 1280
    height: 760
    minimumWidth: 1056
    minimumHeight: 632
    property var controller: null
    readonly property string symbolLabel: !controller ? "函数名:" : controller.currentToolKind === "python_class" ? "类名:" : controller.currentToolKind === "cpp" ? "工厂名:" : "函数名:"
    function openDialog() {
        showAndRaise();
    }
    body: Component {
        Item {
            anchors.fill: parent

            RowLayout {
                anchors.fill: parent
                anchors.leftMargin: 5
                anchors.rightMargin: 5
                anchors.topMargin: 0
                anchors.bottomMargin: 0
                spacing: 5

                ToolCatalogPane {
                    Layout.preferredWidth: 230
                    Layout.fillHeight: true
                    controller: root.controller
                }

                Rectangle {
                    Layout.fillHeight: true
                    width: 1
                    color: QuiColor.Border
                }

                Item {
                    Layout.preferredWidth: 260
                    Layout.fillHeight: true

                    ColumnLayout {
                        anchors.fill: parent
                        anchors.topMargin: 5
                        anchors.bottomMargin: 5
                        spacing: 5

                        RowLayout {
                            Layout.fillWidth: true
                            Layout.preferredHeight: 32
                            Label {
                                text: "工具"
                                font: QuiFont.BodyStrong
                                color: QuiColor.FontPrimary
                                verticalAlignment: Text.AlignVCenter
                            }
                            Item {
                                Layout.fillWidth: true
                            }
                        }

                        // Card with primary highlight border when tool is selected
                        Rectangle {
                            Layout.fillWidth: true
                            Layout.fillHeight: true
                            radius: 8
                            color: QuiColor.Button
                            border.color: (root.controller && root.controller.hasTool) ? QuiColor.Highlight : QuiColor.Border
                            border.width: 1.5

                            ColumnLayout {
                                anchors.fill: parent
                                anchors.margins: 5
                                spacing: 5

                                // 中文名
                                RowLayout {
                                    Layout.fillWidth: true
                                    Layout.fillHeight: false
                                    spacing: 5
                                    Label {
                                        text: "中文名:"
                                        Layout.preferredWidth: 64
                                        horizontalAlignment: Text.AlignRight
                                        verticalAlignment: Text.AlignVCenter
                                        Layout.fillHeight: true
                                        font: QuiFont.Body
                                        color: QuiColor.FontDark
                                    }
                                    ParameterTextField {
                                        Layout.fillWidth: true
                                        flat: false
                                        text: root.controller ? root.controller.currentToolName : ""
                                        readOnly: !(root.controller && root.controller.isEditable)
                                        onCommitRequested: if (root.controller)
                                            root.controller.currentToolName = text
                                        onCancelRequested: text = root.controller ? root.controller.currentToolName : ""
                                    }
                                }

                                // 英文名
                                RowLayout {
                                    Layout.fillWidth: true
                                    Layout.fillHeight: false
                                    spacing: 5
                                    Label {
                                        text: "英文名:"
                                        Layout.preferredWidth: 64
                                        horizontalAlignment: Text.AlignRight
                                        verticalAlignment: Text.AlignVCenter
                                        Layout.fillHeight: true
                                        font: QuiFont.Body
                                        color: QuiColor.FontDark
                                    }
                                    ParameterTextField {
                                        Layout.fillWidth: true
                                        flat: false
                                        text: root.controller ? root.controller.currentToolId : ""
                                        readOnly: !(root.controller && root.controller.isToolIdEditable)
                                        onCommitRequested: if (root.controller)
                                            root.controller.currentToolId = text
                                        onCancelRequested: text = root.controller ? root.controller.currentToolId : ""
                                    }
                                }

                                // 注册接口 (下拉框)
                                RowLayout {
                                    Layout.fillWidth: true
                                    Layout.fillHeight: false
                                    spacing: 5
                                    Label {
                                        text: "注册接口:"
                                        Layout.preferredWidth: 64
                                        horizontalAlignment: Text.AlignRight
                                        verticalAlignment: Text.AlignVCenter
                                        Layout.fillHeight: true
                                        font: QuiFont.Body
                                        color: QuiColor.FontDark
                                    }
                                    ParameterComboBox {
                                        id: regKindCombo
                                        Layout.fillWidth: true
                                        normalColor: QuiColor.Button
                                        disableColor: QuiColor.Button
                                        background: Rectangle {
                                            radius: 3
                                            border.width: 1
                                            border.color: regKindCombo.activeFocus ? QuiColor.Highlight : QuiColor.Border
                                            color: QuiColor.Button
                                        }
                                        model: ["Python 函数", "Python 类", "C++ 注册工厂"]
                                        currentIndex: {
                                            if (!root.controller)
                                                return 0;
                                            if (root.controller.currentToolKind === "python_class")
                                                return 1;
                                            if (root.controller.currentToolKind === "cpp")
                                                return 2;
                                            return 0;
                                        }
                                    }
                                }

                                // 脚本文件
                                RowLayout {
                                    Layout.fillWidth: true
                                    Layout.fillHeight: false
                                    spacing: 5
                                    Label {
                                        text: "脚本文件:"
                                        Layout.preferredWidth: 64
                                        horizontalAlignment: Text.AlignRight
                                        verticalAlignment: Text.AlignVCenter
                                        Layout.fillHeight: true
                                        font: QuiFont.Body
                                        color: QuiColor.FontDark
                                    }
                                    ParameterTextField {
                                        Layout.fillWidth: true
                                        flat: false
                                        text: root.controller ? root.controller.currentToolFile : ""
                                        readOnly: !(root.controller && root.controller.isEditable)
                                        onCommitRequested: if (root.controller)
                                            root.controller.currentToolFile = text
                                        onCancelRequested: text = root.controller ? root.controller.currentToolFile : ""
                                    }
                                }

                                // 函数名 / 类名 / 工厂名
                                RowLayout {
                                    Layout.fillWidth: true
                                    Layout.fillHeight: false
                                    spacing: 5
                                    Label {
                                        text: root.symbolLabel
                                        Layout.preferredWidth: 64
                                        horizontalAlignment: Text.AlignRight
                                        verticalAlignment: Text.AlignVCenter
                                        Layout.fillHeight: true
                                        font: QuiFont.Body
                                        color: QuiColor.FontDark
                                    }
                                    ParameterTextField {
                                        Layout.fillWidth: true
                                        flat: false
                                        text: root.controller ? root.controller.currentToolSymbol : ""
                                        readOnly: !(root.controller && root.controller.isEditable)
                                        onCommitRequested: if (root.controller)
                                            root.controller.currentToolSymbol = text
                                        onCancelRequested: text = root.controller ? root.controller.currentToolSymbol : ""
                                    }
                                }

                                // 版本
                                RowLayout {
                                    Layout.fillWidth: true
                                    Layout.fillHeight: false
                                    spacing: 5
                                    Label {
                                        text: "版本:"
                                        Layout.preferredWidth: 64
                                        horizontalAlignment: Text.AlignRight
                                        verticalAlignment: Text.AlignVCenter
                                        Layout.fillHeight: true
                                        font: QuiFont.Body
                                        color: QuiColor.FontDark
                                    }
                                    ParameterTextField {
                                        Layout.fillWidth: true
                                        flat: false
                                        text: root.controller ? root.controller.currentToolVersion : ""
                                        readOnly: !(root.controller && root.controller.isEditable)
                                        onCommitRequested: if (root.controller)
                                            root.controller.currentToolVersion = text
                                        onCancelRequested: text = root.controller ? root.controller.currentToolVersion : ""
                                    }
                                }

                                Item {
                                    Layout.fillHeight: true
                                }
                                Item {
                                    Layout.fillHeight: true
                                }
                            }
                        }
                    }
                }

                Rectangle {
                    Layout.fillHeight: true
                    width: 1
                    color: QuiColor.Border
                }

                Item {
                    Layout.fillWidth: true
                    Layout.fillHeight: true
                    Layout.minimumWidth: 260

                    ColumnLayout {
                        anchors.fill: parent
                        anchors.topMargin: 5
                        anchors.bottomMargin: 5
                        spacing: 5

                        RowLayout {
                            Layout.fillWidth: true
                            Layout.preferredHeight: 32
                            spacing: 5

                            Label {
                                text: "输入参数"
                                font: QuiFont.BodyStrong
                                color: QuiColor.FontPrimary
                                verticalAlignment: Text.AlignVCenter
                            }

                            Item {
                                Layout.fillWidth: true
                            }

                            QuiButton {
                                text: "+ 参数"
                                enabled: root.controller && root.controller.hasTool && root.controller.isEditable
                                onClicked: {
                                    root.controller.inputModel.addPort("新参数", "int", "0");
                                    inputView.positionViewAtEnd();
                                }
                            }
                        }

                        ListView {
                            id: inputView
                            Layout.fillWidth: true
                            Layout.fillHeight: true
                            clip: true
                            spacing: 5
                            topMargin: 0
                            model: (root.controller && root.controller.hasTool) ? root.controller.inputModel : null
                            delegate: ToolPortCard {
                                width: inputView.width
                                portsModel: root.controller ? root.controller.inputModel : null
                                listView: inputView
                                types: root.controller ? root.controller.supportedTypes : []
                                editable: root.controller && root.controller.isEditable
                                stage: "inputs"
                            }
                        }
                    }
                }

                Rectangle {
                    Layout.fillHeight: true
                    width: 1
                    color: QuiColor.Border
                }

                Item {
                    Layout.fillWidth: true
                    Layout.fillHeight: true
                    Layout.minimumWidth: 260

                    ColumnLayout {
                        anchors.fill: parent
                        anchors.topMargin: 5
                        anchors.bottomMargin: 5
                        spacing: 5

                        RowLayout {
                            Layout.fillWidth: true
                            Layout.preferredHeight: 32
                            spacing: 5

                            Label {
                                text: "输出参数"
                                font: QuiFont.BodyStrong
                                color: QuiColor.FontPrimary
                                verticalAlignment: Text.AlignVCenter
                            }

                            Item {
                                Layout.fillWidth: true
                            }

                            QuiButton {
                                text: "+ 参数"
                                enabled: root.controller && root.controller.hasTool && root.controller.isEditable
                                onClicked: {
                                    root.controller.outputModel.addPort("新参数", "int", "");
                                    outputView.positionViewAtEnd();
                                }
                            }
                        }

                        ListView {
                            id: outputView
                            Layout.fillWidth: true
                            Layout.fillHeight: true
                            clip: true
                            spacing: 5
                            topMargin: 0
                            model: (root.controller && root.controller.hasTool) ? root.controller.outputModel : null
                            delegate: ToolPortCard {
                                width: outputView.width
                                portsModel: root.controller ? root.controller.outputModel : null
                                listView: outputView
                                types: root.controller ? root.controller.supportedTypes : []
                                editable: root.controller && root.controller.isEditable
                                stage: "outputs"
                            }
                        }
                    }
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
