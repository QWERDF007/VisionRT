import QtQuick
import QtQuick.Layouts
import quickui

ColumnLayout {
    id: root
    required property var session
    readonly property var rectangle: session.rectangle || []
    readonly property string sessionKey: String(session.id || "")
    spacing: 4

    function reportReadiness() {
        if (!root.visible || !session.node) return
        let valid = true
        for (let index = 0; index < coordinates.count; ++index) {
            const field = coordinates.itemAt(index)
            if (field && !field.ready) valid = false
        }
        app.setRoiFieldsValid(root.sessionKey, valid)
    }

    Connections {
        target: app
        function onRoiApplyRequested() {
            if (!root.visible || !root.session.node) return
            for (let index = 0; index < coordinates.count; ++index) {
                const field = coordinates.itemAt(index)
                if (field && (!field.invalid || field.isDirty())) field.commitDraft()
            }
            root.reportReadiness()
        }
    }

    QuiButton {
        visible: root.rectangle.length === 0 && (!root.session || root.session.imageWidth <= 0)
        text: "新建矩形"
        onClicked: app.setRoiRectangle(root.sessionKey,
                                     [0, 0, Math.max(1, root.session.imageWidth), Math.max(1, root.session.imageHeight), 0])
    }

    Repeater {
        id: coordinates
        model: root.rectangle.length === 5 ? 5 : 0
        delegate: QuiSpinEditor {
            id: field
            required property int index
            readonly property real coordinate: root.rectangle[index]
            readonly property bool ready: !invalid && !inputItem.inputMethodComposing
                && (!isDirty() || parse(text).valid)
            objectName: "roiCoordinate" + index
            Layout.fillWidth: true
            label: ["X", "Y", "宽度", "高度", "角度"][index]
            labelWidth: 36
            font: Qt.font({ family: "Microsoft YaHei UI", pixelSize: Theme.paramEditorFontSize })
            decimals: 6
            step: index === 4 ? 1 : 0.1
            minValue: index === 2 || index === 3 ? 0 : -Infinity
            maxValue: Infinity
            Component.onCompleted: { applyValue(coordinate); root.reportReadiness() }
            onCoordinateChanged: applyValue(coordinate)
            onReadyChanged: root.reportReadiness()
            commitValue: {
                const controller = app
                const owner = root.sessionKey
                const coordinateIndex = field.index
                return function(number) {
                    const current = controller.roiEditor
                    if (current.id !== owner) return false
                    const updated = current.rectangle.slice()
                    updated[coordinateIndex] = number
                    return controller.setRoiRectangle(owner, updated)
                }
            }
            Connections {
                target: root
                function onSessionKeyChanged() { field.applyValue(field.coordinate) }
            }
        }
    }
}
