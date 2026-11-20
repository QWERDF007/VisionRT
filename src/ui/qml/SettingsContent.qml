import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import quickui
import VisionRT.Ui 1.0

RowLayout {
    id: root
    property bool draftBuiltins: false
    property int draftLevel: LogLevel.Info
    property string draftPython: ""
    property int page: SettingsPage.Runtime
    property bool applying: false
    property bool pythonPending: false
    property bool environmentRejected: false
    property string environmentText: ""
    property string errorText: ""
    signal builtinsEdited(bool value)
    signal levelEdited(int value)
    signal pythonEdited(string value)
    signal pageRequested(int page)
    signal browsePythonRequested
    readonly property var levelKeys: [LogLevel.Trace, LogLevel.Debug, LogLevel.Info, LogLevel.Warn, LogLevel.Error, LogLevel.Critical, LogLevel.Off]
    readonly property var levelNames: ["跟踪", "调试", "信息", "警告", "错误", "严重", "关闭"]
    property alias pythonText: pythonField.text
    anchors.fill: parent
    spacing: 0

    Item {
        Layout.preferredWidth: 150
        Layout.fillHeight: true

        Rectangle {
            anchors.right: parent.right
            width: 1
            height: parent.height
            color: QuiColor.Border
        }
        Column {
            anchors.fill: parent
            anchors.margins: 5
            spacing: 5
            Repeater {
                model: [
                    {
                        "key": SettingsPage.Tools,
                        "name": "工具设置",
                        "icon": QuiFontIcon.DeveloperTools
                    },
                    {
                        "key": SettingsPage.Runtime,
                        "name": "运行设置",
                        "icon": QuiFontIcon.Equalizer
                    },
                    {
                        "key": SettingsPage.Logging,
                        "name": "日志设置",
                        "icon": QuiFontIcon.List
                    }
                ]
                delegate: Rectangle {
                    required property var modelData
                    width: parent.width
                    height: VisionRTSize.ButtonSize
                    radius: 4
                    color: root.page === modelData.key ? QuiColor.Highlight : "transparent"
                    RowLayout {
                        anchors.fill: parent
                        anchors.leftMargin: 5
                        anchors.rightMargin: 5
                        spacing: 5
                        QuiTextIcon {
                            iconSource: modelData.icon
                            iconSize: VisionRTSize.IconSize
                            iconColor: root.page === modelData.key ? QuiColor.FontPrimary : QuiColor.FontCaption
                        }
                        QuiText {
                            Layout.fillWidth: true
                            text: modelData.name
                            elide: Text.ElideRight
                            color: root.page === modelData.key ? QuiColor.FontPrimary : QuiColor.FontPrimary
                            font: QuiFont.Body
                        }
                    }
                    MouseArea {
                        anchors.fill: parent
                        cursorShape: Qt.PointingHandCursor
                        onClicked: root.pageRequested(modelData.key)
                    }
                }
            }
        }
    }

    Item {
        Layout.fillWidth: true
        Layout.fillHeight: true
        clip: true

        Column {
            id: form
            anchors.left: parent.left
            anchors.right: parent.right
            anchors.top: parent.top
            anchors.leftMargin: 5
            anchors.rightMargin: 5
            anchors.topMargin: 5
            spacing: 5

            Row {
                spacing: 5
                Rectangle {
                    width: 3
                    height: 16
                    color: QuiColor.Highlight
                    anchors.verticalCenter: parent.verticalCenter
                }
                QuiText {
                    text: root.page === SettingsPage.Tools ? "工具设置" : root.page === SettingsPage.Logging ? "日志设置" : "运行设置"
                    color: QuiColor.FontPrimary
                    font: QuiFont.BodyStrong
                }
            }

            // Tool settings: whether the declaration shipped with the installation is listed
            // by the custom tool window.
            RowLayout {
                width: parent.width
                visible: root.page === SettingsPage.Tools
                spacing: 5
                QuiText {
                    Layout.preferredWidth: VisionRTSize.ParamLabelWidth
                    horizontalAlignment: Text.AlignRight
                    verticalAlignment: Text.AlignVCenter
                    Layout.fillHeight: true
                    text: "显示内置工具"
                    color: QuiColor.FontCaption
                    font: QuiFont.Body
                }
                QuiToggleSwitch {
                    checked: root.draftBuiltins
                    clickListener: function () {
                        root.builtinsEdited(!root.draftBuiltins);
                    }
                }
                Item {
                    Layout.fillWidth: true
                }
            }

            // Runtime settings: the external interpreter, chosen through one file-or-folder
            // picker or typed. Empty means the next start runs without Python.
            Column {
                width: parent.width
                visible: root.page === SettingsPage.Runtime
                spacing: 5
                RowLayout {
                    width: parent.width
                    spacing: 5
                    QuiText {
                        Layout.preferredWidth: VisionRTSize.ParamLabelWidth
                        Layout.alignment: Qt.AlignVCenter
                        horizontalAlignment: Text.AlignRight
                        verticalAlignment: Text.AlignVCenter
                        Layout.fillHeight: true
                        text: "Python 环境"
                        color: QuiColor.FontCaption
                        font: QuiFont.Body
                    }
                    ParameterTextField {
                        id: pythonField
                        Layout.fillWidth: true
                        Layout.maximumWidth: 420
                        flat: false
                        readOnly: root.applying
                        actionIcon: QuiFontIcon.Folder
                        onActionRequested: root.browsePythonRequested()
                        Connections {
                            target: pythonField.inputItem
                            function onTextEdited() {
                                root.pythonEdited(pythonField.text);
                            }
                        }
                    }
                    Item {
                        Layout.fillWidth: true
                    }
                }
                QuiText {
                    x: pythonField.x
                    visible: root.pythonPending && !root.applying
                    text: "Python 环境在下次启动时生效。"
                    color: QuiColor.FontCaption
                    font: QuiFont.Caption
                }
                QuiText {
                    x: pythonField.x
                    visible: root.applying
                    text: "正在检查所选 Python 环境..."
                    color: QuiColor.FontCaption
                    font: QuiFont.Caption
                }
                // One status, read from the host: the check result is never re-derived here.
                QuiText {
                    x: pythonField.x
                    width: parent.width - pythonField.x
                    visible: !root.applying && !root.pythonPending
                    text: "当前环境：" + root.environmentText
                    color: root.environmentRejected ? QuiColor.Error : QuiColor.FontCaption
                    wrapMode: Text.WordWrap
                    font: QuiFont.Caption
                }
            }

            // Log settings: one production threshold for the file and the window.
            RowLayout {
                width: parent.width
                visible: root.page === SettingsPage.Logging
                spacing: 5
                QuiText {
                    Layout.preferredWidth: VisionRTSize.ParamLabelWidth
                    horizontalAlignment: Text.AlignRight
                    verticalAlignment: Text.AlignVCenter
                    Layout.fillHeight: true
                    text: "日志等级"
                    color: QuiColor.FontCaption
                    font: QuiFont.Body
                }
                ParameterComboBox {
                    id: levelBox
                    Layout.preferredWidth: 200
                    model: root.levelNames
                    currentIndex: Math.max(0, root.levelKeys.indexOf(root.draftLevel))
                    onActivated: root.levelEdited(root.levelKeys[currentIndex])
                }
                Item {
                    Layout.fillWidth: true
                }
            }

            QuiText {
                width: parent.width
                visible: root.errorText.length > 0
                text: root.errorText
                color: QuiColor.Error
                wrapMode: Text.WordWrap
                font: QuiFont.Body
            }
        }
    }
}
