#include "RoiOverlay.h"

#include "RoiGeometry.h"
#include "ui/Color.h"
#include "ui/ImageViewport.h"
#include "ui/Size.h"

#include <quickui/Font.h>

#include <QMetaMethod>
#include <QMetaProperty>
#include <QPainter>
#include <QPainterPath>
#include <QQuickWindow>

namespace visionrt {
RoiOverlay::RoiOverlay(ImageViewport &viewport)
    : QQuickPaintedItem(&viewport)
    , viewport_(viewport)
{
    setAntialiasing(true);
    setAcceptedMouseButtons(Qt::NoButton);
    setAcceptHoverEvents(false);
    setZ(1);
    for (QObject *style :
         {static_cast<QObject *>(VisionRTSize::getInstance()), static_cast<QObject *>(VisionRTColor::getInstance()),
          static_cast<QObject *>(quickui::QuiFont::getInstance())})
        for (int i = style->metaObject()->propertyOffset(); i < style->metaObject()->propertyCount(); ++i)
        {
            const auto property = style->metaObject()->property(i);
            if (property.hasNotifySignal())
                QObject::connect(style, property.notifySignal(), this,
                                 metaObject()->method(metaObject()->indexOfSlot("update()")));
        }
    transition_.setDuration(120);
    transition_.setStartValue(1.0);
    transition_.setEndValue(0.0);
    connect(&transition_, &QVariantAnimation::valueChanged, this, [this] { update(); });
    connect(&transition_, &QVariantAnimation::finished, this,
            [this]
            {
                previous_.reset();
                update();
            });
}

void RoiOverlay::fade(const EncodedRegion &region)
{
    previous_ = region;
    transition_.stop();
    transition_.start();
}

void RoiOverlay::clear()
{
    transition_.stop();
    previous_.reset();
    update();
}

void RoiOverlay::paint(QPainter *p)
{
    const auto &v = viewport_;
    if (!v.hasImage())
        return;
    const auto &s = *VisionRTSize::getInstance();
    const auto &c = *VisionRTColor::getInstance();
    p->setRenderHint(QPainter::Antialiasing);
    p->setFont(quickui::QuiFont::getInstance()->Caption());
    const auto map = [&](QPointF value)
    {
        return v.imageToItem(value);
    };
    const auto pen = [&](QColor color, qreal width)
    {
        p->setPen(QPen(color, width));
        p->setBrush(Qt::NoBrush);
    };
    const auto label = [&](QPointF anchor, QString text, double angle = 0.0, Qt::Alignment alignment = Qt::AlignCenter)
    {
        p->save();
        const QFontMetricsF metrics(p->font());
        const auto          lines = text.split('\n');
        qreal               width = 0;
        for (const auto &line : lines) width = std::max(width, metrics.horizontalAdvance(line));
        const QSizeF size(width + 2 * s.ROILabelHorizontalPadding(),
                          lines.size() * metrics.height() + 2 * s.ROILabelVerticalPadding());
        p->translate(anchor);
        p->rotate(std::remainder(angle, 180.0));
        QRectF rect(-size.width() / 2, -size.height() / 2, size.width(), size.height());
        if (alignment.testFlag(Qt::AlignLeft)) rect.moveLeft(0);
        p->setPen(Qt::NoPen);
        p->setBrush(c.ROIInput());
        p->drawRoundedRect(rect, 2, 2);
        p->setPen(c.ROILabelText());
        p->drawText(rect, Qt::AlignCenter, text);
        p->restore();
    };
    const auto arrow = [&](QPointF a, QPointF b, QColor color)
    {
        pen(color, s.ROIGuideStrokeWidth());
        p->drawLine(a, b);
        const auto delta = b - a;
        if (roiui::length(delta) == 0)
            return;
        const auto head = delta / roiui::length(delta) * s.ROIArrowHeadSize();
        p->drawLine(b, b - roiui::rotate(head, 28));
        p->drawLine(b, b - roiui::rotate(head, -28));
    };
    for (const auto &overlay : v.geometry_)
        std::visit([&](const auto &geometry)
        {
            const auto point = [&](cv::Point2d value)
            {
                const auto image = overlay.alignment.map(value);
                return map({image.x, image.y});
            };
            for (std::size_t i = 0; i < geometry.count(overlay.image); ++i)
            {
                const auto value = geometry.at(overlay.image, i);
                if constexpr (std::is_same_v<std::decay_t<decltype(geometry)>, Direction>)
                    arrow(point(value.start()), point(value.end()), overlay.input ? c.ROIInput() : c.ROIOutput());
                else
                {
                    const auto origin = point(value.origin());
                    const auto xEnd = point(value.xEnd()), yEnd = point(value.yEnd());
                    arrow(origin, xEnd, c.CoordinateAxisX());
                    arrow(origin, yEnd, c.CoordinateAxisY());
                    pen(c.CoordinateAxisX(), s.ROIGuideStrokeWidth());
                    p->drawText(xEnd + QPointF(s.ROIArrowHeadSize(), 0), QStringLiteral("X"));
                    pen(c.CoordinateAxisY(), s.ROIGuideStrokeWidth());
                    p->drawText(yEnd + QPointF(0, s.ROIArrowHeadSize()), QStringLiteral("Y"));
                    pen(c.CoordinateOrigin(), s.ROIGuideStrokeWidth());
                    const auto half = s.ROICenterHandleSize() / 2;
                    p->drawLine(origin - QPointF(half, 0), origin + QPointF(half, 0));
                    p->drawLine(origin - QPointF(0, half), origin + QPointF(0, half));
                }
            }
        }, overlay.value);
    const auto shape = [&](const EncodedRegion &encoded, QColor color, qreal stroke, qreal fill)
    {
        const auto region = roiui::view(encoded);
        pen(color, stroke);
        if (fill > 0 && region.kind() != RoiKind::Segment && region.kind() != RoiKind::Point)
        {
            auto tint = color;
            tint.setAlphaF(fill);
            p->setBrush(tint);
        }
        if (region.kind() == RoiKind::Circle)
        {
            const auto circle = region.circle();
            p->drawEllipse(map(roiui::point(circle.center())), circle.radius() * v.zoom(), circle.radius() * v.zoom());
        }
        else
        {
            QPolygonF polygon;
            for (auto value : roiui::contour(region)) polygon.append(map(value));
            if (region.kind() == RoiKind::Segment)
                p->drawPolyline(polygon);
            else if (region.kind() == RoiKind::Point)
                for (auto value : polygon)
                {
                    p->drawLine(value - QPointF(4, 0), value + QPointF(4, 0));
                    p->drawLine(value - QPointF(0, 4), value + QPointF(0, 4));
                }
            else
                p->drawPolygon(polygon);
        }
    };
    for (const auto &region : v.outputs_) shape(region, c.ROIOutput(), s.ROIOutputStrokeWidth(), 0);
    for (std::size_t i = 0; i < v.inputs_.size(); ++i)
    {
        const bool editingTarget = v.target_ && i < v.inputTargets_.size()
                                && v.inputTargets_[i].nodeSeq == v.target_->nodeSeq
                                && v.inputTargets_[i].inputPortId == v.target_->inputPortId
                                && v.inputTargets_[i].imageIndex == v.target_->imageIndex;
        if (!editingTarget && (!v.inspectedInput_ || v.inputs_[i] != *v.inspectedInput_))
            shape(v.inputs_[i], c.ROIInput(), s.ROIInputStrokeWidth(), 0);
    }
    for (std::size_t i = 0; i < v.editable_.size(); ++i)
        if (static_cast<int>(i) != v.selected_)
            shape(v.editable_[i], c.ROIInput(), s.ROIInputStrokeWidth(), 0);
    if (previous_ && transition_.state() == QAbstractAnimation::Running)
    {
        const qreal amount = transition_.currentValue().toReal();
        shape(*previous_, c.ROIInput(), s.ROIInputStrokeWidth(), c.ROISelectionFillOpacity() * amount);
    }
    for (const auto &group : v.assistPreview_)
        for (const auto &region : group) shape(region, c.ROIPreview(), s.ROIInputStrokeWidth(), 0);
    const auto *active = v.activeRegion();
    if (active)
    {
        shape(*active, c.ROIInput(), s.ROIInputStrokeWidth(), c.ROISelectionFillOpacity());
        const auto region     = roiui::view(*active);
        const auto center     = map(roiui::center(region));
        const bool writable   = !v.readOnly_ && !v.inspectedInput_;
        const qreal centerSize = s.ROICenterHandleSize();
        const bool showMeasurements
            = v.pointerInside_
           && roiui::centerHandleBounds(center, centerSize).contains(v.pointerPosition_);
        const auto square   = [&](QPointF position, qreal size)
        {
            pen(c.ROIInput(), s.ROIGuideStrokeWidth());
            const qreal dpr  = window() ? window()->effectiveDevicePixelRatio() : 1;
            position         = {std::round(position.x() * dpr) / dpr, std::round(position.y() * dpr) / dpr};
            const qreal side = size - s.ROIGuideStrokeWidth();
            p->drawRect(QRectF(position - QPointF(side / 2, side / 2), QSizeF(side, side)));
        };
        const auto coordinate = [](QPointF value)
        {
            return QStringLiteral("x: %1\ny: %2").arg(value.x(), 0, 'f', 2).arg(value.y(), 0, 'f', 2);
        };
        if (showMeasurements && region.kind() != RoiKind::Point && region.kind() != RoiKind::Circle)
            label(center + QPointF(0, s.ROILabelOffset() + 16), coordinate(roiui::center(region)));
        if (region.kind() == RoiKind::Rectangle)
        {
            const auto rect     = region.rectangle();
            const auto controls = v.handles(*active);
            const auto axis     = roiui::rotate({1, 0}, rect.angle());
            const auto normal   = roiui::rotate({0, 1}, rect.angle());
            arrow(center, center + axis * rect.width() * v.zoom() / 2, c.ROIInput());
            if (writable)
            {
                const auto rotations = roiui::rectangleRotationHandles(rect, v.imageTransform(), s.ROIRotationArrowSize());
                pen(c.ROIGuide(), s.ROIGuideStrokeWidth());
                for (const auto &control : rotations) p->drawPath(control.arrow);
                for (std::size_t i = 1; i <= 4; ++i) square(map(controls[i]), s.ROIHandleSize());
            }
            if (showMeasurements)
            {
                const QFontMetricsF metrics(p->font());
                const qreal labelHeight = metrics.height() + 2 * s.ROILabelVerticalPadding();
                const qreal gap = s.ROILabelOffset();
                label(center - normal * (rect.height() * v.zoom() / 2 + gap + labelHeight / 2),
                      QStringLiteral("%1 px").arg(rect.width(), 0, 'f', 1), rect.angle());
                label(center + axis * (rect.width() * v.zoom() / 2 + gap + labelHeight / 2),
                      QStringLiteral("%1 px").arg(rect.height(), 0, 'f', 1), rect.angle() + 90);
                const auto angleText = QString::number(rect.angle(), 'f', 1) + QChar(0x00b0);
                const qreal angleWidth = metrics.horizontalAdvance(angleText) + 2 * s.ROILabelHorizontalPadding();
                const qreal along = (std::abs(axis.x()) * angleWidth + std::abs(axis.y()) * labelHeight) / 2;
                const qreal across = (std::abs(normal.x()) * angleWidth + std::abs(normal.y()) * labelHeight) / 2;
                label(map(roiui::contour(region)[1]) + axis * (gap + along) - normal * (gap + across), angleText);
            }
        }
        else if (region.kind() == RoiKind::Segment || region.kind() == RoiKind::Polygon
                 || region.kind() == RoiKind::Point)
        {
            const auto points = roiui::contour(region);
            for (auto value : points)
            {
                if (writable)
                    square(map(value), s.ROIHandleSize());
                if (showMeasurements && region.kind() != RoiKind::Polygon)
                    label(map(value) + QPointF(0, -s.ROILabelOffset() - 16), coordinate(value));
            }
        }
        else if (region.kind() == RoiKind::Circle)
        {
            const auto circle = region.circle();
            const auto direction = v.circleHandleDirection_;
            const auto handle = center + direction * circle.radius() * v.zoom();
            pen(c.ROIInput(), s.ROIInputStrokeWidth());
            p->drawLine(center, handle);
            if (writable)
                square(handle, s.ROIHandleSize());
            if (showMeasurements)
            {
                const auto value = roiui::point(circle.center());
                label(center + QPointF(centerSize / 2 + s.ROILabelOffset(), 0),
                      QStringLiteral("x: %1 px\ny: %2 px").arg(value.x(), 0, 'f', 1).arg(value.y(), 0, 'f', 1),
                      0, Qt::AlignLeft);
                const auto radiusText = QStringLiteral("r: %1 px").arg(circle.radius(), 0, 'f', 1);
                const QFontMetricsF metrics(p->font());
                const qreal width = metrics.horizontalAdvance(radiusText) + 2 * s.ROILabelHorizontalPadding();
                const qreal height = metrics.height() + 2 * s.ROILabelVerticalPadding();
                const qreal extent = (std::abs(direction.x()) * width + std::abs(direction.y()) * height) / 2;
                label(handle + direction * (s.ROILabelOffset() + extent), radiusText);
            }
        }
        if (writable && region.kind() != RoiKind::Point)
            square(center, s.ROICenterHandleSize());
        pen(c.ROICrosshair(), s.ROIGuideStrokeWidth());
        p->drawLine(center - QPointF(5, 0), center + QPointF(5, 0));
        p->drawLine(center - QPointF(0, 5), center + QPointF(0, 5));
    }
    if (v.gesture_ == ImageViewport::Gesture::Vertices)
    {
        pen(c.ROIInput(), s.ROIInputStrokeWidth());
        QPolygonF polygon;
        for (auto value : v.vertices_) polygon.append(map(value));
        if (v.drawShape_ == ImageViewport::DrawPolygon)
            p->drawPolyline(polygon);
        for (auto value : polygon)
            p->drawRect(QRectF(value - QPointF(s.ROIHandleSize() / 2, s.ROIHandleSize() / 2),
                               QSizeF(s.ROIHandleSize(), s.ROIHandleSize())));
    }
}
} // namespace visionrt
