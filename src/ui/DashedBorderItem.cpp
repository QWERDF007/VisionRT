#include "ui/DashedBorderItem.h"

#include <QPointF>
#include <QQuickWindow>
#include <QSGFlatColorMaterial>
#include <QSGGeometry>
#include <QSGGeometryNode>
#include <QSGRendererInterface>
#include <QSGSimpleRectNode>
#include <algorithm>
#include <cmath>
#include <numbers>
#include <vector>

namespace visionrt {
namespace {

constexpr int kArcSegments = 4;

std::vector<QPointF> buildPerimeter(qreal width, qreal height, qreal inset, qreal radius)
{
    const qreal left   = inset;
    const qreal top    = inset;
    const qreal right  = width - inset;
    const qreal bottom = height - inset;
    const qreal r      = std::clamp(radius, 0.0, std::min(right - left, bottom - top) * 0.5);

    std::vector<QPointF> points;
    if (r <= 0.1)
    {
        points = {
            {left, top},
            {right, top},
            {right, bottom},
            {left, bottom},
            {left, top}
        };
        return points;
    }

    points.reserve(static_cast<std::size_t>(kArcSegments * 4 + 5));
    const auto appendArc = [&](qreal cx, qreal cy, qreal startAngle, qreal endAngle)
    {
        for (int i = 0; i <= kArcSegments; ++i)
        {
            const qreal t     = static_cast<qreal>(i) / static_cast<qreal>(kArcSegments);
            const qreal angle = startAngle + (endAngle - startAngle) * t;
            const QPointF pt(cx + r * std::cos(angle), cy + r * std::sin(angle));
            if (points.empty() || std::hypot(pt.x() - points.back().x(), pt.y() - points.back().y()) > 1e-4)
                points.push_back(pt);
        }
    };

    constexpr qreal halfPi = std::numbers::pi * 0.5;
    appendArc(right - r, top + r, -halfPi, 0.0);
    appendArc(right - r, bottom - r, 0.0, halfPi);
    appendArc(left + r, bottom - r, halfPi, std::numbers::pi);
    appendArc(left + r, top + r, std::numbers::pi, std::numbers::pi * 1.5);
    if (!points.empty())
        points.push_back(points.front());
    return points;
}

struct Vertex2D
{
    float x;
    float y;
};

void pushTriangle(std::vector<Vertex2D> &out, const QPointF &a, const QPointF &b, const QPointF &c)
{
    out.push_back({static_cast<float>(a.x()), static_cast<float>(a.y())});
    out.push_back({static_cast<float>(b.x()), static_cast<float>(b.y())});
    out.push_back({static_cast<float>(c.x()), static_cast<float>(c.y())});
}

void pushQuad(std::vector<Vertex2D> &out, const QPointF &a0, const QPointF &a1, const QPointF &b1, const QPointF &b0)
{
    pushTriangle(out, a0, a1, b1);
    pushTriangle(out, a0, b1, b0);
}

struct DashedGeometry
{
    std::vector<Vertex2D> vertices;
    std::vector<QRectF>   rects;
};

DashedGeometry buildDashedGeometry(qreal width, qreal height, qreal stroke, qreal radius,
                                   qreal dashLength, qreal gapLength)
{
    DashedGeometry result;
    if (width <= 0.0 || height <= 0.0 || stroke <= 0.0)
        return result;

    const qreal clampedStroke = std::min(stroke, std::min(width, height) * 0.5);
    const qreal halfStroke    = clampedStroke * 0.5;
    const auto  perimeter     = buildPerimeter(width, height, halfStroke, std::max(0.0, radius - halfStroke));
    if (perimeter.size() < 2)
        return result;

    const qreal dash   = std::max(0.5, dashLength);
    const qreal gap    = std::max(0.0, gapLength);
    const qreal period = dash + gap;

    qreal   distance      = 0.0;
    bool    hasPrevEnd    = false;
    QPointF prevEndOuter;
    QPointF prevEndInner;
    QPointF prevEndCenter;

    for (std::size_t i = 0; i + 1 < perimeter.size(); ++i)
    {
        const QPointF p0     = perimeter[i];
        const QPointF p1     = perimeter[i + 1];
        const qreal   dx     = p1.x() - p0.x();
        const qreal   dy     = p1.y() - p0.y();
        const qreal   segLen = std::hypot(dx, dy);
        if (segLen <= 1e-6)
            continue;

        const QPointF normal(-dy / segLen * halfStroke, dx / segLen * halfStroke);
        const qreal   segEnd = distance + segLen;
        qreal         cursor = distance;

        while (cursor < segEnd - 1e-6)
        {
            qreal phase = period > 0.0 ? std::fmod(cursor, period) : 0.0;
            if (phase < 0.0 || phase >= period - 1e-6)
                phase = 0.0;
            if (gap <= 1e-6 || phase < dash - 1e-6)
            {
                const qreal   stepEnd = gap <= 1e-6 ? segEnd : std::min(segEnd, cursor + std::max(1e-4, dash - phase));
                const qreal   t0      = (cursor - distance) / segLen;
                const qreal   t1      = (stepEnd - distance) / segLen;
                const QPointF a(p0.x() + dx * t0, p0.y() + dy * t0);
                const QPointF b(p0.x() + dx * t1, p0.y() + dy * t1);
                const QPointF aOuter = a - normal;
                const QPointF aInner = a + normal;
                const QPointF bOuter = b - normal;
                const QPointF bInner = b + normal;

                if (hasPrevEnd && std::hypot(a.x() - prevEndCenter.x(), a.y() - prevEndCenter.y()) <= 1e-3)
                {
                    pushTriangle(result.vertices, prevEndCenter, prevEndOuter, aOuter);
                    pushTriangle(result.vertices, prevEndCenter, prevEndInner, aInner);
                }

                pushQuad(result.vertices, aOuter, aInner, bInner, bOuter);

                const qreal minX = std::min({aOuter.x(), aInner.x(), bOuter.x(), bInner.x()});
                const qreal maxX = std::max({aOuter.x(), aInner.x(), bOuter.x(), bInner.x()});
                const qreal minY = std::min({aOuter.y(), aInner.y(), bOuter.y(), bInner.y()});
                const qreal maxY = std::max({aOuter.y(), aInner.y(), bOuter.y(), bInner.y()});
                result.rects.emplace_back(minX, minY, std::max(0.5, maxX - minX), std::max(0.5, maxY - minY));

                const qreal nextPhase = period > 0.0 ? std::fmod(stepEnd, period) : 0.0;
                const bool  continuesAtVertex = std::abs(stepEnd - segEnd) <= 1e-5
                                             && (gap <= 1e-6 || (nextPhase < dash - 1e-5 && nextPhase > 1e-5));
                hasPrevEnd    = continuesAtVertex;
                prevEndCenter = b;
                prevEndOuter  = bOuter;
                prevEndInner  = bInner;
                cursor        = stepEnd;
            }
            else
            {
                hasPrevEnd = false;
                cursor     = std::min(segEnd, cursor + std::max(1e-4, period - phase));
            }
        }
        distance = segEnd;
    }

    return result;
}

} // namespace

DashedBorderItem::DashedBorderItem(QQuickItem *parent)
    : QQuickItem(parent)
{
    setFlag(ItemHasContents, true);
    setAcceptedMouseButtons(Qt::NoButton);
    setAcceptHoverEvents(false);
}

void DashedBorderItem::setBorderColor(const QColor &color)
{
    if (borderColor_ == color)
        return;
    borderColor_ = color;
    emit borderColorChanged();
    update();
}

void DashedBorderItem::setBorderWidth(qreal width)
{
    const qreal clamped = std::max(0.0, width);
    if (qFuzzyCompare(borderWidth_ + 1.0, clamped + 1.0))
        return;
    borderWidth_   = clamped;
    geometryDirty_ = true;
    emit borderWidthChanged();
    update();
}

void DashedBorderItem::setRadius(qreal value)
{
    const qreal clamped = std::max(0.0, value);
    if (qFuzzyCompare(radius_ + 1.0, clamped + 1.0))
        return;
    radius_        = clamped;
    geometryDirty_ = true;
    emit radiusChanged();
    update();
}

void DashedBorderItem::setDashLength(qreal value)
{
    const qreal clamped = std::max(0.5, value);
    if (qFuzzyCompare(dashLength_ + 1.0, clamped + 1.0))
        return;
    dashLength_    = clamped;
    geometryDirty_ = true;
    emit dashLengthChanged();
    update();
}

void DashedBorderItem::setGapLength(qreal value)
{
    const qreal clamped = std::max(0.0, value);
    if (qFuzzyCompare(gapLength_ + 1.0, clamped + 1.0))
        return;
    gapLength_     = clamped;
    geometryDirty_ = true;
    emit gapLengthChanged();
    update();
}

void DashedBorderItem::setViewScale(qreal value)
{
    const qreal clamped = std::max(0.01, value);
    if (qFuzzyCompare(viewScale_ + 1.0, clamped + 1.0))
        return;
    viewScale_     = clamped;
    geometryDirty_ = true;
    emit viewScaleChanged();
    update();
}

void DashedBorderItem::geometryChange(const QRectF &newGeometry, const QRectF &oldGeometry)
{
    QQuickItem::geometryChange(newGeometry, oldGeometry);
    if (newGeometry.size() != oldGeometry.size())
    {
        geometryDirty_ = true;
        update();
    }
}

QSGNode *DashedBorderItem::updatePaintNode(QSGNode *oldNode, UpdatePaintNodeData *)
{
    if (width() <= 0.0 || height() <= 0.0 || borderWidth_ <= 0.0 || borderColor_.alpha() == 0)
    {
        delete oldNode;
        return nullptr;
    }

    const bool softwareBackend = window() && window()->rendererInterface()
        && window()->rendererInterface()->graphicsApi() == QSGRendererInterface::Software;
    if (softwareBackend)
    {
        auto *container = oldNode;
        if (!container || container->type() != QSGNode::BasicNodeType)
        {
            delete oldNode;
            container      = new QSGNode;
            geometryDirty_ = true;
        }
        if (geometryDirty_)
        {
            container->removeAllChildNodes();
            const qreal invScale = 1.0 / std::max(0.01, viewScale_);
            const auto  built    = buildDashedGeometry(width(), height(), borderWidth_ * invScale, radius_,
                                                       dashLength_ * invScale, gapLength_ * invScale);
            for (const auto &rect : built.rects)
            {
                auto *seg = new QSGSimpleRectNode(rect, borderColor_);
                seg->setFlag(QSGNode::OwnedByParent);
                container->appendChildNode(seg);
            }
            geometryDirty_ = false;
        }
        else
        {
            for (auto *child = container->firstChild(); child; child = child->nextSibling())
            {
                auto *seg = static_cast<QSGSimpleRectNode *>(child);
                if (seg->color() != borderColor_)
                    seg->setColor(borderColor_);
            }
        }
        return container;
    }

    auto *node = oldNode && oldNode->type() == QSGNode::GeometryNodeType ? static_cast<QSGGeometryNode *>(oldNode) : nullptr;
    if (!node)
    {
        delete oldNode;
        node = new QSGGeometryNode;
        auto *geometry = new QSGGeometry(QSGGeometry::defaultAttributes_Point2D(), 0);
        geometry->setDrawingMode(QSGGeometry::DrawTriangles);
        node->setGeometry(geometry);
        node->setFlag(QSGNode::OwnsGeometry);

        auto *material = new QSGFlatColorMaterial;
        node->setMaterial(material);
        node->setFlag(QSGNode::OwnsMaterial);
        geometryDirty_ = true;
    }

    auto *material = static_cast<QSGFlatColorMaterial *>(node->material());
    if (material->color() != borderColor_)
    {
        material->setColor(borderColor_);
        node->markDirty(QSGNode::DirtyMaterial);
    }

    if (geometryDirty_)
    {
        const qreal invScale = 1.0 / std::max(0.01, viewScale_);
        const auto  built    = buildDashedGeometry(width(), height(), borderWidth_ * invScale, radius_,
                                                   dashLength_ * invScale, gapLength_ * invScale);
        auto       *geometry = node->geometry();
        geometry->allocate(static_cast<int>(built.vertices.size()));
        auto *dst = geometry->vertexDataAsPoint2D();
        for (std::size_t i = 0; i < built.vertices.size(); ++i)
            dst[i].set(built.vertices[i].x, built.vertices[i].y);
        node->markDirty(QSGNode::DirtyGeometry);
        geometryDirty_ = false;
    }

    return node;
}

} // namespace visionrt
