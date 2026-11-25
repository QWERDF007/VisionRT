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
    function refreshGroups() {
        if (!tabs.some(function (tab) { return tab.id === currentTab; })) currentTab = ParameterTab.BasicInput;
    }
    onTabsChanged: refreshGroups()
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
                Loader {
                    anchors.fill: parent
                    sourceComponent: root.currentTab === ParameterTab.Initialization ? initializationPage
                                   : root.currentTab === ParameterTab.Training ? trainingPage : inputPage
                }
                Component {
                    id: initializationPage
                    InitializationParameterPage {
                        anchors.fill: parent
                        collapsedGroups: root.collapsedGroups
                    }
                }
                Component {
                    id: trainingPage
                    TrainingParameterPage {
                        anchors.fill: parent
                        collapsedGroups: root.collapsedGroups
                    }
                }
                Component {
                    id: inputPage
                    InputParameterPage {
                        anchors.fill: parent
                        parameterTab: root.currentTab
                        collapsedGroups: root.collapsedGroups
                        onExpressionEditorRequested: function (node, draft, hasDraft) {
                            root.openExpressionEditor(node, draft, hasDraft);
                        }
                        onSelectTypeEditorRequested: function (node) { root.openSelectTypeEditor(node); }
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
