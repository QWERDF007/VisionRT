#pragma once

#include <visionrt/contracts/ROI.h>

#include <QPointF>
#include <QPainterPath>
#include <QPolygonF>
#include <QTransform>
#include <algorithm>
#include <array>
#include <cmath>
#include <numbers>
#include <vector>

namespace visionrt::roiui {
// Canvas values have passed the port or editing-session boundary.
inline RegionView view(const EncodedRegion &encoded)
{
    return RegionView::trusted(encoded, static_cast<RoiKind>(static_cast<int>(encoded.front())));
}

inline QPointF point(cv::Point2d value)
{
    return {value.x, value.y};
}

inline double length(QPointF value)
{
    return std::hypot(value.x(), value.y());
}

inline QPointF rotate(QPointF value, double degrees)
{
    const double angle = degrees * std::numbers::pi / 180;
    return {value.x() * std::cos(angle) - value.y() * std::sin(angle),
            value.x() * std::sin(angle) + value.y() * std::cos(angle)};
}

inline QRectF centerHandleBounds(QPointF center, qreal size)
{
    return {center - QPointF(size / 2, size / 2), QSizeF(size, size)};
}

struct RotationHandle
{
    QPointF      center;
    QPolygonF    bounds;
    QPainterPath arrow;
};

inline std::array<RotationHandle, 4> rectangleRotationHandles(const RectangleView &rect,
                                                           const QTransform &imageToItem, qreal iconSize)
{
    std::array<RotationHandle, 4> result;
    const auto corners = rect.corners();
    const std::array<QPointF, 4> signs{QPointF(-1, -1), QPointF(1, -1), QPointF(1, 1), QPointF(-1, 1)};
    const qreal half = iconSize / 2;
    const qreal radius = iconSize / 2;
    const qreal inset = iconSize / 4;
    QPainterPath arrow;
    const QRectF arc(-half + inset, -half + inset, 2 * radius, 2 * radius);
    arrow.arcMoveTo(arc, 90);
    arrow.arcTo(arc, 90, 90);
    const auto tip = arrow.currentPosition();
    const qreal head = iconSize / 5;
    arrow.moveTo(tip + QPointF(-head, -head));
    arrow.lineTo(tip);
    arrow.lineTo(tip + QPointF(head, -head));
    for (std::size_t i = 0; i < corners.size(); ++i)
    {
        const auto corner = imageToItem.map(point(corners[i]));
        auto &handle = result[i];
        handle.center = corner - rotate(signs[i] * half, rect.angle());
        QTransform transform;
        transform.translate(handle.center.x(), handle.center.y());
        transform.rotate(rect.angle() + 90 * i);
        handle.bounds = transform.map(QPolygonF(QRectF(-half, -half, iconSize, iconSize)));
        handle.arrow = transform.map(arrow);
    }
    return result;
}

inline QPointF center(const RegionView &region)
{
    if (region.kind() == RoiKind::Rectangle)
        return point(region.rectangle().center());
    if (region.kind() == RoiKind::Circle)
        return point(region.circle().center());
    if (region.kind() == RoiKind::Segment)
        return (point(region.segment().start()) + point(region.segment().end())) / 2;
    QPointF    sum;
    const auto count = region.kind() == RoiKind::Point ? region.point().size() : region.polygon().size();
    for (std::size_t i = 0; i < count; ++i)
        sum += point(region.kind() == RoiKind::Point ? region.point().point(i) : region.polygon().point(i));
    return sum / static_cast<double>(count);
}

inline std::vector<QPointF> contour(const RegionView &region)
{
    std::vector<QPointF> points;
    switch (region.kind())
    {
    case RoiKind::Rectangle:
        for (auto value : region.rectangle().corners()) points.push_back(point(value));
        break;
    case RoiKind::Segment:
        points = {point(region.segment().start()), point(region.segment().end())};
        break;
    case RoiKind::Polygon:
        for (std::size_t i = 0; i < region.polygon().size(); ++i) points.push_back(point(region.polygon().point(i)));
        break;
    case RoiKind::Point:
        for (std::size_t i = 0; i < region.point().size(); ++i) points.push_back(point(region.point().point(i)));
        break;
    case RoiKind::Circle:
        break;
    }
    return points;
}

inline double segmentDistance(QPointF value, QPointF a, QPointF b)
{
    const auto delta   = b - a;
    const auto squared = QPointF::dotProduct(delta, delta);
    return squared > 0
             ? length(value - a - delta * std::clamp(QPointF::dotProduct(value - a, delta) / squared, 0.0, 1.0))
             : length(value - a);
}

inline int hit(const RegionGroup &regions, QPointF value, double tolerance)
{
    for (int i = static_cast<int>(regions.size()) - 1; i >= 0; --i)
    {
        const auto region = view(regions[i]);
        if (region.kind() == RoiKind::Circle)
        {
            if (length(value - point(region.circle().center())) <= region.circle().radius() + tolerance)
                return i;
            continue;
        }
        const auto points = contour(region);
        if (region.kind() == RoiKind::Point)
        {
            for (auto p : points)
                if (length(value - p) <= tolerance)
                    return i;
            continue;
        }
        for (std::size_t j = 1; j < points.size(); ++j)
            if (segmentDistance(value, points[j - 1], points[j]) <= tolerance)
                return i;
        if (region.kind() != RoiKind::Segment)
        {
            QPolygonF polygon;
            for (auto p : points) polygon.append(p);
            if (segmentDistance(value, points.back(), points.front()) <= tolerance
                || polygon.containsPoint(value, Qt::OddEvenFill))
                return i;
        }
    }
    return -1;
}
} // namespace visionrt::roiui
