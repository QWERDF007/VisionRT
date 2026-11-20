import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import quickui
import VisionRT.Ui 1.0

ColumnLayout {
    id: root
    required property var controller
    required property real labelWidth
    readonly property alias implicitLabelWidth: informationForm.implicitLabelWidth
    readonly property bool editable: controller && controller.isEditable
    readonly property string symbolLabel: !controller ? "函数名:" : controller.currentToolKind === ToolKind.PythonClass ? "类名:" : controller.currentToolKind === ToolKind.Cpp ? "工厂名:" : "函数名:"
    spacing: 5
    Layout.topMargin: 5

    QuiText {
        Layout.fillWidth: true
        Layout.preferredHeight: VisionRTSize.ContentSize
        verticalAlignment: Text.AlignVCenter
        text: "工具"
        font: QuiFont.BodyStrong
    }
    Rectangle {
        Layout.fillWidth: true
        Layout.fillHeight: true
        Layout.bottomMargin: 5
        radius: 4
        color: QuiColor.Primary
        border.width: 1
        border.color: root.controller && root.controller.hasTool ? QuiColor.Highlight : QuiColor.Border
        ToolFormLayout {
            id: informationForm
            anchors.left: parent.left
            anchors.right: parent.right
            anchors.top: parent.top
            anchors.margins: 5
            ToolFormRow {
                labelWidth: root.labelWidth
                label: "中文名:"
                ParameterTextField {
                    Layout.fillWidth: true
                    flat: false
                    text: root.controller ? root.controller.currentToolName : ""
                    readOnly: !root.editable
                    onCommitRequested: root.controller.currentToolName = text
                    onCancelRequested: text = root.controller.currentToolName
                }
            }
            ToolFormRow {
                labelWidth: root.labelWidth
                label: "英文名:"
                ParameterTextField {
                    Layout.fillWidth: true
                    flat: false
                    text: root.controller ? root.controller.currentToolId : ""
                    readOnly: !(root.controller && root.controller.isToolIdEditable)
                    onCommitRequested: root.controller.currentToolId = text
                    onCancelRequested: text = root.controller.currentToolId
                }
            }
            ToolFormRow {
                labelWidth: root.labelWidth
                label: "注册接口:"
                ParameterComboBox {
                    Layout.fillWidth: true
                    model: ["Python 函数", "Python 类", "C++ 注册工厂"]
                    currentIndex: !root.controller ? 0 : root.controller.currentToolKind === ToolKind.PythonClass ? 1 : root.controller.currentToolKind === ToolKind.Cpp ? 2 : 0
                }
            }
            ToolFormRow {
                labelWidth: root.labelWidth
                label: "脚本文件:"
                ParameterTextField {
                    Layout.fillWidth: true
                    flat: false
                    text: root.controller ? root.controller.currentToolFile : ""
                    readOnly: !root.editable
                    onCommitRequested: root.controller.currentToolFile = text
                    onCancelRequested: text = root.controller.currentToolFile
                }
            }
            ToolFormRow {
                labelWidth: root.labelWidth
                label: root.symbolLabel
                ParameterTextField {
                    Layout.fillWidth: true
                    flat: false
                    text: root.controller ? root.controller.currentToolSymbol : ""
                    readOnly: !root.editable
                    onCommitRequested: root.controller.currentToolSymbol = text
                    onCancelRequested: text = root.controller.currentToolSymbol
                }
            }
            ToolFormRow {
                labelWidth: root.labelWidth
                label: "版本:"
                ParameterTextField {
                    Layout.fillWidth: true
                    flat: false
                    text: root.controller ? root.controller.currentToolVersion : ""
                    readOnly: !root.editable
                    onCommitRequested: root.controller.currentToolVersion = text
                    onCancelRequested: text = root.controller.currentToolVersion
                }
            }
        }
    }
}
