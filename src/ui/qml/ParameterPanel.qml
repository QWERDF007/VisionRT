import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import quickui
import VisionRT.Ui 1.0

QuiSplitView {
    id: root
    objectName: "parameterPanel"
    orientation: Qt.Horizontal
    backgroundColor: QuiColor.Border
    handleColor: QuiColor.Border
    handle: Rectangle {
        implicitWidth: 1
        implicitHeight: root.height
        color: SplitHandle.hovered || SplitHandle.pressed ? QuiColor.Highlight : QuiColor.Border
        containmentMask: Item {
            x: -3
            width: 7
            height: root.height
        }
    }
    property int currentTab: ParameterTab.BasicInput
    readonly property var tabs: app.parameterTabs
    property var collapsedGroups: ({})
    property var parameterGroups: []
    property real inputsRatio: 0.52
    function resetSplit() {
        inputsRatio = 0.52;
        let w = root.width > 200 ? root.width : 980;
        inputs.SplitView.preferredWidth = Math.round(w * inputsRatio);
        outputPanel.SplitView.preferredWidth = Math.round(w * (1.0 - inputsRatio));
    }
    onWidthChanged: {
        if (root.width > 200) {
            inputs.SplitView.preferredWidth = Math.round(root.width * inputsRatio);
            outputPanel.SplitView.preferredWidth = Math.round(root.width * (1.0 - inputsRatio));
        }
    }
    Component.onCompleted: {
        refreshGroups();
        Qt.callLater(resetSplit);
    }
    onCurrentTabChanged: refreshGroups()
    Connections {
        target: app
        function onSelectionChanged() { root.refreshGroups(); }
        function onTrainingChanged() { root.refreshGroups(); }
        function onParametersChanged() {
            root.refreshGroups();
        }
    }
    function isSameGroups(a, b) {
        if (a === b)
            return true;
        if (!a || !b || a.length !== b.length)
            return false;
        for (let i = 0; i < a.length; ++i) {
            let ga = a[i], gb = b[i];
            if (ga.nodeId !== gb.nodeId || ga.name !== gb.name || ga.tab !== gb.tab)
                return false;
            if (!ga.parameters || !gb.parameters || ga.parameters.length !== gb.parameters.length)
                return false;
            for (let j = 0; j < ga.parameters.length; ++j) {
                if (ga.parameters[j].id !== gb.parameters[j].id || ga.parameters[j].stage !== gb.parameters[j].stage || ga.parameters[j].recordId !== gb.parameters[j].recordId)
                    return false;
            }
        }
        return true;
    }
    function refreshGroups() {
        if (currentTab === ParameterTab.Training && !app.trainingAvailable) currentTab = ParameterTab.BasicInput;
        let result = app.parameterGroups.filter(function (group) { return group.tab === currentTab; });
        if (!isSameGroups(result, parameterGroups)) parameterGroups = result;
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
                root.inputsRatio = Math.max(0.30, Math.min(0.70, inputs.width / root.width));
            }
        }
        ColumnLayout {
            anchors.fill: parent
            spacing: 0
            Rectangle {
                Layout.fillWidth: true
                Layout.preferredHeight: VisionRTSize.BarSize
                color: QuiColor.Primary
                Rectangle {
                    anchors.left: parent.left
                    anchors.right: parent.right
                    anchors.bottom: parent.bottom
                    height: 2
                    color: QuiColor.Border
                }
                RowLayout {
                    anchors.fill: parent
                    spacing: 0
                    Repeater {
                        model: root.tabs
                        WorkbenchTabButton {
                            id: parameterTab
                            clip: true
                            required property var modelData
                            required property int index
                            Layout.fillWidth: true
                            Layout.fillHeight: true
                            text: modelData.name
                            checked: root.currentTab === modelData.id
                            contentItem: QuiText {
                                text: parameterTab.text
                                font: parameterTab.font
                                color: parameterTab.textColor
                                horizontalAlignment: Text.AlignHCenter
                                verticalAlignment: Text.AlignVCenter
                                opacity: parameterTab.enabled ? 1.0 : 0.4
                            }
                            onClicked: root.currentTab = modelData.id
                        }
                    }
                    WorkbenchActionButton {
                        objectName: "executeNodeButton"
                        Layout.preferredWidth: 80
                        Layout.alignment: Qt.AlignVCenter
                        Layout.leftMargin: 5
                        Layout.rightMargin: 5
                        text: "执行"
                        iconSource: QuiFontIcon.Play
                        display: AbstractButton.TextBesideIcon
                        iconColor: QuiColor.Highlight
                        enabled: !app.runtimeBusy && app.selectedNode.length > 0
                        onClicked: app.runNode()
                    }
                }
            }
            Item {
                Layout.fillWidth: true
                Layout.fillHeight: true
                Flickable {
                    id: scroller
                    anchors.fill: parent
                    visible: app.selectedNode.length > 0
                    contentWidth: width
                    contentHeight: groupColumn.height
                    clip: true
                    boundsBehavior: Flickable.StopAtBounds
                    boundsMovement: Flickable.StopAtBounds
                    ScrollBar.vertical: WorkbenchScrollBar {
                        id: parameterScrollBar
                    }
                    Column {
                        id: groupColumn
                        width: scroller.width - parameterScrollBar.width
                        spacing: 5
                        Repeater {
                            model: root.parameterGroups
                            QuiExpander {
                                id: parameterGroup
                                required property var modelData
                                width: groupColumn.width
                                headerText: modelData.name
                                headerHeight: VisionRTSize.BarSize
                                headerPadding: 5
                                headerDelegate: Component {
                                    RowLayout {
                                        anchors.fill: parent
                                        spacing: 5
                                        WorkbenchActionButton {
                                            iconSource: parameterGroup.expand ? QuiFontIcon.ChevronDown : QuiFontIcon.ChevronRight
                                            onClicked: parameterGroup.toggle()
                                        }
                                        QuiText {
                                            text: parameterGroup.headerText
                                        }
                                        Item {
                                            Layout.fillWidth: true
                                        }
                                    }
                                }

                                expand: root.collapsedGroups[headerText] !== true
                                contentHeight: groupBody.implicitHeight + 2 * groupBody.anchors.topMargin
                                onExpandChanged: {
                                    root.collapsedGroups[headerText] = !expand;
                                }
                                ColumnLayout {
                                    id: groupBody
                                    anchors.top: parent.top
                                    anchors.topMargin: 5
                                    anchors.left: parent.left
                                    anchors.leftMargin: 5
                                    anchors.right: parent.right
                                    anchors.rightMargin: 5
                                    spacing: 5
                                    Repeater {
                                        model: modelData.parameters
                                        Loader {
                                            id: parameterRow
                                            required property var modelData
                                            readonly property var currentParameter: parameterGroup.modelData.nodeId === app.selectedNode ? app.parameters.find(function (parameter) {
                                                return parameter.id === parameterRow.modelData.id && parameter.stage === parameterRow.modelData.stage && parameter.recordId === parameterRow.modelData.recordId;
                                            }) : undefined
                                            Layout.fillWidth: true
                                            active: currentParameter !== undefined
                                            sourceComponent: ParameterEditor {
                                                nodeId: parameterGroup.modelData.nodeId
                                                parameter: parameterRow.currentParameter
                                                onExpressionEditorRequested: function (node, draft, hasDraft) {
                                                    root.openExpressionEditor(node, draft, hasDraft);
                                                }
                                                onSelectTypeEditorRequested: function (node) {
                                                    root.openSelectTypeEditor(node);
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
    }
    OutputPanel {
        id: outputPanel
        SplitView.fillWidth: true
        SplitView.minimumWidth: 280
        SplitView.preferredWidth: 480
    }
    Loader {
        id: expressionEditorLoader
        active: false
        source: "ExpressionEditor.qml"
    }
    Loader {
        id: selectTypeEditorLoader
        active: false
        source: "SelectTypeEditor.qml"
    }

    function openExpressionEditor(nodeId, draftText, hasDraft) {
        if (!expressionEditorLoader.active)
            expressionEditorLoader.active = true;
        let invoke = function () {
            if (!expressionEditorLoader.item)
                return;
            // Edit always starts from the row's current draft. A cleared field opens an empty draft
            // instead of reloading the committed formula.
            if (hasDraft === true)
                expressionEditorLoader.item.openNodeWithDraft(nodeId, draftText === undefined ? "" : draftText, true);
            else
                expressionEditorLoader.item.openNode(nodeId);
        };
        if (expressionEditorLoader.item)
            invoke();
        else
            Qt.callLater(invoke);
    }

    function openSelectTypeEditor(nodeId) {
        if (!selectTypeEditorLoader.active)
            selectTypeEditorLoader.active = true;
        if (selectTypeEditorLoader.item)
            selectTypeEditorLoader.item.openFor(nodeId);
        else
            Qt.callLater(function () {
                if (selectTypeEditorLoader.item)
                    selectTypeEditorLoader.item.openFor(nodeId);
            });
    }
}
