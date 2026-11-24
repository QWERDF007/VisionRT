import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import quickui
import VisionRT.Ui 1.0

Rectangle {
    id: root
    objectName: "imageToolbar"
    required property QtObject controller
    signal fitRequested
    signal zoomRequested(real factor)
    signal oneToOneRequested

    implicitWidth: VisionRTSize.BarSize
    color: QuiColor.Primary
    Rectangle {
        anchors.top: parent.top
        anchors.bottom: parent.bottom
        anchors.right: parent.right
        width: 1
        color: QuiColor.Border
    }
    Flickable {
        id: imageToolbar
        anchors.fill: parent
        contentWidth: width
        contentHeight: toolbarColumn.implicitHeight + 2 * 5
        clip: true
        boundsBehavior: Flickable.StopAtBounds
        ScrollBar.vertical: WorkbenchScrollBar {
        }
        ColumnLayout {
            id: toolbarColumn
            objectName: "viewportActions"
            y: 5
            width: imageToolbar.width
            spacing: 5
            enabled: root.controller.workspaceEnabled
            WorkbenchActionButton {
                text: "适应窗口"
                iconSource: QuiFontIcon.FitPage
                Layout.alignment: Qt.AlignHCenter
                iconColor: hovered ? QuiColor.Highlight : QuiColor.FontPrimary
                onClicked: root.fitRequested()
            }
            WorkbenchActionButton {
                text: "放大"
                iconSource: QuiFontIcon.ZoomIn
                Layout.alignment: Qt.AlignHCenter
                iconColor: hovered ? QuiColor.Highlight : QuiColor.FontPrimary
                onClicked: root.zoomRequested(1.25)
            }
            WorkbenchActionButton {
                text: "缩小"
                iconSource: QuiFontIcon.ZoomOut
                Layout.alignment: Qt.AlignHCenter
                iconColor: hovered ? QuiColor.Highlight : QuiColor.FontPrimary
                onClicked: root.zoomRequested(0.8)
            }
            WorkbenchSeparator {
                orientation: Qt.Horizontal
                Layout.alignment: Qt.AlignHCenter
            }
            // Smart point selection: the button reports the user preference only. Availability
            // and pausing are explained in the tooltip, never by silently unchecking it.
            WorkbenchActionButton {
                objectName: "easyTouchToggle"
                text: root.controller.easyTouchEnabled ? (root.controller.assistAvailable ? "智能点选（EasyTouch）：已开启" : "智能点选（EasyTouch）：已开启，" + (root.controller.assistDisabledReason || "当前不可用")) : "智能点选（EasyTouch）：已关闭"
                iconSource: QuiFontIcon.Search
                Layout.alignment: Qt.AlignHCenter
                checkable: false
                checked: root.controller.easyTouchEnabled
                normalColor: checked ? QuiColor.Highlight : QuiColor.Button
                hoverColor: checked ? Qt.lighter(QuiColor.Highlight, 1.15) : QuiColor.ItemHover
                pressedColor: checked ? Qt.darker(QuiColor.Highlight, 1.15) : QuiColor.ItemPress
                iconColor: checked ? QuiColor.White : (hovered ? QuiColor.Highlight : QuiColor.FontPrimary)
                Accessible.name: "智能点选（EasyTouch）"
                Accessible.role: Accessible.CheckBox
                Accessible.checkable: true
                Accessible.checked: root.controller.easyTouchEnabled
                onClicked: root.controller.setEasyTouchEnabled(!root.controller.easyTouchEnabled)
            }
            WorkbenchSeparator {
                orientation: Qt.Horizontal
                Layout.alignment: Qt.AlignHCenter
            }
            WorkbenchActionButton {
                text: "1:1 像素显示"
                iconSource: QuiFontIcon.Photo2
                Layout.alignment: Qt.AlignHCenter
                iconColor: hovered ? QuiColor.Highlight : QuiColor.FontPrimary
                onClicked: root.oneToOneRequested()
            }
        }
    }
}
