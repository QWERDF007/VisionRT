import QtQuick
import QtQuick.Shapes

Item {
    id: root
    width: 1
    height: 1
    property var route: []
    readonly property real sourceX: route.length ? route[0].x : 0
    readonly property real sourceY: route.length ? route[0].y : 0
    readonly property real targetX: route.length ? route[route.length - 1].x : 0
    readonly property real targetY: route.length ? route[route.length - 1].y : 0
    property color lineColor: Theme.iconMuted
    property real lineWidth: 1.5
    property real viewScale: 1
    property int connectionCount: 1
    signal activated()
    Repeater {
        model: Math.max(0, root.route.length - 1)
        Item {
            required property int index
            readonly property point start: root.route[index]
            readonly property point finish: root.route[index + 1]
            readonly property real margin: 5 / root.viewScale
            x: Math.min(start.x, finish.x) - margin
            y: Math.min(start.y, finish.y) - margin
            width: Math.abs(finish.x - start.x) + margin * 2
            height: Math.abs(finish.y - start.y) + margin * 2
            MouseArea {
                anchors.fill: parent
                acceptedButtons: Qt.LeftButton
                cursorShape: Qt.PointingHandCursor
                onClicked: root.activated()
            }
        }
    }
    Text {
        visible: root.connectionCount > 1
        x: root.sourceX + 8
        y: root.sourceY + 4
        text: "×" + root.connectionCount
        color: root.lineColor
        font.pixelSize: Theme.fontSizeSmall
        scale: 1 / root.viewScale
        transformOrigin: Item.TopLeft
    }
    Shape {
        preferredRendererType: Shape.CurveRenderer
        ShapePath {
            strokeColor: root.lineColor; strokeWidth: root.lineWidth; fillColor: "transparent"
            capStyle: ShapePath.RoundCap; joinStyle: ShapePath.RoundJoin
            PathPolyline { path: root.route }
        }
        ShapePath {
            strokeColor: "transparent"; fillColor: root.lineColor
            startX: root.targetX; startY: root.targetY
            PathLine { x: root.targetX - 4; y: root.targetY - 6 }
            PathLine { x: root.targetX + 4; y: root.targetY - 6 }
            PathLine { x: root.targetX; y: root.targetY }
        }
    }
}
