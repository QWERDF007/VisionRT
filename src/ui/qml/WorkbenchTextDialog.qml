import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import QtQuick.Window
import quickui

WorkbenchDialog {
    id: root
    property string text: ""
    property string description: ""
    property bool monospace: false
    width: Math.min(900, parent ? parent.width - 40 : 900)
    useNegativeButton: false
    positiveText: "关闭"
    contentDelegate: Component {
        ColumnLayout {
            implicitHeight: Math.min(520, Window.window ? Window.window.height - 170 : 520)
            QuiText {
                visible: root.description.length > 0
                text: root.description
                textFormat: Text.PlainText
                wrapMode: Text.Wrap
                Layout.fillWidth: true
                color: QuiColor.FontCaption
            }
            ScrollView {
                Layout.fillWidth: true
                Layout.fillHeight: true
                QuiTextArea {
                    text: root.text
                    textFormat: TextEdit.PlainText
                    readOnly: true
                    selectByMouse: true
                    wrapMode: root.monospace ? TextEdit.NoWrap : TextEdit.WrapAnywhere
                    font.family: root.monospace ? "Consolas" : "Microsoft YaHei UI"
                }
            }
        }
    }
}
