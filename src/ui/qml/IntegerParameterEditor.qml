import QtQuick
import quickui
import VisionRT.Style 1.0

QuiStepperField {
    id: root

    property string valueText: ""
    property string parameterId: ""
    property bool canDecrement: false
    property bool canIncrement: false
    property var commitValue
    property var stepValue

    // A rejected draft is reported to whoever owns the value; this control has no global dependency.
    signal rejected(string text)

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
        // A rejected draft stays in the field for the user to correct; validation is reported to
        // the owner of the value instead of painting the row red.
        if (commitValue(text)) {
            _lastValue = text
            invalid = false
        } else {
            rejected(text)
            invalid = false
        }
    }
    onCancelRequested: {
        text = valueText
        invalid = false
    }
    onDecreaseRequested: {
        _lastValue = text
        stepValue(-1, text)
    }
    onIncreaseRequested: {
        _lastValue = text
        stepValue(1, text)
    }
}
