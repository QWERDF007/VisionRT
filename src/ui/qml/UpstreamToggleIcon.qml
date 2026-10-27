import QtQuick
import quickui
import VisionRT.Style 1.0

Item {
    id: root

    property color iconColor: Theme.bindingInactiveColor
    property real iconSize: Theme.paramToggleIconSize

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

            var cx = w * 0.5
            var topY = h * 0.05
            var arrowW = w * 0.28
            var arrowBaseY = h * 0.35

            ctx.fillStyle = root.iconColor
            ctx.strokeStyle = root.iconColor
            ctx.lineWidth = Math.max(1, w * 0.07)
            ctx.lineCap = "round"
            ctx.lineJoin = "round"

            // Upward arrow head
            ctx.beginPath()
            ctx.moveTo(cx, topY)
            ctx.lineTo(cx - arrowW, arrowBaseY)
            ctx.lineTo(cx + arrowW, arrowBaseY)
            ctx.closePath()
            ctx.fill()

            // Arrow shaft
            ctx.beginPath()
            ctx.moveTo(cx, arrowBaseY)
            ctx.lineTo(cx, h * 0.56)
            ctx.stroke()

            // Trays
            var trayLeft = w * 0.12
            var trayRight = w * 0.88
            var trayW = trayRight - trayLeft
            var r = Math.max(1, w * 0.07)

            // Upper tray
            ctx.beginPath()
            ctx.roundedRect(trayLeft, h * 0.58, trayW, h * 0.16, r, r)
            ctx.stroke()

            // Lower tray
            ctx.beginPath()
            ctx.roundedRect(trayLeft, h * 0.78, trayW, h * 0.16, r, r)
            ctx.stroke()
        }

        Connections {
            target: root
            function onIconColorChanged() { canvas.requestPaint() }
            function onIconSizeChanged() { canvas.requestPaint() }
        }
    }
}
