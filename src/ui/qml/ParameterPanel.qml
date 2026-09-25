import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import quickui

QuiSplitView {
    id: root
    orientation: Qt.Horizontal
    backgroundColor: QuiColor.Border; handleColor: "#adb8c6"
    property int currentTab: 0
    readonly property var tabs: ["基本输入", "预处理", "检测", "结果处理"]
    property var collapsedGroups: ({})
    function resetSplit() {
        let w = root.width > 200 ? root.width : 980
        inputs.SplitView.preferredWidth = Math.round(w * 0.52)
        outputPanel.SplitView.preferredWidth = Math.round(w * 0.48)
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
        SplitView.fillWidth: true
        SplitView.minimumWidth: 320
        SplitView.preferredWidth: 520
        color: QuiColor.Primary
        ColumnLayout {
            anchors.fill: parent; spacing: 0
            RowLayout {
                Layout.fillWidth: true; Layout.preferredHeight: 32; spacing: 0
                Repeater {
                    model: root.tabs
                    QuiTabButton {
                        required property string modelData; required property int index
                        text: modelData; checked: root.currentTab === index; Layout.fillWidth: true
                        implicitWidth: 60; implicitHeight: 32; font: QuiFont.Body
                        showIndicator: true
                        normalColor: "#f5f7fa"; checkedColor: QuiColor.Primary
                        normalTextColor: QuiColor.FontPrimary; checkedTextColor: QuiColor.Highlight
                        onClicked: root.currentTab = index
                    }
                }
                QuiTextIconButton { text: "执行当前工具"; iconSource: QuiFontIcon.Play; enabled: !app.busy && app.selectedNode.length > 0; onClicked: app.runNode() }
            }
            QuiText {
                Layout.fillWidth: true; Layout.leftMargin: 8; Layout.preferredHeight: app.selectedNode ? 24 : 0
                visible: app.selectedNode.length > 0
                text: app.parameters.length ? app.parameters[0].title || "" : ""
                color: QuiColor.FontCaption; font: QuiFont.Caption; elide: Text.ElideRight
            }
            Flickable {
                id: scroller
                Layout.fillWidth: true; Layout.fillHeight: true; contentWidth: width; contentHeight: groupColumn.height
                clip: true; boundsBehavior: Flickable.StopAtBounds
                ScrollBar.vertical: QuiScrollBar {}
                Column {
                    id: groupColumn; width: scroller.width; spacing: 4
                    Repeater {
                        model: root.groups()
                        QuiExpander {
                            required property var modelData
                            width: groupColumn.width
                            headerText: modelData.name; headerHeight: 30
                            expand: root.collapsedGroups[headerText] !== true
                            contentHeight: groupBody.implicitHeight + 10
                            onExpandChanged: { let values = root.collapsedGroups; values[headerText] = !expand; root.collapsedGroups = values }
                            ColumnLayout {
                                id: groupBody; anchors.left: parent.left; anchors.right: parent.right; anchors.margins: 8; spacing: 4
                                QuiText { text: "下次执行重建资源"; visible: modelData.init; font: QuiFont.Caption; color: "#ab751d" }
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
