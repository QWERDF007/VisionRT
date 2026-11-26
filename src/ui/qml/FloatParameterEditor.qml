import QtQuick
import quickui
import VisionRT.Ui 1.0

QuiStepperField {
    id: root
    buttonWidth: implicitHeight
    iconSize: VisionRTSize.IconSize
    contentPadding: 0
    flat: true
    implicitHeight: VisionRTSize.ContentSize

    property string displayText: ""
    property string editText: ""
    property string parameterId: ""
    property string parameterKey: ""
    property real minimumValue: -Infinity
    property real maximumValue: Infinity
    property real step: 1
    property int decimals: 6
    property var commitValue

    signal rejected(string text)

    property bool _skipRequest: false
    property bool _dirty: false
    property string _focusBaseline: ""

    readonly property string _resolvedEditText: editText.length > 0 ? editText : displayText
    readonly property real _stepSize: Math.abs(step) > 0 ? Math.abs(step) : 1
    readonly property real _backendNumber: {
        const parsed = Number(_resolvedEditText);
        return isFinite(parsed) ? parsed : 0;
    }
    readonly property real _effectiveNumber: {
        const parsed = _parseStrict(text);
        return parsed.valid ? parsed.value : _backendNumber;
    }

    canDecrease: enabled && !readOnly && _effectiveNumber > minimumValue
    canIncrease: enabled && !readOnly && _effectiveNumber < maximumValue

    RegularExpressionValidator {
        id: floatKeystrokeValidator
        regularExpression: /^[+-]?(?:\d*\.?\d*)(?:[eE][+-]?\d*)?$/
    }

    Timer {
        id: selectAllTimer
        interval: 10
        repeat: false
        onTriggered: {
            if (root.inputItem.activeFocus && root.enabled && !root.readOnly && !root.inputItem.inputMethodComposing)
                root.inputItem.selectAll();
        }
    }

    function _parseStrict(raw) {
        const trimmed = String(raw).trim();
        if (!/^[+-]?(?:\d+(?:\.\d*)?|\.\d+)(?:[eE][+-]?\d+)?$/.test(trimmed))
            return { valid: false, value: 0 };
        const value = Number(trimmed);
        if (!isFinite(value))
            return { valid: false, value: 0 };
        if (value < minimumValue || value > maximumValue)
            return { valid: false, value: 0 };
        return { valid: true, value: value };
    }

    function _roundStep(value) {
        const precision = Math.max(Math.min(Math.max(decimals, 0), 15), 12);
        const rounded = Number(value.toFixed(precision));
        return Object.is(rounded, -0) ? 0 : rounded;
    }

    function _setTextSilently(value) {
        _skipRequest = true;
        text = value;
        _skipRequest = false;
    }

    function sync(force) {
        if (!force && inputItem.activeFocus && _dirty)
            return;
        const target = inputItem.activeFocus ? _resolvedEditText : displayText;
        _setTextSilently(target);
        _focusBaseline = inputItem.activeFocus ? target : "";
        _dirty = false;
        invalid = false;
    }

    function _stepBy(direction) {
        if (_skipRequest || !enabled || readOnly || inputItem.inputMethodComposing)
            return;
        const parsed = _parseStrict(text);
        if (!parsed.valid) {
            rejected(text);
            sync(true);
            return;
        }
        let next = parsed.value + direction * _stepSize;
        if (!isFinite(next)) {
            rejected(text);
            sync(true);
            return;
        }
        next = Math.max(minimumValue, Math.min(maximumValue, _roundStep(next)));
        if (next === _backendNumber) {
            sync(true);
            return;
        }
        const serialized = String(next);
        if (commitValue && commitValue(serialized) === true) {
            sync(true);
        } else {
            rejected(serialized);
            sync(true);
        }
    }

    Connections {
        target: root.inputItem
        function onActiveFocusChanged() {
            if (root.inputItem.activeFocus) {
                if (!root._dirty) {
                    root._setTextSilently(root._resolvedEditText);
                    root._focusBaseline = root.text;
                }
                selectAllTimer.restart();
            } else {
                selectAllTimer.stop();
                if (!root._dirty) {
                    root._setTextSilently(root.displayText);
                    root._focusBaseline = "";
                }
            }
        }
    }

    onTextChanged: {
        if (_skipRequest)
            return;
        _dirty = (text !== _focusBaseline && text !== displayText && text !== _resolvedEditText);
        invalid = false;
    }

    onDisplayTextChanged: sync(false)
    onEditTextChanged: sync(false)
    onParameterKeyChanged: sync(true)

    Component.onCompleted: {
        inputItem.validator = floatKeystrokeValidator;
        sync(true);
    }

    onCommitRequested: {
        if (_skipRequest || !enabled || readOnly || inputItem.inputMethodComposing)
            return;
        if (!_dirty) {
            invalid = false;
            return;
        }
        const parsed = _parseStrict(text);
        if (!parsed.valid) {
            rejected(text);
            sync(true);
            return;
        }
        const trimmed = text.trim();
        if (commitValue && commitValue(trimmed) === true) {
            sync(true);
        } else {
            rejected(text);
            sync(true);
        }
    }

    onCancelRequested: sync(true)
    onDecreaseRequested: _stepBy(-1)
    onIncreaseRequested: _stepBy(1)
}
