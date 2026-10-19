import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import quickui

QuiSplitView {
    id: root
    orientation: Qt.Horizontal
    backgroundColor: "#d0d7de"
    handleColor: "#d0d7de"
    handle: Rectangle {
        implicitWidth: 1
        implicitHeight: root.height
        color: SplitHandle.hovered || SplitHandle.pressed ? QuiColor.Highlight : "#d0d7de"
        containmentMask: Item {
            x: -3; width: 7; height: root.height
        }
    }
    property int currentTab: 0
    readonly property var tabs: ["基本输入", "预处理", "检测", "结果处理"]
    property var collapsedGroups: ({})
    property real inputsRatio: 0.52
    function resetSplit() {
        inputsRatio = 0.52
        let w = root.width > 200 ? root.width : 980
        inputs.SplitView.preferredWidth = Math.round(w * inputsRatio)
        outputPanel.SplitView.preferredWidth = Math.round(w * (1.0 - inputsRatio))
    }
    onWidthChanged: {
        if (root.width > 200) {
            inputs.SplitView.preferredWidth = Math.round(root.width * inputsRatio)
            outputPanel.SplitView.preferredWidth = Math.round(root.width * (1.0 - inputsRatio))
        }
    }
    Component.onCompleted: Qt.callLater(resetSplit)
    function groups() {
        let result = [], byName = ({})
        for (let p of app.parameters) {
            if ((p.tab || "基本输入") !== tabs[currentTab]) continue
            let name = p.init ? "初始化配置" + (p.group ? " · " + p.group : "") : p.group || "输入参数"
            if (byName[name] === undefined) { byName[name] = result.length; result.push({ name: name, init: p.init, parameters: [] }) }
            result[byName[name]].parameters.push(p)
        }
        return result
    }
    function focusRoi() {
        for (let p of app.parameters) {
            if (p.type === "RoiBatch" && !p.init) {
                currentTab = Math.max(0, tabs.indexOf(p.tab || "基本输入"))
                roiPicker.open(); return
            }
        }
        roiNotice.open()
    }
    Rectangle {
        id: inputs
        color: QuiColor.Primary
        border.width: 0
        SplitView.fillWidth: true
        SplitView.minimumWidth: 320
        SplitView.preferredWidth: 520
        onWidthChanged: {
            if (root.width > 200 && inputs.SplitHandle.pressed) {
                root.inputsRatio = Math.max(0.30, Math.min(0.70, inputs.width / root.width))
            }
        }
        ColumnLayout {
            anchors.fill: parent; spacing: 0
            Rectangle {
                Layout.fillWidth: true; Layout.preferredHeight: 34; color: Theme.tabBarBg
                Rectangle {
                    anchors.left: parent.left; anchors.right: parent.right; anchors.bottom: parent.bottom
                    height: 1; color: Theme.tabBorder
                }
                RowLayout {
                    anchors.fill: parent; spacing: 0
                    Repeater {
                        model: root.tabs
                        Rectangle {
                            id: tabItem
                            required property string modelData; required property int index
                            readonly property bool isActive: root.currentTab === index
                            readonly property bool isHovered: tabMouse.containsMouse
                            Layout.fillWidth: true; Layout.fillHeight: true
                            color: isActive ? Theme.tabActiveBg : (isHovered ? Theme.paramRowHoverBg : Theme.tabBg)
                            Rectangle {
                                anchors.top: parent.top; anchors.bottom: parent.bottom; anchors.right: parent.right
                                width: 1; color: Theme.tabBorder
                            }
                            Rectangle {
                                visible: !tabItem.isActive
                                anchors.left: parent.left; anchors.right: parent.right; anchors.bottom: parent.bottom
                                height: 1; color: Theme.tabBorder
                            }
                            Rectangle {
                                visible: tabItem.isActive
                                anchors.left: parent.left; anchors.right: parent.right; anchors.bottom: parent.bottom
                                height: 2; color: Theme.tabActiveIndicator
                            }
                            QuiText {
                                anchors.centerIn: parent
                                text: tabItem.modelData
                                font: Qt.font({ family: "Microsoft YaHei UI", pixelSize: Theme.tabFontSize, bold: tabItem.isActive })
                                color: tabItem.isActive ? Theme.tabActiveText : (tabItem.isHovered ? Theme.tabHoverText : Theme.tabText)
                            }
                            MouseArea {
                                id: tabMouse
                                anchors.fill: parent; cursorShape: Qt.PointingHandCursor
                                hoverEnabled: true
                                onClicked: root.currentTab = tabItem.index
                            }
                        }
                    }
                    Rectangle {
                        Layout.preferredWidth: Theme.executeButtonWidth
                        Layout.preferredHeight: Theme.executeButtonHeight
                        Layout.margins: 4; Layout.rightMargin: 8
                        radius: Theme.radiusBase
                        color: runMouse.pressed ? Theme.executeButtonPressedBg : (runMouse.containsMouse ? Theme.executeButtonHoverBg : Theme.executeButtonNormalBg)
                        enabled: !app.busy && app.selectedNode.length > 0
                        opacity: enabled ? 1.0 : 0.4
                        RowLayout {
                            anchors.centerIn: parent; spacing: 5
                            Rectangle {
                                width: 16; height: 16; radius: 8
                                color: "transparent"
                                border.color: Theme.executeButtonIcon; border.width: 1.5
                                QuiTextIcon {
                                    anchors.centerIn: parent
                                    anchors.horizontalCenterOffset: 0.8
                                    iconSource: QuiFontIcon.Play
                                    iconSize: 8
                                    iconColor: Theme.executeButtonIcon
                                }
                            }
                            QuiText {
                                text: "执行"
                                font: Qt.font({ family: "Microsoft YaHei UI", pixelSize: Theme.executeButtonFontSize, bold: true })
                                color: Theme.executeButtonText
                            }
                        }
                        MouseArea {
                            id: runMouse; anchors.fill: parent; hoverEnabled: true
                            cursorShape: Qt.PointingHandCursor
                            onClicked: app.runNode()
                        }
                    }
                }
            }
            Item {
                Layout.fillWidth: true; Layout.fillHeight: true
                Flickable {
                    id: scroller
                    anchors.fill: parent; visible: app.selectedNode.length > 0
                    contentWidth: width; contentHeight: groupColumn.height
                    clip: true; boundsBehavior: Flickable.StopAtBounds
                    ScrollBar.vertical: QuiScrollBar {}
                    Column {
                        id: groupColumn; width: scroller.width; spacing: 4
                        Repeater {
                            model: root.groups()
                            QuiExpander {
                                required property var modelData
                                width: groupColumn.width
                                headerText: modelData.name; headerHeight: 28
                                expand: root.collapsedGroups[headerText] !== true
                                contentHeight: groupBody.implicitHeight + 8
                                onExpandChanged: { root.collapsedGroups[headerText] = !expand }
                                ColumnLayout {
                                    id: groupBody; anchors.left: parent.left; anchors.right: parent.right; anchors.margins: 8; spacing: 4
                                    QuiText { text: "下次执行重建资源"; visible: modelData.init; font: QuiFont.Caption; color: Theme.warning }
                                    Repeater {
                                        model: modelData.parameters
                                        ParameterEditor { required property var modelData; parameter: modelData; Layout.fillWidth: true }
                                    }
                                }
                            }
                        }
                    }
                }
            }
        }
    }
    OutputPanel {
        id: outputPanel
        SplitView.fillWidth: true
        SplitView.minimumWidth: 280
        SplitView.preferredWidth: 480
    }
    QuiPopup {
        id: roiPicker; width: Math.min(420, parent ? parent.width - 32 : 420); height: Math.min(320, roiList.contentHeight + 54)
        closePolicy: Popup.CloseOnEscape | Popup.CloseOnPressOutside
        ColumnLayout {
            anchors.fill: parent; anchors.margins: 12
            QuiText { text: "选择输入 ROI"; font: QuiFont.BodyStrong }
            ListView {
                id: roiList; Layout.fillWidth: true; Layout.fillHeight: true; clip: true
                model: app.parameters.filter(function(p) { return p.type === "RoiBatch" && !p.init })
                delegate: QuiItemDelegate {
                    required property var modelData
                    width: roiList.width; text: modelData.name + (modelData.mode === "upstream" ? " · 复制当前值为用户输入" : "")
                    enabled: !app.busy
                    onClicked: {
                        if (modelData.mode === "upstream") { roiConfirm.parameter = modelData.id; roiConfirm.open() }
                        else if (app.editRoi(modelData.id, false)) roiPicker.close()
                    }
                }
            }
        }
    }
    WorkbenchDialog {
        id: roiConfirm; property string parameter: ""; title: "复制上层 ROI？"
        message: "断开此输入绑定，将当前 ROI 复制为用户输入后编辑；源节点保持不变。"
        onPositiveClickListener: function() { if (app.editRoi(parameter, true)) { roiConfirm.close(); roiPicker.close() } }
    }
    WorkbenchDialog { id: roiNotice; title: "输入 ROI 编辑"; message: "请选择具有 RoiBatch 输入的节点，然后选择要编辑的参数。"; useNegativeButton: false; positiveText: "关闭" }
}
