import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import quickui
import VisionRT.Ui 1.0

Item {
    id: root
    required property var portsModel
    required property var types
    required property int stage
    required property real labelWidth
    property bool editable: false
    readonly property real implicitLabelWidth: {
        let widest = 0;
        for (const card of portView.contentItem.children) {
            if (card instanceof ToolPortCard)
                widest = Math.max(widest, card.implicitLabelWidth);
        }
        return widest;
    }

    ColumnLayout {
        anchors.fill: parent
        anchors.topMargin: 5
        anchors.bottomMargin: 5
        spacing: 5

        RowLayout {
            Layout.fillWidth: true
            Layout.preferredHeight: VisionRTSize.ContentSize
            spacing: 5

            Label {
                text: root.stage === PortStage.Outputs ? "输出参数" : "输入参数"
                font: QuiFont.BodyStrong
                color: QuiColor.FontPrimary
                verticalAlignment: Text.AlignVCenter
            }

            Item {
                Layout.fillWidth: true
            }

            QuiButton {
                text: "+ 参数"
                enabled: root.editable
                onClicked: {
                    root.portsModel.addPort("新参数", {
                        type: PortType.Int
                    }, "0");
                    portView.positionViewAtEnd();
                }
            }
        }

        ListView {
            id: portView
            Layout.fillWidth: true
            Layout.fillHeight: true
            clip: true
            spacing: 5
            topMargin: 0
            model: root.portsModel
            delegate: ToolPortCard {
                width: portView.width
                portsModel: root.portsModel
                listView: portView
                types: root.types
                editable: root.editable
                stage: root.stage
                labelWidth: root.labelWidth
            }
        }
    }
}
