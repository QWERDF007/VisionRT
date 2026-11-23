import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import quickui
import VisionRT.Ui 1.0

Rectangle {
    id: root
    property var flows: []
    property var searchResults: []
    property bool workspaceEnabled: false
    property int searchIndex: 0
    signal searchRequested(string text)
    signal locateRequested(var result)
    signal fitRequested
    signal flowSelectionRequested(string id, bool selected)
    signal canvasFocusRequested
    function focusSearch() {
        search.forceActiveFocus();
        search.selectAll();
    }
    function locate(result) {
        search.text = "";
        locateRequested(result);
    }
    implicitHeight: VisionRTSize.BarSize
    color: QuiColor.Primary
    Rectangle {
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.bottom: parent.bottom
        height: 1
        color: QuiColor.Border
    }
    RowLayout {
        anchors.fill: parent
        anchors.leftMargin: 5
        anchors.rightMargin: 5
        spacing: 5
        QuiText {
            objectName: "flowTitle"
            text: "流程图绘制区"
            font: QuiFont.Body
            color: QuiColor.FontPrimary
        }
        Item {
            Layout.fillWidth: true
        }
        Rectangle {
            id: searchBox
            Layout.preferredWidth: Math.min(200, root.width * 0.35)
            Layout.preferredHeight: VisionRTSize.ContentSize
            color: QuiColor.Primary
            radius: 4
            border.color: search.activeFocus ? QuiColor.Highlight : QuiColor.Border
            border.width: 1
            RowLayout {
                anchors.fill: parent
                anchors.leftMargin: 5
                anchors.rightMargin: 5
                spacing: 5
                QuiTextIcon {
                    iconSource: QuiFontIcon.Search
                    iconSize: VisionRTSize.IconSize
                    iconColor: QuiColor.FontCaption
                }
                TextInput {
                    id: search
                    objectName: "flowSearch"
                    Layout.fillWidth: true
                    font: QuiFont.Body
                    color: QuiColor.FontPrimary
                    selectByMouse: true
                    verticalAlignment: Text.AlignVCenter
                    onTextChanged: {
                        root.searchRequested(text);
                        root.searchIndex = 0;
                    }
                    Keys.onDownPressed: root.searchIndex = Math.min(root.searchResults.length - 1, root.searchIndex + 1)
                    Keys.onUpPressed: root.searchIndex = Math.max(0, root.searchIndex - 1)
                    Keys.onReturnPressed: {
                        if (root.searchResults.length)
                            root.locate(root.searchResults[root.searchIndex]);
                    }
                    Keys.onEnterPressed: {
                        if (root.searchResults.length)
                            root.locate(root.searchResults[root.searchIndex]);
                    }
                    Keys.onEscapePressed: {
                        text = "";
                        root.canvasFocusRequested();
                    }
                    Text {
                        anchors.fill: parent
                        text: "搜索工具名"
                        color: QuiColor.FontCaption
                        font: QuiFont.Body
                        visible: !search.text && !search.activeFocus
                        verticalAlignment: Text.AlignVCenter
                    }
                }
                QuiTextIcon {
                    iconSource: QuiFontIcon.Clear
                    iconSize: VisionRTSize.IconSize
                    iconColor: QuiColor.FontCaption
                    visible: search.text.length > 0
                    MouseArea {
                        anchors.fill: parent
                        onClicked: {
                            search.text = "";
                            root.canvasFocusRequested();
                        }
                    }
                }
            }
        }
        Rectangle {
            id: flowDropdown
            objectName: "flowDropdown"
            Layout.preferredWidth: 125
            Layout.preferredHeight: VisionRTSize.ContentSize
            radius: 4
            color: QuiColor.Primary
            border.color: selectFlowPopup.visible ? QuiColor.Highlight : (dropdownMouse.containsMouse ? QuiColor.Highlight : QuiColor.Border)
            border.width: 1
            RowLayout {
                anchors.fill: parent
                anchors.leftMargin: 5
                anchors.rightMargin: 5
                spacing: 5
                QuiText {
                    text: "选择流程:"
                    font: QuiFont.Body
                    color: QuiColor.FontPrimary
                }
                QuiText {
                    Layout.fillWidth: true
                    text: {
                        let count = 0;
                        for (let i = 0; i < root.flows.length; ++i) {
                            if (root.flows[i].selected)
                                count++;
                        }
                        return count + "/" + (root.flows.length || 0);
                    }
                    font: QuiFont.Body
                    color: QuiColor.Highlight
                }
                QuiTextIcon {
                    iconSource: selectFlowPopup.visible ? QuiFontIcon.ChevronUp : QuiFontIcon.ChevronDown
                    iconSize: VisionRTSize.IconSize
                    iconColor: selectFlowPopup.visible ? QuiColor.Highlight : QuiColor.FontCaption
                }
            }
            MouseArea {
                id: dropdownMouse
                anchors.fill: parent
                cursorShape: Qt.PointingHandCursor
                hoverEnabled: true
                onClicked: {
                    if (selectFlowPopup.visible) {
                        selectFlowPopup.close();
                    } else {
                        selectFlowPopup.open();
                    }
                }
            }
            Popup {
                id: selectFlowPopup
                objectName: "selectFlowPopup"
                y: flowDropdown.height + 2
                width: Math.max(flowDropdown.width, 130)
                implicitHeight: Math.min(flowListView.contentHeight + topPadding + bottomPadding, 220)
                padding: 4
                rightPadding: 0
                closePolicy: Popup.CloseOnEscape | Popup.CloseOnPressOutsideParent | Popup.CloseOnPressOutside
                background: Rectangle {
                    radius: 4
                    color: QuiColor.Primary
                    border.color: QuiColor.Border
                    border.width: 1
                    QuiShadow {
                        color: QuiColor.Border
                        radius: 5
                    }
                }
                contentItem: ListView {
                    id: flowListView
                    model: root.flows
                    clip: true
                    implicitHeight: contentHeight
                    spacing: 5
                    ScrollBar.vertical: WorkbenchScrollBar {
                        id: flowScrollBar
                    }
                    delegate: Rectangle {
                        id: flowRow
                        objectName: "flowToggle_" + modelData.id
                        width: flowListView.width - flowScrollBar.width
                        height: VisionRTSize.ContentSize
                        radius: 4
                        color: rowMouse.containsMouse ? QuiColor.Border : "transparent"
                        RowLayout {
                            anchors.fill: parent
                            anchors.leftMargin: 5
                            anchors.rightMargin: 5
                            spacing: 5
                            Rectangle {
                                width: 14
                                height: 14
                                radius: 4
                                color: modelData.selected ? QuiColor.Highlight : QuiColor.Primary
                                border.color: modelData.selected ? QuiColor.Highlight : QuiColor.FontCaption
                                border.width: 1
                                QuiTextIcon {
                                    anchors.centerIn: parent
                                    iconSource: QuiFontIcon.CheckMark
                                    iconSize: VisionRTSize.IconSize
                                    iconColor: QuiColor.Primary
                                    visible: modelData.selected
                                }
                            }
                            QuiText {
                                Layout.fillWidth: true
                                text: modelData.name
                                font: QuiFont.Body
                                color: QuiColor.FontPrimary
                                elide: Text.ElideRight
                            }
                        }
                        MouseArea {
                            id: rowMouse
                            anchors.fill: parent
                            hoverEnabled: true
                            cursorShape: Qt.PointingHandCursor
                            enabled: root.workspaceEnabled
                            onClicked: root.flowSelectionRequested(modelData.id, !modelData.selected)
                        }
                    }
                }
            }
        }
        WorkbenchActionButton {
            objectName: "flowFitButton"
            iconSource: QuiFontIcon.FitPage
            implicitWidth: VisionRTSize.IconButtonSize
            implicitHeight: VisionRTSize.IconButtonSize
            iconColor: hovered ? QuiColor.Highlight : QuiColor.FontPrimary
            text: "适应全部节点"
            onClicked: root.fitRequested()
        }
    }
    Rectangle {
        visible: search.text.length > 0 && search.activeFocus
        z: 20
        x: 4
        y: 36
        width: parent.width - 8
        height: Math.min(280, searchList.contentHeight + 8)
        color: QuiColor.Primary
        border.color: QuiColor.Border
        radius: 4
        ListView {
            id: searchList
            anchors.fill: parent
            anchors.margins: 5
            clip: true
            boundsBehavior: Flickable.StopAtBounds
            boundsMovement: Flickable.StopAtBounds
            model: root.searchResults
            currentIndex: root.searchIndex
            section.property: "flowName"
            section.criteria: ViewSection.FullString
            section.delegate: QuiText {
                required property string section
                text: section
                height: VisionRTSize.ContentSize
                font: QuiFont.Caption
                color: QuiColor.FontCaption
            }
            delegate: QuiItemDelegate {
                required property var modelData
                required property int index
                width: searchList.width
                height: VisionRTSize.ContentSize
                text: modelData.title
                highlighted: index === root.searchIndex
                onClicked: root.locate(modelData)
            }
        }
    }
}
