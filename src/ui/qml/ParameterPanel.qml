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
    readonly property var tabs: app.parameterTabs
    property var collapsedGroups: ({})
    property var parameterGroups: []
    property string roiNodeId: ""
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
    Component.onCompleted: { refreshGroups(); Qt.callLater(resetSplit) }
    onCurrentTabChanged: refreshGroups()
    Connections {
        target: app
        function onParametersChanged() { root.refreshGroups() }
    }
    function isSameGroups(a, b) {
        if (a === b) return true
        if (!a || !b || a.length !== b.length) return false
        for (let i = 0; i < a.length; ++i) {
            let ga = a[i], gb = b[i]
            if (ga.nodeId !== gb.nodeId || ga.name !== gb.name || ga.init !== gb.init) return false
            if (!ga.parameters || !gb.parameters || ga.parameters.length !== gb.parameters.length) return false
            for (let j = 0; j < ga.parameters.length; ++j) {
                if (ga.parameters[j].id !== gb.parameters[j].id || ga.parameters[j].init !== gb.parameters[j].init) return false
            }
        }
        return true
    }
    function refreshGroups() {
        let result = [], byName = ({})
        for (let p of app.parameters) {
            if ((p.tab || "基本输入") !== tabs[currentTab]) continue
            let name = p.init ? "初始化配置" + (p.group ? " · " + p.group : "") : p.group || "输入参数"
            if (byName[name] === undefined) { byName[name] = result.length; result.push({ nodeId: app.selectedNode, name: name, init: p.init, parameters: [] }) }
            result[byName[name]].parameters.push({ id: p.id, init: p.init })
        }
        if (!isSameGroups(result, parameterGroups))
            parameterGroups = result
    }
    function focusRoi() {
        if (!root.enabled || app.busy) return
        roiNodeId = app.selectedNode
        for (let p of app.parameters) {
            if ((p.type === "RoiBatch" || p.type === "Rect") && !p.init) {
                currentTab = Math.max(0, tabs.indexOf(p.tab || "基本输入"))
                app.startRoiDraw(p.id, 2)
                return
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
                            model: root.parameterGroups
                            QuiExpander {
                                id: parameterGroup
                                required property var modelData
                                width: groupColumn.width
                                headerText: modelData.name; headerHeight: Theme.paramGroupHeaderHeight
                                expand: root.collapsedGroups[headerText] !== true
                                contentHeight: groupBody.implicitHeight + 16
                                onExpandChanged: { root.collapsedGroups[headerText] = !expand }
                                ColumnLayout {
                                    id: groupBody
                                    anchors.top: parent.top
                                    anchors.topMargin: 8
                                    anchors.left: parent.left
                                    anchors.leftMargin: 8
                                    anchors.right: parent.right
                                    anchors.rightMargin: 8
                                    spacing: 6
                                    QuiText { text: "下次执行重建资源"; visible: modelData.init; font: QuiFont.Caption; color: Theme.warning }
                                    Repeater {
                                        model: modelData.parameters
                                        Loader {
                                            id: parameterRow
                                            required property var modelData
                                            readonly property var currentParameter: parameterGroup.modelData.nodeId === app.selectedNode
                                                ? app.parameters.find(function(parameter) {
                                                    return parameter.id === parameterRow.modelData.id && parameter.init === parameterRow.modelData.init
                                                }) : undefined
                                            Layout.fillWidth: true
                                            active: currentParameter !== undefined
                                            sourceComponent: ParameterEditor {
                                                nodeId: parameterGroup.modelData.nodeId
                                                parameter: parameterRow.currentParameter
                                            }
                                        }
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
    WorkbenchDialog { id: roiNotice; title: "输入 ROI 编辑"; message: "请选择具有区域输入的节点，然后选择要编辑的参数。"; useNegativeButton: false; positiveText: "关闭" }
}
