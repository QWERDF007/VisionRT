#include "ui/ImageViewport.h"

#include <QCursor>
#include <QHoverEvent>
#include <QKeyEvent>
#include <QMatrix4x4>
#include <QMouseEvent>
#include <QPainter>
#include <QPolygonF>
#include <QQuickWindow>
#include <QSGFlatColorMaterial>
#include <QSGGeometryNode>
#include <QSGRenderNode>
#include <QSGRendererInterface>
#include <QSGSimpleTextureNode>
#include <QSGTexture>
#include <QSGTransformNode>
#include <QThread>
#include <QWheelEvent>
#include <algorithm>
#include <cmath>
#include <limits>
#include <numbers>

namespace visionrt {
namespace {
constexpr double pi      = std::numbers::pi;
constexpr double epsilon = 0.0001;

double length(QPointF point)
{
    return std::hypot(point.x(), point.y());
}

QPointF rotated(QPointF point, double degrees)
{
    const double angle = degrees * pi / 180;
    const double c = std::cos(angle), s = std::sin(angle);
    return {point.x() * c - point.y() * s, point.x() * s + point.y() * c};
}

QPointF rectangleCenter(const Roi &roi)
{
    const auto data = roi.encoded();
    return {data[1] + data[3] / 2.0, data[2] + data[4] / 2.0};
}

std::vector<QPointF> contour(const Roi &roi)
{
    std::vector<QPointF> points;
    const auto           data = roi.encoded();
    if (roi.kind() == RoiKind::Rectangle)
    {
        for (const auto &point : roi.rectangleCorners()) points.emplace_back(point.x, point.y);
    }
    else if (roi.kind() == RoiKind::Circle)
    {
        points.reserve(96);
        for (int i = 0; i < 96; ++i)
        {
            const double angle = 2 * pi * i / 96;
            points.emplace_back(data[1] + data[3] * std::cos(angle), data[2] + data[3] * std::sin(angle));
        }
    }
    else
    {
        points.reserve((data.size() - 1) / 2);
        for (std::size_t i = 1; i < data.size(); i += 2) points.emplace_back(data[i], data[i + 1]);
    }
    return points;
}

double segmentDistance(QPointF point, QPointF a, QPointF b)
{
    const auto   delta   = b - a;
    const double squared = QPointF::dotProduct(delta, delta);
    if (squared == 0)
        return length(point - a);
    const double fraction = std::clamp(QPointF::dotProduct(point - a, delta) / squared, 0.0, 1.0);
    return length(point - a - delta * fraction);
}

// The raster scene graph does not execute generic material/geometry nodes.
// Only ROI geometry uses its painter; the image remains a QSG texture node.
class SoftwareRoiNode final : public QSGRenderNode
{
public:
    SoftwareRoiNode(QQuickWindow *window, std::vector<QPointF> triangles, QColor color)
        : window_(window)
        , triangles_(std::move(triangles))
        , color_(color)
    {
        double left = triangles_[0].x(), right = left, top = triangles_[0].y(), bottom = top;
        for (const auto &point : triangles_)
        {
            left   = std::min(left, point.x());
            right  = std::max(right, point.x());
            top    = std::min(top, point.y());
            bottom = std::max(bottom, point.y());
        }
        bounds_ = QRectF(QPointF(left, top), QPointF(right, bottom));
    }

    RenderingFlags flags() const override
    {
        return BoundedRectRendering;
    }

    QRectF rect() const override
    {
        return bounds_;
    }

    void render(const RenderState *state) override
    {
        auto *painter = static_cast<QPainter *>(
            window_->rendererInterface()->getResource(window_, QSGRendererInterface::PainterResource));
        if (!painter)
            return;
        painter->setWorldTransform(QTransform());
        if (state->clipRegion())
            painter->setClipRegion(*state->clipRegion(), Qt::IntersectClip);
        painter->setWorldTransform(matrix()->toTransform());
        painter->setOpacity(inheritedOpacity());
        painter->setPen(Qt::NoPen);
        painter->setBrush(color_);
        for (std::size_t i = 0; i < triangles_.size(); i += 3) painter->drawConvexPolygon(triangles_.data() + i, 3);
    }

private:
    QQuickWindow        *window_;
    std::vector<QPointF> triangles_;
    QColor               color_;
    QRectF               bounds_;
};

struct Lines
{
    std::vector<QPointF> triangles;
    double               halfWidth;

