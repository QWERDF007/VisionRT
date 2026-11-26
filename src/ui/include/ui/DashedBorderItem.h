#pragma once

#include "ui/Export.h"

#include <QColor>
#include <QQuickItem>

namespace visionrt {

// Pure vector dashed rounded-rectangle border rendered via QSGGeometryNode + QSGFlatColorMaterial.
// Compatible with both RHI backends and QSGRendererInterface::Software with constant screen-space
// stroke width and dash cadence under parent scale transforms.
class VISIONRT_UI_API DashedBorderItem : public QQuickItem
{
    Q_OBJECT
    Q_PROPERTY(QColor borderColor READ borderColor WRITE setBorderColor NOTIFY borderColorChanged)
    Q_PROPERTY(qreal borderWidth READ borderWidth WRITE setBorderWidth NOTIFY borderWidthChanged)
    Q_PROPERTY(qreal radius READ radius WRITE setRadius NOTIFY radiusChanged)
    Q_PROPERTY(qreal dashLength READ dashLength WRITE setDashLength NOTIFY dashLengthChanged)
    Q_PROPERTY(qreal gapLength READ gapLength WRITE setGapLength NOTIFY gapLengthChanged)
    Q_PROPERTY(qreal viewScale READ viewScale WRITE setViewScale NOTIFY viewScaleChanged)

public:
    explicit DashedBorderItem(QQuickItem *parent = nullptr);

    QColor borderColor() const noexcept { return borderColor_; }
    void   setBorderColor(const QColor &color);

    qreal borderWidth() const noexcept { return borderWidth_; }
    void  setBorderWidth(qreal width);

    qreal radius() const noexcept { return radius_; }
    void  setRadius(qreal value);

    qreal dashLength() const noexcept { return dashLength_; }
    void  setDashLength(qreal value);

    qreal gapLength() const noexcept { return gapLength_; }
    void  setGapLength(qreal value);

    qreal viewScale() const noexcept { return viewScale_; }
    void  setViewScale(qreal value);

signals:
    void borderColorChanged();
    void borderWidthChanged();
    void radiusChanged();
    void dashLengthChanged();
    void gapLengthChanged();
    void viewScaleChanged();

protected:
    QSGNode *updatePaintNode(QSGNode *oldNode, UpdatePaintNodeData *data) override;
    void     geometryChange(const QRectF &newGeometry, const QRectF &oldGeometry) override;

private:
    QColor borderColor_{Qt::transparent};
    qreal  borderWidth_ = 1.0;
    qreal  radius_      = 0.0;
    qreal  dashLength_  = 6.0;
    qreal  gapLength_   = 4.0;
    qreal  viewScale_   = 1.0;
    bool   geometryDirty_ = true;
};

} // namespace visionrt
