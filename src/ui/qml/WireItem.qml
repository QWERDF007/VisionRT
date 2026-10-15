import QtQuick
import QtQuick.Shapes

Item {
    id: root
    width: 1
    height: 1
    property real sourceX: 0
    property real sourceY: 0
    property real targetX: 0
    property real targetY: 0
    property color lineColor: "#8c959f"
    property real lineWidth: 1.5
    readonly property real elbowY: targetY >= sourceY + 12 ? (sourceY + targetY) / 2 : sourceY + 20
    readonly property real detourX: Math.max(sourceX, targetX) + 80
    readonly property var route: targetY >= sourceY + 12
        ? [Qt.point(sourceX, sourceY), Qt.point(sourceX, elbowY), Qt.point(targetX, elbowY), Qt.point(targetX, targetY)]
        : [Qt.point(sourceX, sourceY), Qt.point(sourceX, elbowY), Qt.point(detourX, elbowY), Qt.point(detourX, targetY - 16), Qt.point(targetX, targetY - 16), Qt.point(targetX, targetY)]
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
