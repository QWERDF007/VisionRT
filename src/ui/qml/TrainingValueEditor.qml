import QtQuick
import QtQuick.Layouts
import quickui
import VisionRT.Ui 1.0

Item {
    id: root
    required property var field
    property font font: QuiFont.Body
    property var commitValue: null
    signal commitRequested(var value)
    signal rejected(string text)
    implicitHeight: VisionRTSize.ContentSize

    readonly property bool isBooleanField: root.field.editor === EditorKind.Switch || root.field.editor === EditorKind.Bool

    RegularExpressionValidator {
        id: intValidator
        regularExpression: /^[+-]?\d{0,19}$/
    }

    RegularExpressionValidator {
        id: floatValidator
        regularExpression: /^[+-]?(?:\d*\.?\d*)(?:[eE][+-]?\d*)?$/
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

    function _isValidFloat(raw) {
        const trimmed = String(raw).trim();
        if (!/^[+-]?(?:\d+(?:\.\d*)?|\.\d+)(?:[eE][+-]?\d+)?$/.test(trimmed))
            return false;
        return isFinite(Number(trimmed));
    }

    function _submit(value) {
        if (commitValue)
            return commitValue(value) === true;
        commitRequested(value);
        return true;
    }

    WorkbenchTextField {
        id: textField
        anchors.fill: parent
        font: root.font
        visible: !root.isBooleanField
        Accessible.name: root.field.name
        validator: root.field.type === PortType.Int ? intValidator : (root.field.type === PortType.Float ? floatValidator : null)

        property bool skipRequest: false
        property string committedText: ""

        function sync(force) {
            const backend = root.field.value === undefined ? "" : String(root.field.value);
            committedText = backend;
            if (force || !activeFocus) {
                skipRequest = true;
                text = backend;
                skipRequest = false;
            }
        }

        Component.onCompleted: sync(true)
        onEditingFinished: {
            if (skipRequest || inputMethodComposing)
                return;
            if (text === committedText)
                return;
            const value = text;
            const valid = root.field.type === PortType.Int ? root._isValidInt64(value)
                : root.field.type === PortType.Float ? root._isValidFloat(value) : true;
            if (!valid || !root._submit(value)) {
                root.rejected(value);
                sync(true);
                return;
            }
            sync(true);
        }
        Keys.onEscapePressed: function (event) {
            if (inputMethodComposing) {
                event.accepted = false;
                return;
            }
            sync(true);
            event.accepted = true;
        }
    }

    QuiToggleSwitch {
        id: toggleSwitch
        anchors.left: parent.left
        anchors.verticalCenter: parent.verticalCenter
        visible: root.isBooleanField
        Accessible.name: root.field.name

        property bool skipRequest: false

        function syncChecked() {
            skipRequest = true;
            checked = !!root.field.value;
            skipRequest = false;
        }

        Component.onCompleted: syncChecked()
        clickListener: function () {
            if (skipRequest)
                return;
            if (!root._submit(!root.field.value))
                syncChecked();
        }
    }

    onFieldChanged: {
        textField.sync(false);
        toggleSwitch.syncChecked();
    }
}