    explicit Lines(double scale)
        : halfWidth(0.9 / scale)
    {
    }

    void segment(QPointF a, QPointF b)
    {
        const auto   delta = b - a;
        const double size  = length(delta);
        if (size <= epsilon)
            return;
        const QPointF perpendicular(-delta.y() / size * halfWidth, delta.x() / size * halfWidth);
        triangles.insert(triangles.end(), {a + perpendicular, a - perpendicular, b + perpendicular, b + perpendicular,
                                           a - perpendicular, b - perpendicular});
    }

    void square(QPointF center, double radius)
    {
        const QPointF a = center + QPointF(-radius, -radius), b = center + QPointF(radius, -radius);
        const QPointF c = center + QPointF(radius, radius), d = center + QPointF(-radius, radius);
        triangles.insert(triangles.end(), {a, b, c, a, c, d});
    }

    void path(const std::vector<QPointF> &points, bool closed)
    {
        for (std::size_t i = 1; i < points.size(); ++i) segment(points[i - 1], points[i]);
        if (closed && points.size() > 2)
            segment(points.back(), points.front());
    }

    void roi(const Roi &roi)
    {
        const auto points = contour(roi);
        if (roi.kind() == RoiKind::PointSet)
        {
            for (const auto &point : points)
            {
                segment(point - QPointF(halfWidth * 4, 0), point + QPointF(halfWidth * 4, 0));
                segment(point - QPointF(0, halfWidth * 4), point + QPointF(0, halfWidth * 4));
            }
        }
        else
            path(points, roi.kind() != RoiKind::Segment);
    }

    QSGNode *node(QQuickWindow *window, QColor color)
    {
        if (triangles.empty())
            return nullptr;
        if (window->rendererInterface()->graphicsApi() == QSGRendererInterface::Software)
            return new SoftwareRoiNode(window, std::move(triangles), color);
        auto *geometry = new QSGGeometry(QSGGeometry::defaultAttributes_Point2D(), static_cast<int>(triangles.size()));
        geometry->setDrawingMode(QSGGeometry::DrawTriangles);
        auto *vertices = geometry->vertexDataAsPoint2D();
        for (std::size_t i = 0; i < triangles.size(); ++i)
            vertices[i].set(static_cast<float>(triangles[i].x()), static_cast<float>(triangles[i].y()));
        auto *material = new QSGFlatColorMaterial;
        material->setColor(color);
        auto *result = new QSGGeometryNode;
        result->setGeometry(geometry);
        result->setFlag(QSGNode::OwnsGeometry);
        result->setMaterial(material);
        result->setFlag(QSGNode::OwnsMaterial);
        return result;
    }
};

class ViewportNode final : public QSGTransformNode
{
public:
    ViewportNode()
    {
        image = new QSGSimpleTextureNode;
        image->setOwnsTexture(true);
        image->setFiltering(QSGTexture::Nearest);
        appendChildNode(image);
        overlays = new QSGNode;
        appendChildNode(overlays);
    }

