import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import quickui

RowLayout {
    id: root
    property string value: ""
    property bool integer: true
    property string minimum: ""
    property string maximum: ""
    property string step: "1"
    property int decimals: 6
    property var commitValue
    property bool invalid: false
    spacing: 2
    function signedAdd(left, right) {
        function normalize(s) { let n = s[0] === "-"; s = s.replace(/^[+-]/, "").replace(/^0+(?=\d)/, ""); return { negative: n && s !== "0", digits: s } }
        let a = normalize(left), b = normalize(right), out = "", carry = 0
        if (a.negative === b.negative) {
            let i = a.digits.length - 1, j = b.digits.length - 1
            while (i >= 0 || j >= 0 || carry) {
                let n = (i >= 0 ? a.digits.charCodeAt(i--) - 48 : 0) + (j >= 0 ? b.digits.charCodeAt(j--) - 48 : 0) + carry
                out = String(n % 10) + out; carry = Math.floor(n / 10)
            }
            return (a.negative ? "-" : "") + out
        }
        let greater = a.digits.length !== b.digits.length ? a.digits.length > b.digits.length : a.digits >= b.digits
        let large = greater ? a : b, small = greater ? b : a, j = small.digits.length - 1
        for (let i = large.digits.length - 1; i >= 0; --i) {
            let n = large.digits.charCodeAt(i) - 48 - (j >= 0 ? small.digits.charCodeAt(j--) - 48 : 0) - carry
            carry = n < 0 ? 1 : 0; out = String((n + 10) % 10) + out
        }
        out = out.replace(/^0+(?=\d)/, "")
        return (large.negative && out !== "0" ? "-" : "") + out
    }
    function commit(text) {
        if (text === value) { invalid = false; return }
        invalid = !commitValue(text)
    }
    function increment(direction) {
        let next
        if (integer) {
            if (!/^[+-]?\d+$/.test(field.text.trim()) || !/^\+?\d+$/.test(step || "1")) { invalid = true; return }
            next = signedAdd(field.text.trim(), (direction < 0 ? "-" : "") + (step || "1").replace(/^\+/, ""))
        } else {
            let number = Number(field.text), amount = Number(step || "1")
            if (!Number.isFinite(number) || !Number.isFinite(amount)) { invalid = true; return }
            next = (number + direction * amount).toFixed(Math.max(0, Math.min(15, decimals)))
        }
        field.text = next; commit(next)
    }
    onValueChanged: { field.text = value; invalid = false }
    QuiTextField {
        id: field
        Layout.fillWidth: true; Layout.minimumWidth: 60; implicitHeight: 32
        text: root.value; color: root.invalid ? "#c9483d" : QuiColor.FontPrimary
        selectByMouse: true; inputMethodHints: Qt.ImhFormattedNumbersOnly
        onEditingFinished: root.commit(text)
        Keys.onUpPressed: root.increment(1)
        Keys.onDownPressed: root.increment(-1)
        QuiToolTip {
            visible: field.hovered
            text: {
                let lines = []
                if (root.minimum !== undefined && root.minimum !== "") lines.push("最小值: " + root.minimum)
                if (root.maximum !== undefined && root.maximum !== "") lines.push("最大值: " + root.maximum)
                lines.push("步长: " + (root.step || "1"))
                if (root.invalid) lines.push("⚠️ 输入未保存：请检查格式或范围")
                return lines.join("\n")
            }
            delay: 500
        }
    }
    QuiTextIconButton { text: "减小"; iconSource: QuiFontIcon.Remove; implicitWidth: 24; onClicked: root.increment(-1) }
    QuiTextIconButton { text: "增大"; iconSource: QuiFontIcon.Add; implicitWidth: 24; onClicked: root.increment(1) }
}
