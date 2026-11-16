import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import quickui
import VisionRT.Style 1.0

Rectangle {
    id: root
    required property QtObject controller
    signal fitRequested
    signal zoomRequested(real factor)
    signal oneToOneRequested
    signal roiRequested

    implicitWidth: Theme.toolbarWidth
    color: Theme.surfaceBg
    Rectangle {
        anchors.top: parent.top
        anchors.bottom: parent.bottom
        anchors.right: parent.right
        width: Theme.separatorWidth
        color: QuiColor.Border
    }
    Flickable {
        id: imageToolbar
        anchors.fill: parent
        contentWidth: width
        contentHeight: toolbarColumn.implicitHeight + 8
        clip: true
        boundsBehavior: Flickable.StopAtBounds
        ScrollBar.vertical: QuiScrollBar {
        }
        ColumnLayout {
            id: toolbarColumn
            width: imageToolbar.width
            spacing: 4
            enabled: root.controller.workspaceEnabled
            QuiTextIconButton {
                text: "适应窗口"
                iconSource: QuiFontIcon.FitPage
                Layout.alignment: Qt.AlignHCenter
                implicitWidth: Theme.toolbarButtonSize
                implicitHeight: Theme.toolbarButtonSize
                radius: Theme.radiusSmall
                display: AbstractButton.IconOnly
                normalColor: Theme.menuButtonBg
                hoverColor: Theme.buttonHoverBg
                pressedColor: Theme.buttonPressedBg
                iconColor: hovered ? Theme.highlight : Theme.menuButtonIcon
                onClicked: root.fitRequested()
            }
            QuiTextIconButton {
                text: "放大"
                iconSource: QuiFontIcon.ZoomIn
                Layout.alignment: Qt.AlignHCenter
                implicitWidth: Theme.toolbarButtonSize
                implicitHeight: Theme.toolbarButtonSize
                radius: Theme.radiusSmall
                display: AbstractButton.IconOnly
                normalColor: Theme.menuButtonBg
                hoverColor: Theme.buttonHoverBg
                pressedColor: Theme.buttonPressedBg
                iconColor: hovered ? Theme.highlight : Theme.menuButtonIcon
                onClicked: root.zoomRequested(1.25)
            }
            QuiTextIconButton {
                text: "缩小"
                iconSource: QuiFontIcon.ZoomOut
                Layout.alignment: Qt.AlignHCenter
                implicitWidth: Theme.toolbarButtonSize
                implicitHeight: Theme.toolbarButtonSize
                radius: Theme.radiusSmall
                display: AbstractButton.IconOnly
                normalColor: Theme.menuButtonBg
                hoverColor: Theme.buttonHoverBg
                pressedColor: Theme.buttonPressedBg
                iconColor: hovered ? Theme.highlight : Theme.menuButtonIcon
                onClicked: root.zoomRequested(0.8)
            }
            Rectangle {
                Layout.preferredWidth: 20
                Layout.preferredHeight: Theme.separatorWidth
                color: QuiColor.Border
                Layout.alignment: Qt.AlignHCenter
            }
            Repeater {
                model: [
                    {
                        shape: 2,
                        label: "矩形",
                        icon: QuiFontIcon.RectangularClipping
                    },
                    {
                        shape: 4,
                        label: "多边形",
                        icon: QuiFontIcon.FreeFormClipping
                    },
                    {
                        shape: 3,
                        label: "圆形",
                        icon: QuiFontIcon.CircleRing
                    },
                    {
                        shape: 1,
                        label: "线",
                        icon: QuiFontIcon.Remove
                    },
                    {
                        shape: 5,
                        label: "点",
                        icon: QuiFontIcon.MapPin
                    }
                ]
                delegate: QuiTextIconButton {
                    required property var modelData
                    implicitWidth: Theme.toolbarButtonSize
                    implicitHeight: Theme.toolbarButtonSize
                    Layout.alignment: Qt.AlignHCenter
                    display: AbstractButton.IconOnly
                    text: modelData.label
                    radius: Theme.radiusSmall
                    checkable: false
                    checked: root.controller.drawShape === modelData.shape
                    enabled: root.controller.shapeToolsAvailable && (!root.controller.trainingMode || modelData.shape === 2)
                    opacity: enabled ? 1.0 : 0.4
                    normalColor: checked ? QuiColor.ItemCheck : "transparent"
                    hoverColor: Theme.buttonHoverBg
                    pressedColor: Theme.buttonPressedBg
                    iconColor: checked || hovered ? Theme.highlight : Theme.menuButtonIcon
                    iconSource: modelData.icon
                    iconSize: Theme.paramRoiIconSize
                    onClicked: root.controller.setDrawShape(root.controller.drawShape === modelData.shape ? 0 : modelData.shape)
                    Accessible.name: "绘制" + modelData.label
                    Accessible.role: Accessible.RadioButton
                    Accessible.checkable: true
                    Accessible.checked: checked
                }
            }
            Rectangle {
                Layout.preferredWidth: 20
                Layout.preferredHeight: Theme.separatorWidth
                color: QuiColor.Border
                Layout.alignment: Qt.AlignHCenter
            }
            // Smart point selection: the button reports the user preference only. Availability
            // and pausing are explained in the tooltip, never by silently unchecking it.
            QuiTextIconButton {
                objectName: "easyTouchToggle"
                text: root.controller.easyTouchEnabled ? (root.controller.assistAvailable ? "智能点选（EasyTouch）：已开启" : "智能点选（EasyTouch）：已开启，" + (root.controller.assistDisabledReason || "当前不可用")) : "智能点选（EasyTouch）：已关闭"
                iconSource: QuiFontIcon.Search
                implicitWidth: Theme.toolbarButtonSize
                implicitHeight: Theme.toolbarButtonSize
                Layout.alignment: Qt.AlignHCenter
                display: AbstractButton.IconOnly
                radius: Theme.radiusSmall
                checkable: false
                checked: root.controller.easyTouchEnabled
                normalColor: checked ? Theme.sourceButtonActiveBackground : Theme.menuButtonBg
                hoverColor: checked ? Qt.lighter(Theme.sourceButtonActiveBackground, 1.15) : Theme.buttonHoverBg
                pressedColor: checked ? Qt.darker(Theme.sourceButtonActiveBackground, 1.15) : Theme.buttonPressedBg
                iconColor: checked ? Theme.sourceButtonActiveIcon : (hovered ? Theme.highlight : Theme.menuButtonIcon)
                Accessible.name: "智能点选（EasyTouch）"
                Accessible.role: Accessible.CheckBox
                Accessible.checkable: true
                Accessible.checked: root.controller.easyTouchEnabled
                onClicked: root.controller.setEasyTouchEnabled(!root.controller.easyTouchEnabled)
            }
            Rectangle {
                Layout.preferredWidth: 20
                Layout.preferredHeight: Theme.separatorWidth
                color: QuiColor.Border
                Layout.alignment: Qt.AlignHCenter
            }
            QuiTextIconButton {
                text: "编辑输入 ROI"
                iconSource: QuiFontIcon.Crop
                implicitWidth: Theme.toolbarButtonSize
                implicitHeight: Theme.toolbarButtonSize
                Layout.alignment: Qt.AlignHCenter
                radius: Theme.radiusSmall
                display: AbstractButton.IconOnly
                normalColor: Theme.menuButtonBg
                hoverColor: Theme.buttonHoverBg
                pressedColor: Theme.buttonPressedBg
                iconColor: enabled ? (hovered ? Theme.highlight : Theme.menuButtonIcon) : Theme.disabledText
                enabled: !root.controller.runtimeBusy && root.controller.selectedNode.length > 0 && !root.controller.trainingMode
                opacity: enabled ? 1.0 : 0.4
                Accessible.name: "编辑输入 ROI"
                onClicked: root.roiRequested()
            }
            QuiTextIconButton {
                text: "1:1 像素显示"
                iconSource: QuiFontIcon.Photo2
                implicitWidth: Theme.toolbarButtonSize
                implicitHeight: Theme.toolbarButtonSize
                Layout.alignment: Qt.AlignHCenter
                radius: Theme.radiusSmall
                display: AbstractButton.IconOnly
                normalColor: Theme.menuButtonBg
                hoverColor: Theme.buttonHoverBg
                pressedColor: Theme.buttonPressedBg
                iconColor: hovered ? Theme.highlight : Theme.menuButtonIcon
                onClicked: root.oneToOneRequested()
            }
        }
    }
}