    QSGSimpleTextureNode *image;
    QSGNode              *overlays;
    quint64               textureRevision = std::numeric_limits<quint64>::max();
    quint64               overlayRevision = std::numeric_limits<quint64>::max();
};

} // namespace

ImageViewport::ImageViewport(QQuickItem *parent)
    : QQuickItem(parent)
{
    setFlag(ItemHasContents, true);
    setClip(true);
    setAcceptedMouseButtons(Qt::LeftButton | Qt::MiddleButton | Qt::RightButton);
    setAcceptHoverEvents(true);
    connect(this, &QQuickItem::enabledChanged, this, [this] { updateCursor(); });
    setActiveFocusOnTab(true);
    const auto observeWindow = [this](QQuickWindow *window)
    {
        QObject::disconnect(windowActiveConnection_);
        if (editing())
            emit roiEditCanceled();
        windowActiveConnection_ = window
            ? connect(window, &QWindow::activeChanged, this,
                      [this, window]
                      {
                          if (!window->isActive() && editing())
                              emit roiEditCanceled();
                      })
            : QMetaObject::Connection{};
        if (mode_ == OneToOne)
        {
            recalculateFit();
            transformUpdated();
        }
    };
    connect(this, &QQuickItem::windowChanged, this, observeWindow);
    observeWindow(window());
}

ImageViewport::~ImageViewport() = default;

void ImageViewport::setStatus(QString text)
{
    if (statusText_ == text)
        return;
    statusText_ = std::move(text);
    emit statusTextChanged();
}

void ImageViewport::setGeneration(quint64 generation)
{
    Q_ASSERT(QThread::currentThread() == thread());
    if (generation <= generation_)
        return;
    generation_ = generation;
    requested_  = false;
    if (converter_)
        converter_->invalidate(generation);
    endRoiEdit();
    inputs_.clear();
    outputs_.clear();
    display_    = {};
    imageSize_  = {};
    firstImage_ = true;
    ++textureRevision_;
    overlaysUpdated();
    setStatus({});
    updateCursor();
    emit imageChanged();
}

void ImageViewport::setImage(OwnedImage image, const QString &colorOrder, quint64 requestId)
{
    Q_ASSERT(QThread::currentThread() == thread());
    if (requested_ && requestId <= requestId_)
        return;
    requested_ = true;
    requestId_ = requestId;
    if (!converter_)
    {
        converter_ = std::make_unique<DisplayConverter>();
        connect(converter_.get(), &DisplayConverter::converted, this, &ImageViewport::applyFrame);
    }
    if (image.empty())
    {
        converter_->invalidate(generation_);
        DisplayFrame empty;
        empty.requestId  = requestId;
        empty.generation = generation_;
        applyFrame(empty);
    }
    else
        converter_->request(std::move(image), colorOrder, requestId, generation_);
}

void ImageViewport::clearImage(quint64 requestId)
{
    setImage({}, {}, requestId);
}

void ImageViewport::setDisplayFrame(const DisplayFrame &frame)
{
    Q_ASSERT(QThread::currentThread() == thread());
    if (frame.generation < generation_)
        return;
    if (frame.generation > generation_)
        setGeneration(frame.generation);
    if (requested_ && frame.requestId <= requestId_)
        return;
    if (converter_)
        converter_->invalidate(generation_);
    requested_ = true;
    requestId_ = frame.requestId;
    applyFrame(frame);
}

void ImageViewport::applyFrame(const DisplayFrame &frame)
{
    if (frame.generation != generation_ || frame.requestId != requestId_)
        return;
    if (display_.cacheKey() != frame.image.cacheKey())
    {
        display_   = frame.image;
        imageSize_ = display_.size();
        ++textureRevision_;
        if (hasImage() && firstImage_)
        {
            firstImage_ = false;
            fitContain();
        }
        if (!hasImage())
            cancelDraft();
        emit imageChanged();
        overlaysUpdated();
        updateCursor();
    }
    setStatus(!frame.error.isEmpty() ? frame.error : frame.warning);
    if (!hasImage() && editing() && statusText_.isEmpty())
        setStatus(QStringLiteral("没有输入图像缓存，请先运行读图或上游；仍可在参数区进行数值编辑。"));
    update();
    emit imagePresented(frame.requestId);
}

void ImageViewport::setOverlays(std::vector<Roi> inputs, std::vector<Roi> outputs)
{
    inputs_  = std::move(inputs);
    outputs_ = std::move(outputs);
    overlaysUpdated();
}

QTransform ImageViewport::imageTransform() const
{
    return QTransform(scale_, 0, 0, scale_, width() / 2 - center_.x() * scale_, height() / 2 - center_.y() * scale_);
}

QPointF ImageViewport::imageToItem(QPointF point) const
{
    return imageTransform().map(point);
}

QPointF ImageViewport::itemToImage(QPointF point) const
{
    return center_ + QPointF((point.x() - width() / 2) / scale_, (point.y() - height() / 2) / scale_);
}

void ImageViewport::recalculateFit()
{
    if (mode_ == OneToOne)
    {
        scale_ = 1 / (window() ? window()->effectiveDevicePixelRatio() : 1.0);
    }
    else if ((mode_ == Contain || mode_ == Cover) && !imageSize_.isEmpty() && width() > 0 && height() > 0)
    {
        const double horizontal = width() / imageSize_.width(), vertical = height() / imageSize_.height();
        scale_  = mode_ == Contain ? std::min(horizontal, vertical) : std::max(horizontal, vertical);
        center_ = QPointF(imageSize_.width() / 2.0, imageSize_.height() / 2.0);
    }
}

void ImageViewport::transformUpdated()
{
    if (gesture_ != Gesture::None && gesture_ != Gesture::Pan)
        cancelDraft();
    overlaysUpdated();
    emit transformChanged();
}

void ImageViewport::fitContain()
{
    mode_ = Contain;
    recalculateFit();
    transformUpdated();
}

void ImageViewport::fitCover()
{
    mode_ = Cover;
    recalculateFit();
    transformUpdated();
}

void ImageViewport::fitOneToOne()
{
    mode_ = OneToOne;
    if (!imageSize_.isEmpty())
        center_ = QPointF(imageSize_.width() / 2.0, imageSize_.height() / 2.0);
    recalculateFit();
    transformUpdated();
}

void ImageViewport::restoreViewState(const ViewState &state)
{
    if (!std::isfinite(state.scale) || state.scale <= 0 || !std::isfinite(state.imageCenter.x())
        || !std::isfinite(state.imageCenter.y()))
        return;
    mode_   = state.mode;
    scale_  = state.scale;
    center_ = state.imageCenter;
    transformUpdated();
}

void ImageViewport::zoomAt(double factor, QPointF itemPoint)
{
    if (!std::isfinite(factor) || factor <= 0 || !hasImage())
        return;
    const auto fixed = itemToImage(itemPoint);
    scale_           = std::clamp(scale_ * factor, 0.00001, 100000.0);
    mode_            = Manual;
    center_          = fixed - QPointF((itemPoint.x() - width() / 2) / scale_, (itemPoint.y() - height() / 2) / scale_);
    transformUpdated();
}

void ImageViewport::zoomBy(double factor)
{
    zoomAt(factor, {width() / 2, height() / 2});
}

void ImageViewport::geometryChange(const QRectF &current, const QRectF &previous)
{
    QQuickItem::geometryChange(current, previous);
    if (current.size() == previous.size())
        return;
    recalculateFit();
    transformUpdated();
}

bool ImageViewport::event(QEvent *event)
{
    bool dprChanged = event->type() == QEvent::ScreenChangeInternal;
#if QT_VERSION >= QT_VERSION_CHECK(6, 6, 0)
    dprChanged = dprChanged || event->type() == QEvent::DevicePixelRatioChange;
#endif
    if (dprChanged && mode_ == OneToOne)
    {
        recalculateFit();
        transformUpdated();
    }
    return QQuickItem::event(event);
}

void ImageViewport::beginRoiEdit(RoiEditTarget target, std::vector<Roi> targetRois, bool readOnly, Type type)
{
    cancelDraft();
    editType_ = type;
    target_   = std::move(target);
    editable_ = std::move(targetRois);
    selectRoi(target_->roiIndex >= 0 && target_->roiIndex < static_cast<int>(editable_.size()) ? target_->roiIndex
                                                                                               : -1);
    setReadOnly(readOnly);
    overlaysUpdated();
    emit editingChanged();
    updateCursor();
    if (!hasImage())
        setStatus(QStringLiteral("没有输入图像缓存，请先运行读图或上游；仍可在参数区进行数值编辑。"));
}

void ImageViewport::endRoiEdit()
{
    cancelDraft();
    if (!target_)
        return;
    target_.reset();
    editable_.clear();
    setDrawShape(0);
    selectRoi(-1);
    overlaysUpdated();
    emit editingChanged();
    updateCursor();
}

void ImageViewport::setReadOnly(bool value)
{
    if (readOnly_ == value)
        return;
    if (value)
        cancelDraft();
    readOnly_ = value;
    overlaysUpdated();
    emit readOnlyChanged();
    updateCursor();
}

void ImageViewport::setDrawShape(int shape)
{
    if (shape < 0 || shape > 5 || drawShape_ == shape || (editing() && singleRectangle() && shape != 0 && shape != 2))
        return;
    clearGesture();
    drawShape_ = shape;
    updateCursor();
    emit drawShapeChanged();
}

void ImageViewport::selectRoi(int index)
{
    if (selected_ == index)
        return;
    selected_ = index;
    emit selectedRoiChanged();
    overlaysUpdated();
}

bool ImageViewport::canEdit() const
{
    return editing() && !readOnly_ && hasImage() && isEnabled();
}

void ImageViewport::overlaysUpdated()
{
    ++overlayRevision_;
    update();
}

void ImageViewport::cancelDraft()
{
    const bool restoreSelection = gesture_ == Gesture::Create && singleRectangle() && !editable_.empty();
    clearGesture();
    if (restoreSelection)
    {
        setDrawShape(0);
        selectRoi(0);
    }
    updateCursor();
}

void ImageViewport::clearGesture()
{
    gesture_   = Gesture::None;
    panButton_ = Qt::NoButton;
    original_.reset();
    draft_.reset();
    vertices_.clear();
    handle_       = -1;
    draftChanged_ = false;
    overlaysUpdated();
}

std::vector<QPointF> ImageViewport::handles(const Roi &roi) const
{
    const auto data = roi.encoded();
    if (roi.kind() == RoiKind::Circle)
    {
        const QPointF center(data[1], data[2]);
        return {center + QPointF(data[3], 0), center + QPointF(0, data[3]), center - QPointF(data[3], 0),
                center - QPointF(0, data[3])};
    }
    auto points = contour(roi);
    if (roi.kind() == RoiKind::Rectangle)
    {
        points.push_back(rectangleCenter(roi) + rotated({0, -data[4] / 2.0 - 24 / scale_}, data[5]));
    }
    return points;
}

int ImageViewport::hitRoi(QPointF point) const
{
    const double tolerance = 7 / scale_;
    for (int i = static_cast<int>(editable_.size()) - 1; i >= 0; --i)
    {
        const auto &roi = editable_[i];
        if (roi.kind() == RoiKind::Circle)
        {
            const auto data = roi.encoded();
            if (length(point - QPointF(data[1], data[2])) <= data[3] + tolerance)
                return i;
            continue;
        }
        const auto points = contour(roi);
        if (roi.kind() == RoiKind::PointSet)
        {
            for (const auto &candidate : points)
                if (length(point - candidate) <= tolerance)
                    return i;
        }
        else
        {
            for (std::size_t j = 1; j < points.size(); ++j)
                if (segmentDistance(point, points[j - 1], points[j]) <= tolerance)
                    return i;
            if (roi.kind() != RoiKind::Segment)
            {
                if (segmentDistance(point, points.back(), points.front()) <= tolerance)
                    return i;
                QPolygonF polygon;
                for (const auto &vertex : points) polygon.append(vertex);
                if (polygon.containsPoint(point, Qt::OddEvenFill))
                    return i;
            }
        }
    }
    return -1;
}

void ImageViewport::mousePressEvent(QMouseEvent *event)
{
    if (event->button() == Qt::MiddleButton || event->button() == Qt::RightButton)
    {
        if (gesture_ != Gesture::None)
        {
            event->accept();
            return;
        }
        gesture_     = Gesture::Pan;
        panButton_   = event->button();
        lastPointer_ = event->position();
        event->accept();
        return;
    }
    if (event->button() != Qt::LeftButton || !canEdit()
        || (gesture_ != Gesture::None && gesture_ != Gesture::Vertices))
    {
        event->ignore();
        return;
    }
    forceActiveFocus(Qt::MouseFocusReason);
    const auto point = itemToImage(event->position());
    if (gesture_ == Gesture::Vertices)
    {
        if (vertices_.empty() || length(point - vertices_.back()) * scale_ > 4)
            vertices_.push_back(point);
        draftChanged_ = true;
        overlaysUpdated();
        event->accept();
        return;
    }
    anchor_ = point;
    handle_ = -1;
    const bool isDrawingNew = drawShape_ >= 1 && drawShape_ <= 5;
    if (!isDrawingNew && selected_ >= 0 && selected_ < static_cast<int>(editable_.size()))
    {
        const auto controls = handles(editable_[selected_]);
        double nearest = std::numeric_limits<double>::infinity();
        for (std::size_t i = 0; i < controls.size(); ++i)
        {
            const double distance = length(point - controls[i]) * scale_;
            if (distance <= 8 && distance < nearest)
            {
                handle_ = static_cast<int>(i);
                nearest = distance;
            }
        }
    }
    if (isDrawingNew)
        selectRoi(-1);
    else if (handle_ < 0)
        selectRoi(hitRoi(point));
    if (selected_ >= 0)
    {
        original_     = editable_[selected_];
        draft_        = original_;
        gesture_      = handle_ >= 0 ? Gesture::Handle : Gesture::Move;
        draftChanged_ = false;
    }
    else if (drawShape_ >= 1 && drawShape_ <= 5)
    {
        gesture_      = drawShape_ >= 4 ? Gesture::Vertices : Gesture::Create;
        draftChanged_ = false;
        if (gesture_ == Gesture::Vertices)
        {
            vertices_     = {point};
            draftChanged_ = true;
        }
    }
    overlaysUpdated();
    event->accept();
}

void ImageViewport::changeDraft(QPointF point)
{
    try
    {
        if (gesture_ == Gesture::Create)
        {
            const auto delta = point - anchor_;
            draft_.reset();
            if (drawShape_ == 1 && length(delta) > epsilon)
                draft_ = Roi::segment({float(anchor_.x()), float(anchor_.y())}, {float(point.x()), float(point.y())});
            else if (drawShape_ == 2 && std::abs(delta.x()) > epsilon && std::abs(delta.y()) > epsilon)
                draft_
                    = Roi::rectangle(float(std::min(anchor_.x(), point.x())), float(std::min(anchor_.y(), point.y())),
                                     float(std::abs(delta.x())), float(std::abs(delta.y())), 0);
            else if (drawShape_ == 3 && length(delta) > epsilon)
                draft_ = Roi::circle(float(anchor_.x()), float(anchor_.y()), float(length(delta)));
        }
        else if (original_ && gesture_ == Gesture::Move)
        {
            const auto         delta   = point - anchor_;
            auto               encoded = original_->encoded();
            std::vector<float> data(encoded.begin(), encoded.end());
            const std::size_t  end
                = (original_->kind() == RoiKind::Rectangle || original_->kind() == RoiKind::Circle) ? 3 : data.size();
            for (std::size_t i = 1; i < end; i += 2)
            {
                data[i] += float(delta.x());
                data[i + 1] += float(delta.y());
            }
            draft_ = Roi::decode(data);
        }
        else if (original_ && gesture_ == Gesture::Handle)
        {
            draft_.reset();
            const auto data = original_->encoded();
            if (original_->kind() == RoiKind::Rectangle)
            {
                const auto center = rectangleCenter(*original_);
                if (handle_ == 4)
                {
                    const auto delta = point - center;
                    if (length(delta) > epsilon)
                        draft_ = Roi::rectangle(data[1], data[2], data[3], data[4],
                                                float(std::atan2(delta.y(), delta.x()) * 180 / pi + 90));
                }
                else
                {
                    const auto corners   = contour(*original_);
                    const auto opposite  = corners[(handle_ + 2) % 4];
                    const auto delta     = rotated(point - opposite, -data[5]);
                    const auto newCenter = (point + opposite) / 2;
                    if (std::abs(delta.x()) > epsilon && std::abs(delta.y()) > epsilon)
                        draft_ = Roi::rectangle(float(newCenter.x() - std::abs(delta.x()) / 2),
                                                float(newCenter.y() - std::abs(delta.y()) / 2),
                                                float(std::abs(delta.x())), float(std::abs(delta.y())), data[5]);
                }
            }
            else if (original_->kind() == RoiKind::Circle)
            {
                const double radius = length(point - QPointF(data[1], data[2]));
                if (radius > epsilon)
                    draft_ = Roi::circle(data[1], data[2], float(radius));
            }
            else
            {
                std::vector<float> updated(data.begin(), data.end());
                updated[1 + handle_ * 2] = float(point.x());
                updated[2 + handle_ * 2] = float(point.y());
                draft_                   = Roi::decode(updated);
            }
        }
        draftChanged_ = draft_.has_value()
                     && (!original_
                         || !std::equal(original_->encoded().begin(), original_->encoded().end(),
                                        draft_->encoded().begin(), draft_->encoded().end()));
    }
    catch (const ContractError &)
    {
        draft_.reset();
        draftChanged_ = false;
    }
    overlaysUpdated();
}

void ImageViewport::mouseMoveEvent(QMouseEvent *event)
{
    if (gesture_ == Gesture::Pan)
    {
        const auto delta = event->position() - lastPointer_;
        lastPointer_     = event->position();
        if (!delta.isNull())
        {
            mode_ = Manual;
            center_ -= delta / scale_;
            transformUpdated();
        }
        event->accept();
        return;
    }
    if (canEdit() && (gesture_ == Gesture::Create || gesture_ == Gesture::Move || gesture_ == Gesture::Handle))
    {
        changeDraft(itemToImage(event->position()));
        event->accept();
        return;
    }
    event->ignore();
}

void ImageViewport::mouseReleaseEvent(QMouseEvent *event)
{
    if (gesture_ == Gesture::Pan && event->button() == panButton_)
    {
        gesture_   = Gesture::None;
        panButton_ = Qt::NoButton;
        event->accept();
        return;
    }
    if (event->button() == Qt::LeftButton && canEdit()
        && (gesture_ == Gesture::Create || gesture_ == Gesture::Move || gesture_ == Gesture::Handle))
    {
        changeDraft(itemToImage(event->position()));
        commitDraft();
        event->accept();
        return;
    }
    event->setAccepted(gesture_ == Gesture::Vertices);
}

void ImageViewport::mouseDoubleClickEvent(QMouseEvent *event)
{
    if (event->button() == Qt::LeftButton && canEdit() && gesture_ == Gesture::Vertices)
    {
        const auto point = itemToImage(event->position());
        if (vertices_.empty() || length(point - vertices_.back()) * scale_ > 4)
            vertices_.push_back(point);
        finishDraft();
        event->accept();
        return;
    }
    event->ignore();
}

void ImageViewport::mouseUngrabEvent()
{
    // Multi-click polygon/point-set drafts intentionally survive each click's ungrab.
    if (gesture_ != Gesture::Vertices)
        cancelDraft();
}

std::optional<Roi> ImageViewport::completedDraft() const
{
    if (gesture_ != Gesture::Vertices)
        return draft_;
    std::vector<cv::Point2f> points;
    points.reserve(vertices_.size());
    for (const auto &point : vertices_) points.emplace_back(float(point.x()), float(point.y()));
    if (drawShape_ == 4 && points.size() > 1 && length(vertices_.front() - vertices_.back()) * scale_ <= 4)
        points.pop_back();
    try
    {
        if (drawShape_ == 4 && points.size() >= 3)
            return Roi::polygon(points);
        if (drawShape_ == 5 && !points.empty())
            return Roi::points(points);
    }
    catch (const ContractError &)
    {
    }
    return {};
}

void ImageViewport::commitDraft()
{
    if (!canEdit() || !draftChanged_)
    {
        clearGesture();
        return;
    }
    const auto value = completedDraft();
    if (!value)
    {
        clearGesture();
        return;
    }
    auto key     = *target_;
    key.roiIndex = (gesture_ == Gesture::Create || gesture_ == Gesture::Vertices) ? -1 : selected_;
    if (singleRectangle() && !editable_.empty())
        key.roiIndex = 0;
    if (key.roiIndex < 0)
    {
        editable_.push_back(*value);
        selectRoi(static_cast<int>(editable_.size()) - 1);
    }
    else
        editable_[key.roiIndex] = *value;
    clearGesture();
    if (singleRectangle())
        setDrawShape(0);
    updateCursor();
    emit roiCommitted(key, *value);
}

void ImageViewport::finishDraft()
{
    if (gesture_ == Gesture::Vertices)
        commitDraft();
}

void ImageViewport::deleteSelectedRoi()
{
    if (!canEdit() || selected_ < 0 || selected_ >= static_cast<int>(editable_.size()))
        return;
    auto key     = *target_;
    key.roiIndex = selected_;
    cancelDraft();
    editable_.erase(editable_.begin() + selected_);
    selectRoi(-1);
    overlaysUpdated();
    emit roiDeleted(key);
}

void ImageViewport::wheelEvent(QWheelEvent *event)
{
    const double delta
        = !event->angleDelta().isNull() ? event->angleDelta().y() / 120.0 : event->pixelDelta().y() / 60.0;
    zoomAt(std::pow(1.2, delta), event->position());
    event->accept();
}

void ImageViewport::keyPressEvent(QKeyEvent *event)
{
    if (event->key() == Qt::Key_Escape && editing())
    {
        if (gesture_ == Gesture::None)
            emit roiEditCanceled();
        else
            cancelDraft();
        event->accept();
    }
    else if ((event->key() == Qt::Key_Enter || event->key() == Qt::Key_Return) && editing())
    {
        finishDraft();
        event->accept();
    }
    else if ((event->key() == Qt::Key_Delete || event->key() == Qt::Key_Backspace) && editing())
    {
        deleteSelectedRoi();
        event->accept();
    }
    else
        QQuickItem::keyPressEvent(event);
}

void ImageViewport::updateCursor()
{
    if (drawShape_ > 0 && canEdit())
        setCursor(QCursor(Qt::CrossCursor));
    else
        unsetCursor();
}

void ImageViewport::hoverEnterEvent(QHoverEvent *event)
{
    updateCursor();
    QQuickItem::hoverEnterEvent(event);
}

void ImageViewport::hoverMoveEvent(QHoverEvent *event)
{
    updateCursor();
    QQuickItem::hoverMoveEvent(event);
}

void ImageViewport::hoverLeaveEvent(QHoverEvent *event)
{
    unsetCursor();
    QQuickItem::hoverLeaveEvent(event);
}

QSGNode *ImageViewport::updatePaintNode(QSGNode *oldNode, UpdatePaintNodeData *)
{
    if (!hasImage() || !window())
    {
        delete oldNode;
        return nullptr;
    }
    auto *node = static_cast<ViewportNode *>(oldNode);
    if (!node)
        node = new ViewportNode;
    if (node->textureRevision != textureRevision_)
    {
        auto *texture = window()->createTextureFromImage(display_);
        if (!texture)
        {
            delete node;
            return nullptr;
        }
        node->image->setTexture(texture);
        node->image->setRect(0, 0, imageSize_.width(), imageSize_.height());
        node->textureRevision = textureRevision_;
    }
    QMatrix4x4 matrix;
    matrix.translate(float(width() / 2 - center_.x() * scale_), float(height() / 2 - center_.y() * scale_));
    matrix.scale(float(scale_), float(scale_));
    node->setMatrix(matrix);
    if (node->overlayRevision != overlayRevision_)
    {
        while (auto *child = node->overlays->firstChild())
        {
            node->overlays->removeChildNode(child);
            delete child;
        }
        Lines input(scale_), output(scale_), edit(scale_);
        for (const auto &roi : inputs_) input.roi(roi);
        for (const auto &roi : outputs_) output.roi(roi);
        for (std::size_t i = 0; editing() && i < editable_.size(); ++i)
        {
            if (static_cast<int>(i) == selected_ && draft_)
                edit.roi(*draft_);
            else
                edit.roi(editable_[i]);
        }
        if (gesture_ == Gesture::Create && draft_)
            edit.roi(*draft_);
        if (gesture_ == Gesture::Vertices)
        {
            if (drawShape_ == 4)
                edit.path(vertices_, false);
            for (const auto &point : vertices_) edit.square(point, 2.5 / scale_);
        }
        if (editing() && !readOnly_ && selected_ >= 0 && selected_ < static_cast<int>(editable_.size()))
        {
            const auto &roi      = draft_ ? *draft_ : editable_[selected_];
            const auto  controls = handles(roi);
            for (const auto &point : controls) edit.square(point, 3.5 / scale_);
            if (roi.kind() == RoiKind::Rectangle)
                edit.segment((controls[0] + controls[1]) / 2, controls[4]);
        }
        if (auto *geometry = input.node(window(), QColor(255, 220, 0)))
            node->overlays->appendChildNode(geometry);
        if (auto *geometry = output.node(window(), QColor(0, 120, 255)))
            node->overlays->appendChildNode(geometry);
        if (auto *geometry = edit.node(window(), QColor(255, 220, 0)))
            node->overlays->appendChildNode(geometry);
        node->overlayRevision = overlayRevision_;
        node->markDirty(QSGNode::DirtyMatrix);
    }
    return node;
}

} // namespace visionrt
