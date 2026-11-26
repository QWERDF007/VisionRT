import QtQuick
import quickui
import VisionRT.Ui 1.0

QuiStepperField {
    id: root
    buttonWidth: implicitHeight
    iconSize: VisionRTSize.IconSize
    contentPadding: 0
    flat: true

    property string valueText: ""
    property string parameterId: ""
    property string parameterKey: ""
    property bool canDecrement: false
    property bool canIncrement: false
    property var commitValue
    property var stepValue

    signal rejected(string text)

    property bool _skipRequest: false
    property bool _dirty: false

    implicitHeight: VisionRTSize.ContentSize
    canDecrease: enabled && !readOnly && canDecrement
    canIncrease: enabled && !readOnly && canIncrement

    RegularExpressionValidator {
        id: intKeystrokeValidator
        regularExpression: /^[+-]?\d{0,19}$/
    }

    function _isValidInt64(raw) {
        const trimmed = String(raw).trim();
        if (!/^[+-]?\d+$/.test(trimmed))
            return false;
        const negative = trimmed.startsWith("-");
        let digits = (trimmed.startsWith("+") || negative) ? trimmed.slice(1) : trimmed;
        digits = digits.replace(/^0+/, "") || "0";
        const limit = negative ? "9223372036854775808" : "9223372036854775807";
        if (digits.length !== limit.length)
            return digits.length < limit.length;
        return digits <= limit;
    }

    function _setTextSilently(value) {
        _skipRequest = true;
        text = value;
        _skipRequest = false;
    }

    function sync(force) {
        if (!force && inputItem.activeFocus && _dirty)
            return;
        _setTextSilently(valueText);
        _dirty = false;
        invalid = false;
    }

    onTextChanged: {
        if (_skipRequest)
            return;
        _dirty = (text !== valueText);
        invalid = false;
    }
    onValueTextChanged: sync(false)
    onParameterKeyChanged: sync(true)
    Component.onCompleted: {
        inputItem.validator = intKeystrokeValidator;
        sync(true);
    }

    onCommitRequested: {
        if (_skipRequest || !enabled || readOnly || inputItem.inputMethodComposing)
            return;
        if (text === valueText) {
            _dirty = false;
            invalid = false;
            return;
        }
        const trimmed = text.trim();
        if (!_isValidInt64(trimmed) || !commitValue || commitValue(trimmed) !== true) {
            rejected(text);
            sync(true);
            return;
        }
        sync(true);
    }
    onCancelRequested: sync(true)
    onDecreaseRequested: {
        if (_skipRequest || !enabled || readOnly || inputItem.inputMethodComposing)
            return;
        const trimmed = text.trim();
        if (!_isValidInt64(trimmed) || !stepValue || stepValue(-1, trimmed) !== true) {
            rejected(text);
            sync(true);
            return;
        }
        sync(true);
    }
    onIncreaseRequested: {
        if (_skipRequest || !enabled || readOnly || inputItem.inputMethodComposing)
            return;
        const trimmed = text.trim();
        if (!_isValidInt64(trimmed) || !stepValue || stepValue(1, trimmed) !== true) {
            rejected(text);
            sync(true);
            return;
        }
        sync(true);
    }
}
