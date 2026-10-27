import QtQuick
import quickui
import VisionRT.Style 1.0

QuiStepperField {
    id: root

    property string valueText: ""
    property bool canDecrement: false
    property bool canIncrement: false
    property var commitValue
    property var stepValue

    property string _lastValue: ""

    implicitHeight: Theme.paramEditorHeight
    canDecrease: enabled && !readOnly && canDecrement
    canIncrease: enabled && !readOnly && canIncrement
    onTextEdited: invalid = false

    function sync() {
        if (inputItem.activeFocus && text !== _lastValue)
            return
        _lastValue = valueText
        text = valueText
        invalid = false
    }

    onValueTextChanged: sync()
    Component.onCompleted: sync()

    onCommitRequested: {
        if (text === valueText) {
            invalid = false
            return
        }
        if (commitValue(text)) {
            _lastValue = text
            invalid = false
        } else {
            invalid = true
        }
    }
    onCancelRequested: {
        text = valueText
        invalid = false
    }
    onDecreaseRequested: {
        _lastValue = text
        if (!stepValue(-1, text))
            invalid = true
    }
    onIncreaseRequested: {
        _lastValue = text
        if (!stepValue(1, text))
            invalid = true
    }
}
