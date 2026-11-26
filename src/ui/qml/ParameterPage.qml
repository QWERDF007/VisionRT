import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import quickui
import VisionRT.Ui 1.0

Item {
    id: root
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
                if (ga.parameters[j].id !== gb.parameters[j].id || ga.parameters[j].stage !== gb.parameters[j].stage || ga.parameters[j].recordId !== gb.parameters[j].recordId || !!ga.parameters[j].recordScoped !== !!gb.parameters[j].recordScoped || ga.parameters[j].datasetId !== gb.parameters[j].datasetId)
                    return false;
            }
        }
        return true;
    }
    required property int parameterTab
    property var collapsedGroups: ({})
    property var parameterGroups: []
    signal expressionEditorRequested(string node, string draft, bool hasDraft)
    signal selectTypeEditorRequested(string node)
    onParameterTabChanged: refreshGroups()
    Component.onCompleted: refreshGroups()
    Connections {
        target: app
        function onSelectionChanged() { root.refreshGroups(); }
    }
    Connections {
        target: app.parameterModel
        function onGroupsChanged() { root.refreshGroups(); }
    }
    function refreshGroups() {
        let result = app.parameterModel.groupsForTab(root.parameterTab);
        if (!isSameGroups(result, parameterGroups)) parameterGroups = result;
    }
    visible: app.selectedNode.length > 0
    Flickable {
        id: parameterFlickable
        anchors.fill: parent
        anchors.topMargin: VisionRTSize.Spacing
        anchors.bottomMargin: VisionRTSize.Spacing
        contentWidth: width
        contentHeight: groupColumn.height
        clip: true
        boundsBehavior: Flickable.StopAtBounds
        boundsMovement: Flickable.StopAtBounds
        ScrollBar.vertical: WorkbenchScrollBar {
            id: parameterScrollBar
            parent: root
            anchors.top: parent.top
            anchors.bottom: parent.bottom
            anchors.right: parent.right
        }
        Column {
            id: groupColumn
            x: 10
            width: parameterFlickable.width - 20
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
                        anchors.right: parent.right
                        spacing: 5
                        Repeater {
                            model: modelData.parameters
                            Loader {
                                id: parameterRow
                                required property var modelData
                                readonly property string rowKey: modelData.key || ""
                                property var currentParameter: parameterGroup.modelData.nodeId === app.selectedNode
                                                               ? app.parameterModel.rowByKey(rowKey)
                                                               : ({})
                                Connections {
                                    target: app.parameterModel
                                    function onDataChanged(topLeft, bottomRight) {
                                        if (parameterGroup.modelData.nodeId !== app.selectedNode)
                                            return;
                                        const idx = app.parameterModel.rowIndexOf(parameterRow.rowKey);
                                        if (idx >= topLeft.row && idx <= bottomRight.row)
                                            parameterRow.currentParameter = app.parameterModel.rowByKey(parameterRow.rowKey);
                                    }
                                    function onModelReset() {
                                        parameterRow.currentParameter = parameterGroup.modelData.nodeId === app.selectedNode
                                                                        ? app.parameterModel.rowByKey(parameterRow.rowKey)
                                                                        : ({});
                                    }
                                }
                                Layout.fillWidth: true
                                active: currentParameter !== undefined && currentParameter !== null && Object.keys(currentParameter).length > 0
                                sourceComponent: ParameterEditor {
                                    nodeId: parameterGroup.modelData.nodeId
                                    parameter: parameterRow.currentParameter
                                    onExpressionEditorRequested: function (node, draft, hasDraft) {
                                        root.expressionEditorRequested(node, draft, hasDraft);
                                    }
                                    onSelectTypeEditorRequested: function (node) {
                                        root.selectTypeEditorRequested(node);
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
