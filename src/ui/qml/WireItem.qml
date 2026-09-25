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
    property color lineColor: "#798491"
    property real lineWidth: 1.4
    readonly property real elbowY: targetY >= sourceY + 12 ? (sourceY + targetY) / 2 : sourceY + 24
    readonly property real detourX: Math.max(sourceX, targetX) + 100
    readonly property var route: targetY >= sourceY + 12
        ? [Qt.point(sourceX, sourceY), Qt.point(sourceX, elbowY), Qt.point(targetX, elbowY), Qt.point(targetX, targetY)]
        : [Qt.point(sourceX, sourceY), Qt.point(sourceX, elbowY), Qt.point(detourX, elbowY), Qt.point(detourX, targetY - 20), Qt.point(targetX, targetY - 20), Qt.point(targetX, targetY)]
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
            PathLine { x: root.targetX - 5; y: root.targetY - 8 }
            PathLine { x: root.targetX + 5; y: root.targetY - 8 }
            PathLine { x: root.targetX; y: root.targetY }
        }
    }
}
