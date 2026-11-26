#include "RoiGeometry.h"
#include "RoiOverlay.h"
#include "ui/ImageViewport.h"
#include "ui/Size.h"

#include <QCursor>
#include <QGuiApplication>
#include <QHoverEvent>
#include <QKeyEvent>
#include <QMouseEvent>
#include <QStyleHints>
#include <QWheelEvent>
#include <limits>
#include <numbers>

namespace visionrt {
constexpr double pi      = std::numbers::pi;
constexpr double epsilon = 0.0001;

void ImageViewport::beginRoiEdit(RoiEditTarget target, RegionGroup targetRois, bool readOnly, bool singleRectangle)
{
    if (target_ && *target_ == target && editable_ == targetRois && singleRectangle_ == singleRectangle)
    {
        setReadOnly(readOnly);
        return;
    }
    cancelDraft();
    inspectedInput_.reset();
    if (!target_ || *target_ != target)
        circleHandleDirection_ = {1, 0};
    singleRectangle_     = singleRectangle;
    target_              = std::move(target);
    editable_            = std::move(targetRois);
    int initialSelection = target_->roiIndex >= 0 && target_->roiIndex < static_cast<int>(editable_.size())
                             ? target_->roiIndex
                             : (editable_.size() == 1 ? 0 : -1);
    selectRoi(initialSelection);
    setReadOnly(readOnly);
    overlaysUpdated();
    emit editingChanged();
    updateCursor();
}

void ImageViewport::flushPendingKeyCommit()
{
    keyCommitTimer_.stop();
    if (!pendingKeyCommit_)
        return;
    auto pending = std::move(*pendingKeyCommit_);
    pendingKeyCommit_.reset();
    emit roiCommitted(pending.first, pending.second);
}

void ImageViewport::endRoiEdit()
{
    flushPendingKeyCommit();
    cancelDraft();
    if (!target_)
        return;
    target_.reset();
    editable_.clear();
    setDrawShape(DrawNone);
    selectRoi(-1);
    inspectedInput_.reset();
    hoveringCenterHandle_ = false;
    static_cast<RoiOverlay *>(overlay_)->clear();
    overlaysUpdated();
    emit editingChanged();
    updateCursor();
}

void ImageViewport::setReadOnly(bool value)
{
    if (readOnly_ == value)
        return;
    if (value)
    {
        flushPendingKeyCommit();
        cancelDraft();
    }
    readOnly_ = value;
    overlaysUpdated();
    emit readOnlyChanged();
    updateCursor();
}

void ImageViewport::setDrawShape(int shape)
{
    if (shape < DrawNone || shape > DrawPoint || drawShape_ == shape
        || (editing() && singleRectangle() && shape != DrawNone && shape != DrawRectangle))
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
    flushPendingKeyCommit();
    if (selected_ >= 0 && selected_ < static_cast<int>(editable_.size()))
        static_cast<RoiOverlay *>(overlay_)->fade(editable_[selected_]);
    selected_ = index;
    circleHandleDirection_ = {1, 0};
    hoveringCenterHandle_  = pointerInside_ && computeHoverCenterHandle(pointerPosition_);
    emit selectedRoiChanged();
    overlaysUpdated();
}

void ImageViewport::selectRegion(int index)
{
    if (index >= 0 && index < static_cast<int>(editable_.size()))
        selectRoi(index);
}

void ImageViewport::previewGeometry(const RegionGroup &regions)
{
    editable_ = regions;
    overlaysUpdated();
}

void ImageViewport::inspectInputRegion(const EncodedRegion &region)
{
    inspectedInput_ = region;
    selectRoi(-1);
    overlaysUpdated();
}

const EncodedRegion *ImageViewport::activeRegion() const
{
    if (inspectedInput_) return &*inspectedInput_;
    if (draft_) return &*draft_;
    return selected_ >= 0 && selected_ < static_cast<int>(editable_.size()) ? &editable_[selected_] : nullptr;
}

bool ImageViewport::computeHoverCenterHandle(QPointF itemPoint) const
{
    const auto *active = activeRegion();
    if (!active || !hasImage())
        return false;
    const auto center = imageToItem(roiui::center(roiui::view(*active)));
    return roiui::centerHandleBounds(center, VisionRTSize::getInstance()->ROICenterHandleSize()).contains(itemPoint);
}

bool ImageViewport::updateCircleHandle(QPointF itemPoint)
{
    const auto *active = activeRegion();
    if (!active || !hasImage()) return false;
    const auto region = roiui::view(*active);
    if (region.kind() != RoiKind::Circle) return false;
    const auto circle = region.circle();
    const auto center = roiui::point(circle.center());
    const auto &style = *VisionRTSize::getInstance();
    if (roiui::centerHandleBounds(imageToItem(center), style.ROICenterHandleSize()).contains(itemPoint)) return false;
    const auto delta = itemToImage(itemPoint) - center;
    const auto distance = roiui::length(delta);
    if (distance > 0 && std::abs(distance - circle.radius()) * scale_ <= style.ROIEdgeHitTolerance())
    {
        const QPointF nextDir = delta / distance;
        if (!qFuzzyCompare(nextDir.x() + 1.0, circleHandleDirection_.x() + 1.0)
            || !qFuzzyCompare(nextDir.y() + 1.0, circleHandleDirection_.y() + 1.0))
        {
            circleHandleDirection_ = nextDir;
            return true;
        }
    }
    return false;
}

std::vector<QPointF> ImageViewport::handles(const EncodedRegion &encoded) const
{
    const auto region = roiui::view(encoded);
    if (region.kind() == RoiKind::Point)
        return roiui::contour(region);
    std::vector<QPointF> result{roiui::center(region)};
    if (region.kind() == RoiKind::Rectangle)
    {
        const auto points = roiui::contour(region);
        for (std::size_t i = 0; i < 4; ++i) result.push_back((points[i] + points[(i + 1) % 4]) / 2);
    }
    else if (region.kind() != RoiKind::Circle)
    {
        const auto points = roiui::contour(region);
        result.insert(result.end(), points.begin(), points.end());
    }
    return result;
}

int ImageViewport::hitRoi(QPointF point) const
{
    return roiui::hit(editable_, point, VisionRTSize::getInstance()->ROIEdgeHitTolerance() / scale_);
}

bool ImageViewport::canEdit() const
{
    return editing() && !readOnly_ && hasImage() && isEnabled();
}

void ImageViewport::overlaysUpdated()
{
    if (overlay_)
        overlay_->update();
    update();
}

void ImageViewport::cancelDraft()
{
    const bool hadDraft = gesture_ == Gesture::Create || gesture_ == Gesture::Move || gesture_ == Gesture::Handle;
    const bool restoreSelection = gesture_ == Gesture::Create && singleRectangle() && !editable_.empty();
    clearGesture();
    if (hadDraft)
        emit roiDraftCanceled();
    if (restoreSelection)
    {
        setDrawShape(DrawNone);
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

void ImageViewport::mousePressEvent(QMouseEvent *event)
{
    flushPendingKeyCommit();
    forceActiveFocus(Qt::MouseFocusReason);
    gestureInverse_ = imageTransform().inverted();
    if (event->button() == Qt::LeftButton && drawShape_ == DrawNone && gesture_ == Gesture::None)
    {
        const auto point = itemToImage(event->position());
        const int  hit   = roiui::hit(inputs_, point, VisionRTSize::getInstance()->ROIEdgeHitTolerance() / scale_);
        inspectedInput_.reset();
        if (hit >= 0 && hit < static_cast<int>(inputTargets_.size()))
        {
            const auto key = inputTargets_[hit];
            if (!target_ || key.nodeSeq != target_->nodeSeq || key.inputPortId != target_->inputPortId
                || key.imageIndex != target_->imageIndex)
            {
                emit roiSelected(key);
                event->accept();
                return;
            }
        }
    }
    if (event->button() == Qt::MiddleButton || event->button() == Qt::RightButton)
    {
        if (gesture_ != Gesture::None)
        {
            event->accept();
            return;
        }
        gesture_     = Gesture::Pan;
        panButton_   = event->button();
        anchor_      = event->position();
        lastPointer_ = event->position();
        event->accept();
        return;
    }
    // While regions are editable, a press on one of them keeps selecting, moving or resizing it;
    // only empty space belongs to the assistant. That is what lets a training view hold its picked
    // bands and still accept the next point-and-click.
    if (event->button() == Qt::LeftButton && drawShape_ == DrawNone && assistActive_ && hasImage()
        && gesture_ == Gesture::None && (!canEdit() || hitRoi(itemToImage(event->position())) < 0))
    {
        // A click is only a selection if the pointer does not travel; beyond the drag threshold the
        // same press turns into a pan, so inspecting the image never commits a candidate.
        gesture_     = Gesture::Assist;
        anchor_      = itemToImage(event->position());
        lastPointer_ = event->position();
        event->accept();
        return;
    }
    if (!canEdit() && event->button() == Qt::LeftButton && hasImage() && gesture_ == Gesture::None)
    {
        const auto point = itemToImage(event->position());
        const int  hit   = roiui::hit(inputs_, point, VisionRTSize::getInstance()->ROIEdgeHitTolerance() / scale_);
        if (hit >= 0 && hit < static_cast<int>(inputTargets_.size()))
        {
            event->accept();
            return;
        }
    }
    if (event->button() != Qt::LeftButton || !canEdit() || (gesture_ != Gesture::None && gesture_ != Gesture::Vertices))
    {
        event->ignore();
        return;
    }
    const auto point = itemToImage(event->position());
    if (gesture_ == Gesture::Vertices)
    {
        if (vertices_.empty() || roiui::length(point - vertices_.back()) * scale_ > 4)
            vertices_.push_back(point);
        draftChanged_ = true;
        overlaysUpdated();
        event->accept();
        return;
    }
    anchor_                 = point;
    handle_                 = -1;
    const bool isDrawingNew = drawShape_ >= DrawLine && drawShape_ <= DrawPoint;
    if (!isDrawingNew && selected_ >= 0 && selected_ < static_cast<int>(editable_.size()))
    {
        const auto controls = handles(editable_[selected_]);
        double     nearest  = std::numeric_limits<double>::infinity();
        for (std::size_t i = 0; i < controls.size(); ++i)
        {
            const double distance = roiui::length(point - controls[i]) * scale_;
            if (distance <= VisionRTSize::getInstance()->ROIHandleHitRadius() && distance < nearest)
            {
                handle_ = static_cast<int>(i);
                nearest = distance;
            }
        }
        const auto region = roiui::view(editable_[selected_]);
        if (region.kind() == RoiKind::Rectangle)
        {
            const auto &style = *VisionRTSize::getInstance();
            const auto rotations = roiui::rectangleRotationHandles(region.rectangle(), imageTransform(),
                                                                   style.ROIRotationArrowSize());
            for (std::size_t i = 0; i < rotations.size(); ++i)
            {
                const auto &control = rotations[i];
                const double distance = roiui::length(event->position() - control.center);
                if (control.bounds.containsPoint(event->position(), Qt::OddEvenFill) && distance < nearest)
                {
                    handle_ = static_cast<int>(i) + 5;
                    nearest = distance;
                }
            }
        }
        if (region.kind() == RoiKind::Circle
            && std::abs(roiui::length(point - roiui::point(region.circle().center())) - region.circle().radius())
                       * scale_
                   <= VisionRTSize::getInstance()->ROIEdgeHitTolerance()
            && std::abs(roiui::length(point - roiui::point(region.circle().center())) - region.circle().radius())
                   < roiui::length(point - roiui::point(region.circle().center())))
            handle_ = 1;
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
    else if (drawShape_ >= DrawLine && drawShape_ <= DrawPoint)
    {
        gesture_      = drawShape_ >= DrawPolygon ? Gesture::Vertices : Gesture::Create;
        draftChanged_ = false;
        if (gesture_ == Gesture::Vertices)
        {
            vertices_     = {point};
            draftChanged_ = true;
        }
    }
    updateCircleHandle(event->position());
    overlaysUpdated();
    event->accept();
}

void ImageViewport::changeDraft(QPointF point, Qt::KeyboardModifiers modifiers)
{
    try
    {
        if (gesture_ == Gesture::Create)
        {
            const auto delta = point - anchor_;
            draft_.reset();
            if (drawShape_ == DrawLine && roiui::length(delta) > epsilon)
                draft_ = makeSegment({anchor_.x(), anchor_.y()}, {point.x(), point.y()});
            else if (drawShape_ == DrawRectangle && std::abs(delta.x()) > epsilon && std::abs(delta.y()) > epsilon)
                draft_ = makeRectangle(std::min(anchor_.x(), point.x()), std::min(anchor_.y(), point.y()),
                                       std::abs(delta.x()), std::abs(delta.y()));
            else if (drawShape_ == DrawCircle && roiui::length(delta) > epsilon)
            {
                circleHandleDirection_ = delta / roiui::length(delta);
                draft_ = makeCircle({anchor_.x(), anchor_.y()}, roiui::length(delta));
            }
        }
        else if (original_
                 && (gesture_ == Gesture::Move
                     || (gesture_ == Gesture::Handle && handle_ == 0
                         && roiui::view(*original_).kind() != RoiKind::Point)))
        {
            auto draft = *original_;
            RegionEditor(draft).translate({point.x() - anchor_.x(), point.y() - anchor_.y()});
            draft_ = std::move(draft);
        }
        else if (original_ && gesture_ == Gesture::Handle)
        {
            draft_.reset();
            const auto region = roiui::view(*original_);
            if (region.kind() == RoiKind::Rectangle)
            {
                const auto rectangle = region.rectangle();
                const auto center    = roiui::point(rectangle.center());
                if (handle_ >= 5)
                {
                    const auto delta   = point - center;
                    const auto initial = anchor_ - center;
                    if (roiui::length(delta) > epsilon)
                    {
                        double angle = rectangle.angle()
                            + std::atan2(initial.x() * delta.y() - initial.y() * delta.x(),
                                         QPointF::dotProduct(initial, delta)) * 180.0 / pi;
                        if (modifiers.testFlag(Qt::ControlModifier))
                            angle = std::round(angle / 15.0) * 15.0;
                        draft_ = makeRectangle(rectangle.left(), rectangle.top(), rectangle.width(), rectangle.height(),
                                               angle);
                    }
                }
                else
                {
                    const auto delta = roiui::rotate(point - anchor_, -rectangle.angle());
                    double     width = rectangle.width(), height = rectangle.height();
                    QPointF    shift;
                    if (handle_ == 1 || handle_ == 3)
                    {
                        height += (handle_ == 1 ? -1 : 1) * delta.y();
                        shift.setY(delta.y() / 2);
                    }
                    else
                    {
                        width += (handle_ == 4 ? -1 : 1) * delta.x();
                        shift.setX(delta.x() / 2);
                    }
                    const auto newCenter = center + roiui::rotate(shift, rectangle.angle());
                    if (width > epsilon && height > epsilon)
                        draft_ = makeRectangle(newCenter.x() - width / 2, newCenter.y() - height / 2, width, height,
                                               rectangle.angle());
                }
            }
            else if (region.kind() == RoiKind::Circle)
            {
                const auto   circle = region.circle();
                const double radius = roiui::length(point - roiui::point(circle.center()));
                if (radius > epsilon)
                {
                    circleHandleDirection_ = (point - roiui::point(circle.center())) / radius;
                    draft_ = makeCircle(circle.center(), radius);
                }
            }
            else
            {
                auto draft = *original_;
                RegionEditor(draft).setVertex(
                    static_cast<std::size_t>(handle_ - (region.kind() == RoiKind::Point ? 0 : 1)),
                    {point.x(), point.y()});
                draft_ = std::move(draft);
            }
        }
        draftChanged_ = draft_.has_value() && (!original_ || *original_ != *draft_);
    }
    catch (const ContractError &)
    {
        draft_.reset();
        draftChanged_ = false;
    }
    overlaysUpdated();
    if (draft_ && target_)
    {
        auto key     = *target_;
        key.roiIndex = gesture_ == Gesture::Create ? -1 : selected_;
        emit roiDraftChanged(key, *draft_);
    }
}

void ImageViewport::mouseMoveEvent(QMouseEvent *event)
{
    pointerPosition_ = event->position();
    pointerInside_   = true;
    updatePixelProbe();
    if (gesture_ == Gesture::Assist)
    {
        const auto delta = event->position() - lastPointer_;
        if (delta.manhattanLength() > QGuiApplication::styleHints()->startDragDistance())
        {
            gesture_     = Gesture::Pan;
            panButton_   = Qt::LeftButton;
            lastPointer_ = event->position();
        }
        event->accept();
        return;
    }
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
        lastPointer_ = gestureInverse_.map(event->position());
        changeDraft(lastPointer_, event->modifiers());
        event->accept();
        return;
    }
    event->ignore();
}

void ImageViewport::mouseReleaseEvent(QMouseEvent *event)
{
    if (gesture_ == Gesture::Assist && event->button() == Qt::LeftButton)
    {
        const auto point  = itemToImage(event->position());
        const bool append = event->modifiers().testFlag(Qt::ControlModifier);
        clearGesture();
        event->accept();
        emit assistClicked(point, append);
        return;
    }
    if (gesture_ == Gesture::Pan && event->button() == panButton_)
    {
        const bool contextMenu
            = panButton_ == Qt::RightButton
           && (event->position() - anchor_).manhattanLength() <= QGuiApplication::styleHints()->startDragDistance();
        gesture_   = Gesture::None;
        panButton_ = Qt::NoButton;
        event->accept();
        if (contextMenu)
            emit contextMenuRequested(event->position());
        return;
    }
    if (event->button() == Qt::LeftButton && canEdit()
        && (gesture_ == Gesture::Create || gesture_ == Gesture::Move || gesture_ == Gesture::Handle))
    {
        changeDraft(gestureInverse_.map(event->position()), event->modifiers());
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
        if (vertices_.empty() || roiui::length(point - vertices_.back()) * scale_ > 4)
            vertices_.push_back(point);
        finishDraft();
        event->accept();
        return;
    }
    event->ignore();
}

void ImageViewport::mouseUngrabEvent()
{
    // Multi-click polygon/point drafts intentionally survive each click's ungrab.
    if (gesture_ == Gesture::Assist)
        clearGesture();
    else if (gesture_ != Gesture::Vertices)
        cancelDraft();
}

std::optional<EncodedRegion> ImageViewport::completedDraft() const
{
    if (gesture_ != Gesture::Vertices)
        return draft_;
    auto count = vertices_.size();
    if (drawShape_ == DrawPolygon && count > 1 && roiui::length(vertices_.front() - vertices_.back()) * scale_ <= 4)
        --count;
    std::vector<cv::Point2d> points;
    points.reserve(count);
    for (std::size_t index = 0; index < count; ++index) points.push_back({vertices_[index].x(), vertices_[index].y()});
    try
    {
        if (drawShape_ == DrawPolygon)
            return makePolygon(points);
        if (drawShape_ == DrawPoint)
            return makePoint(points);
    }
    catch (const ContractError &)
    {
    }
    return {};
}

void ImageViewport::commitDraft()
{
    const auto value = canEdit() && draftChanged_ ? completedDraft() : std::nullopt;
    if (!value)
    {
        clearGesture();
        emit roiDraftCanceled();
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
    setDrawShape(DrawNone);
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
    keyCommitTimer_.stop();
    pendingKeyCommit_.reset();
    auto key     = *target_;
    key.roiIndex = selected_;
    cancelDraft();
    static_cast<RoiOverlay *>(overlay_)->clear();
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
    if (canEdit() && selected_ >= 0 && gesture_ == Gesture::None
        && (event->key() == Qt::Key_Left || event->key() == Qt::Key_Right || event->key() == Qt::Key_Up
            || event->key() == Qt::Key_Down))
    {
        auto         value = editable_[selected_];
        const double step  = event->modifiers().testFlag(Qt::ShiftModifier) ? 10 : 1;
        RegionEditor(value).translate({event->key() == Qt::Key_Left    ? -step
                                       : event->key() == Qt::Key_Right ? step
                                                                       : 0,
                                       event->key() == Qt::Key_Up     ? -step
                                       : event->key() == Qt::Key_Down ? step
                                                                      : 0});
        editable_[selected_] = value;
        auto key             = *target_;
        key.roiIndex         = selected_;
        overlaysUpdated();
        if (!event->isAutoRepeat() && !pendingKeyCommit_)
        {
            emit roiCommitted(key, value);
        }
        else
        {
            pendingKeyCommit_ = std::make_pair(key, value);
            emit roiDraftChanged(key, value);
            keyCommitTimer_.start();
        }
        event->accept();
        return;
    }
    if (event->key() == Qt::Key_Escape && assistActive_ && !editing())
    {
        emit assistCanceled();
        event->accept();
    }
    else if (event->key() == Qt::Key_Escape && editing())
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

void ImageViewport::keyReleaseEvent(QKeyEvent *event)
{
    if (!event->isAutoRepeat() && pendingKeyCommit_
        && (event->key() == Qt::Key_Left || event->key() == Qt::Key_Right || event->key() == Qt::Key_Up
            || event->key() == Qt::Key_Down))
    {
        flushPendingKeyCommit();
        event->accept();
        return;
    }
    QQuickItem::keyReleaseEvent(event);
}

void ImageViewport::focusOutEvent(QFocusEvent *event)
{
    flushPendingKeyCommit();
    QQuickItem::focusOutEvent(event);
}

void ImageViewport::updateCursor()
{
    if ((drawShape_ != DrawNone && canEdit()) || (assistActive_ && hasImage()))
        setCursor(QCursor(Qt::CrossCursor));
    else
        unsetCursor();
}

void ImageViewport::hoverEnterEvent(QHoverEvent *event)
{
    pointerInside_   = true;
    pointerPosition_ = event->position();
    altHeld_         = event->modifiers().testFlag(Qt::AltModifier);
    updatePixelProbe();
    const bool circleChanged   = updateCircleHandle(event->position());
    const bool nextHoverCenter = computeHoverCenterHandle(event->position());
    const bool centerChanged   = nextHoverCenter != hoveringCenterHandle_;
    hoveringCenterHandle_      = nextHoverCenter;
    if (circleChanged || centerChanged || gesture_ == Gesture::Vertices)
        overlaysUpdated();
    updateCursor();
    if (assistActive_ && hasImage() && drawShape_ == DrawNone)
        emit assistHover(itemToImage(event->position()));
    QQuickItem::hoverEnterEvent(event);
}

void ImageViewport::hoverMoveEvent(QHoverEvent *event)
{
    pointerInside_   = true;
    pointerPosition_ = event->position();
    updatePixelProbe();
    const bool circleChanged   = updateCircleHandle(event->position());
    const bool nextHoverCenter = computeHoverCenterHandle(event->position());
    const bool centerChanged   = nextHoverCenter != hoveringCenterHandle_;
    hoveringCenterHandle_      = nextHoverCenter;
    if (circleChanged || centerChanged || gesture_ == Gesture::Vertices)
        overlaysUpdated();
    updateCursor();
    if (assistActive_ && hasImage() && drawShape_ == DrawNone)
        emit assistHover(itemToImage(event->position()));
    QQuickItem::hoverMoveEvent(event);
}

void ImageViewport::hoverLeaveEvent(QHoverEvent *event)
{
    pointerInside_ = false;
    updatePixelProbe();
    const bool wasHoverCenter = hoveringCenterHandle_;
    hoveringCenterHandle_     = false;
    if (wasHoverCenter || gesture_ == Gesture::Vertices)
        overlaysUpdated();
    unsetCursor();
    if (assistActive_)
        emit assistLeave();
    QQuickItem::hoverLeaveEvent(event);
}

} // namespace visionrt
