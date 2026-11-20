import QtQuick
import QtQuick.Layouts

ColumnLayout {
    readonly property real implicitLabelWidth: {
        let widest = 0;
        for (const child of children) {
            if (child.visible && (child instanceof ToolFormRow || child instanceof ToolFormLayout))
                widest = Math.max(widest, child.implicitLabelWidth);
        }
        return widest;
    }

    spacing: 5
}
