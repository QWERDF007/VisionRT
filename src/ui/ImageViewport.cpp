#include "ui/ImageViewport.h"

#include "RoiOverlay.h"

#include <QEvent>
#include <QKeyEvent>
#include <QQuickWindow>
#include <QSGSimpleTextureNode>
#include <QSGTexture>
#include <QThread>
#include <algorithm>
#include <cmath>
#include <limits>

namespace visionrt {
ImageViewport::ImageViewport(QQuickItem *parent)
    : QQuickItem(parent)
{
    setFlag(ItemHasContents, true);
    overlay_ = new RoiOverlay(*this);
    setClip(true);
    setAcceptedMouseButtons(Qt::LeftButton | Qt::MiddleButton | Qt::RightButton);
    setAcceptHoverEvents(true);
    connect(this, &QQuickItem::enabledChanged, this, [this] { updateCursor(); });
    setActiveFocusOnTab(true);
    const auto observeWindow = [this](QQuickWindow *window)
    {
        if (observedWindow_)
            observedWindow_->removeEventFilter(this);
        observedWindow_ = window;
        if (window)
            window->installEventFilter(this);
        altHeld_ = false;
        updatePixelProbe();
        QObject::disconnect(windowActiveConnection_);
        if (editing())
            emit roiEditCanceled();
        windowActiveConnection_ = window ? connect(window, &QWindow::activeChanged, this,
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

ImageViewport::~ImageViewport()
{
    if (observedWindow_)
        observedWindow_->removeEventFilter(this);
    QObject::disconnect(this, &QQuickItem::windowChanged, this, nullptr);
    QObject::disconnect(this, &QQuickItem::enabledChanged, this, nullptr);
    QObject::disconnect(windowActiveConnection_);
}

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
    endRoiEdit();
    inputs_.clear();
    outputs_.clear();
    assistPreview_.clear();
    display_    = {};
    sourceType_ = -1;
    updatePixelProbe();
    imageSize_  = {};
    firstImage_ = true;
    ++textureRevision_;
    overlaysUpdated();
    setStatus({});
    updateCursor();
    emit imageChanged();
}

void ImageViewport::clearImage(quint64 requestId)
{
    DisplayFrame empty;
    empty.requestId  = requestId;
    empty.generation = generation_;
    setDisplayFrame(empty);
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
    requested_ = true;
    requestId_ = frame.requestId;
    applyFrame(frame);
}

void ImageViewport::setHostFrame(const DisplayFrame &source)
{
    auto frame       = source;
    frame.generation = generation_;
    frame.requestId  = requestId_;
    applyFrame(frame);
    setStatus({});
    update();
}

void ImageViewport::applyFrame(const DisplayFrame &frame)
{
    if (frame.generation != generation_ || frame.requestId != requestId_)
        return;
    sourceType_ = frame.sourceType;
    if (display_.cacheKey() != frame.image.cacheKey())
    {
        inspectedInput_.reset();
        static_cast<RoiOverlay *>(overlay_)->clear();
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
    updatePixelProbe();
    update();
    emit imagePresented(frame.requestId);
}

void ImageViewport::setOverlays(RegionGroup inputs, RegionGroup outputs, std::vector<RoiEditTarget> targets)
{
    inputs_       = std::move(inputs);
    outputs_      = std::move(outputs);
    inputTargets_ = std::move(targets);
    overlaysUpdated();
}

void ImageViewport::setAssistPreview(ROI preview)
{
    if (assistPreview_ == preview)
        return;
    assistPreview_ = std::move(preview);
    overlaysUpdated();
}

void ImageViewport::setAssistActive(bool value)
{
    if (assistActive_ == value)
        return;
    assistActive_ = value;
    if (!assistActive_)
        setAssistPreview({});
    if (gesture_ == Gesture::Assist)
        clearGesture();
    updateCursor();
    emit assistActiveChanged();
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
    updatePixelProbe();
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
    if (overlay_)
        overlay_->setSize(current.size());
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

QString ImageViewport::pixelTextAt(QPointF itemPoint) const
{
    if (!hasImage() || itemPoint.x() < 0 || itemPoint.y() < 0 || itemPoint.x() >= width() || itemPoint.y() >= height())
        return {};
    const auto point = itemToImage(itemPoint);
    if (point.x() < 0 || point.y() < 0 || point.x() >= imageSize_.width() || point.y() >= imageSize_.height())
        return {};
    const int x    = static_cast<int>(std::floor(point.x()));
    const int y    = static_cast<int>(std::floor(point.y()));
    auto      text = QStringLiteral("%1,%2").arg(x).arg(y);
    if (sourceType_ == CV_8UC1)
        text += QStringLiteral("\nGray: %1").arg(display_.constScanLine(y)[x]);
    else if (sourceType_ == CV_8UC3)
    {
        const auto color = display_.pixelColor(x, y);
        text += QStringLiteral("\nR: %1\nG: %2\nB: %3").arg(color.red()).arg(color.green()).arg(color.blue());
    }
    return text;
}

void ImageViewport::updatePixelProbe()
{
    pixelText_ = pointerInside_ && altHeld_ ? pixelTextAt(pointerPosition_) : QString{};
    emit pixelProbeChanged();
}

bool ImageViewport::eventFilter(QObject *watched, QEvent *event)
{
    // Window keys remain authoritative while the pointer is inside, regardless of keyboard focus.
    if (watched == observedWindow_)
    {
        bool held = altHeld_;
        if (event->type() == QEvent::KeyPress || event->type() == QEvent::KeyRelease)
        {
            const auto *key = static_cast<QKeyEvent *>(event);
            held = key->key() == Qt::Key_Alt ? event->type() == QEvent::KeyPress
                                            : key->modifiers().testFlag(Qt::AltModifier);
        }
        else if (event->type() == QEvent::WindowDeactivate)
            held = false;
        if (held != altHeld_)
        {
            altHeld_ = held;
            updatePixelProbe();
        }
    }
    return QQuickItem::eventFilter(watched, event);
}

QSGNode *ImageViewport::updatePaintNode(QSGNode *oldNode, UpdatePaintNodeData *)
{
    if (!hasImage() || !window())
    {
        delete oldNode;
        return nullptr;
    }
    auto *node = static_cast<QSGSimpleTextureNode *>(oldNode);
    if (!node)
    {
        node = new QSGSimpleTextureNode;
        node->setOwnsTexture(true);
        node->setFiltering(QSGTexture::Nearest);
    }
    if (!node->texture() || node->texture()->comparisonKey() != uploadedTextureKey_)
        uploadedRevision_ = std::numeric_limits<quint64>::max();
    if (uploadedRevision_ != textureRevision_)
    {
        auto *texture = window()->createTextureFromImage(display_);
        if (!texture)
        {
            delete node;
            return nullptr;
        }
        node->setTexture(texture);
        uploadedTextureKey_ = texture->comparisonKey();
        uploadedRevision_   = textureRevision_;
    }
    node->setRect(QRectF(imageToItem({0, 0}), imageToItem({double(imageSize_.width()), double(imageSize_.height())})));
    return node;
}
} // namespace visionrt
