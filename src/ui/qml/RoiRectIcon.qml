import QtQuick
import quickui

Item {
    id: root

    property color iconColor: Theme.textPrimary
    property real iconSize: Theme.paramRoiIconSize

    implicitWidth: iconSize
    implicitHeight: iconSize

    Canvas {
        id: canvas
        anchors.fill: parent
        renderTarget: Canvas.Image
        antialiasing: true

        onPaint: {
            var ctx = getContext("2d")
            ctx.reset()
            var w = width
            var h = height

            var col = root.iconColor
            ctx.strokeStyle = col
            ctx.fillStyle = col
            ctx.lineWidth = 1

            // Scale factor relative to 16x16 grid
            var s = w / 16.0

            // Main rectangle outline: (1.5, 1.5) to (12.5, 12.5)
            var rx = 1.5 * s
            var ry = 1.5 * s
            var rw = 11.0 * s
            var rh = 11.0 * s
            ctx.strokeRect(rx, ry, rw, rh)

            // 3 corner handle squares: top-left, top-right, bottom-left
            var hw = 3.0 * s
            ctx.fillRect(1.0 * s, 1.0 * s, hw, hw)
            ctx.fillRect(10.0 * s, 1.0 * s, hw, hw)
            ctx.fillRect(1.0 * s, 10.0 * s, hw, hw)

            // Pointer cursor arrow dragging the bottom-right corner
            var poly = [
                [8.0 * s, 8.0 * s],
                [8.0 * s, 14.5 * s],
                [9.8 * s, 12.5 * s],
                [12.0 * s, 15.5 * s],
                [13.6 * s, 14.2 * s],
                [11.5 * s, 11.2 * s],
                [14.0 * s, 11.2 * s]
            ]

            ctx.fillStyle = Theme.surfaceBg
            ctx.beginPath()
            ctx.moveTo(poly[0][0], poly[0][1])
            for (var i = 1; i < poly.length; ++i) {
                ctx.lineTo(poly[i][0], poly[i][1])
            }
            ctx.closePath()
            ctx.fill()
            ctx.stroke()
        }

        Connections {
            target: root
            function onIconColorChanged() { canvas.requestPaint() }
            function onIconSizeChanged() { canvas.requestPaint() }
        }
    }
}
