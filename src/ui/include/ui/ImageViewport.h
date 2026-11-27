#pragma once

#include "core/ToolDefinition.h"
#include "ui/DisplayConverter.h"
#include "ui/Export.h"

#include <QPointF>
#include <QPointer>
#include <QQuickItem>
#include <QQuickPaintedItem>
#include <QSize>
#include <QTimer>
#include <QTransform>
#include <limits>
#include <optional>
#include <string>
#include <utility>
#include <vector>

namespace visionrt {

// Which owner the edited regions belong to. A training record is not a node input, so the viewport
// carries the distinction instead of the controller guessing from a port number.
enum class RoiTargetKind
{
    Input,
    Training,
    Initialization,
    TrainingParameter
};

struct RoiEditTarget
{
    std::uint64_t nodeSeq     = 0;
    PortId        inputPortId = 0;
    int           imageIndex  = 0;
    int           roiIndex    = -1; // -1 appends a new ROI; existing edits/deletion use an index.
    std::uint64_t session     = 0;
    RoiTargetKind kind        = RoiTargetKind::Input;
    bool          operator==(const RoiEditTarget &) const = default;
};

struct VISIONRT_UI_API GeometryOverlay
{
    std::variant<Direction, CoordinateFrame2D> value;
    std::size_t image = 0;
    bool input = false;
    Affine2D alignment;
    static std::optional<GeometryOverlay> fromValue(const PortValue &value, int image, bool input,
                                                     Affine2D alignment = {});
};

class VISIONRT_UI_API ImageViewport : public QQuickItem
{
    Q_OBJECT
    Q_PROPERTY(bool hasImage READ hasImage NOTIFY imageChanged)
    Q_PROPERTY(QString statusText READ statusText NOTIFY statusTextChanged)
    Q_PROPERTY(double zoom READ zoom NOTIFY transformChanged)
    Q_PROPERTY(bool editing READ editing NOTIFY editingChanged)
    Q_PROPERTY(bool singleRectangle READ singleRectangle NOTIFY editingChanged)
    Q_PROPERTY(bool readOnly READ readOnly WRITE setReadOnly NOTIFY readOnlyChanged)
    Q_PROPERTY(int drawShape READ drawShape WRITE setDrawShape NOTIFY drawShapeChanged)
    Q_PROPERTY(int selectedRoi READ selectedRoi NOTIFY selectedRoiChanged)
    Q_PROPERTY(bool assistActive READ assistActive WRITE setAssistActive NOTIFY assistActiveChanged)
    Q_PROPERTY(QString pixelText READ pixelText NOTIFY pixelProbeChanged)
    Q_PROPERTY(QPointF pointerPosition READ pointerPosition NOTIFY pixelProbeChanged)
public:
    enum DrawShape
    {
        DrawNone      = 0,
        DrawLine      = 1,
        DrawRectangle = 2,
        DrawCircle    = 3,
        DrawPolygon   = 4,
        DrawPoint     = 5
    };
    Q_ENUM(DrawShape)
    static DrawShape drawShapeForType(Type type) noexcept
    {
        switch (type)
        {
#define VRT_SHAPE(name, kind, view, code, wire, label) case Type::name: return Draw##name;
#include <visionrt/contracts/ShapeKinds.def>
#undef VRT_SHAPE
        default: return DrawNone;
        }
    }

    enum ViewMode
    {
        Contain,
        Cover,
        OneToOne,
        Manual
    };
    Q_ENUM(ViewMode)
    explicit ImageViewport(QQuickItem *parent = nullptr);
    ~ImageViewport() override;

    // All methods belong to the GUI thread. Request ids increase within a generation.
    // For a shared converter serving the main view and all visible thumbnails.
    void    setDisplayFrame(const DisplayFrame &frame);
    void    clearImage(quint64 requestId);
    // Shows a host-owned picture (the selected training record) without consuming the result
    // pipeline's request identity; the next result frame replaces it in the ordinary way.
    void    setHostFrame(const DisplayFrame &frame);
    QString pixelTextAt(QPointF itemPoint) const;

    QString pixelText() const
    {
        return pixelText_;
    }

    QPointF pointerPosition() const
    {
        return pointerPosition_;
    }

    // The full-resolution picture on screen. Training saves exactly these pixels, never a thumbnail.
    QImage displayedImage() const
    {
        return display_;
    }

    void setGeneration(quint64 generation);
    void setOverlays(RegionGroup inputs, RegionGroup outputs, std::vector<RoiEditTarget> targets = {});
    void setGeometryOverlays(std::vector<GeometryOverlay> values);
    void selectRegion(int index);
    void previewGeometry(const RegionGroup &regions);
    void inspectInputRegion(const EncodedRegion &region);
    // Caller freezes the matching background and performs the project edit command.
    void beginRoiEdit(RoiEditTarget target, RegionGroup targetRois, bool readOnly = false,
                      bool singleRectangle = false);
    void endRoiEdit();
    // Transient assist candidate in current image coordinates. It never reaches the project and
    // never claims to be a formal result.
    void setAssistPreview(ROI preview);
    void setAssistActive(bool value);

    bool hasImage() const noexcept
    {
        return !display_.isNull();
    }

    QString statusText() const
    {
        return statusText_;
    }

    double zoom() const noexcept
    {
        return scale_;
    }

    bool editing() const noexcept
    {
        return target_.has_value();
    }

    const std::optional<RoiEditTarget> &editTarget() const noexcept
    {
        return target_;
    }

    bool singleRectangle() const noexcept
    {
        return singleRectangle_;
    }

    bool readOnly() const noexcept
    {
        return readOnly_;
    }

    void setReadOnly(bool value);

    int drawShape() const noexcept
    {
        return drawShape_;
    }

    void setDrawShape(int shape);

    int selectedRoi() const noexcept
    {
        return selected_;
    }

    bool assistActive() const noexcept
    {
        return assistActive_;
    }

    QPointF    imageToItem(QPointF point) const;
    QPointF    itemToImage(QPointF point) const;
    QTransform imageTransform() const;

    struct ViewState
    {
        ViewMode mode;
        double   scale;
        QPointF  imageCenter;
    };

    ViewState viewState() const
    {
        return {mode_, scale_, center_};
    }

    void restoreViewState(const ViewState &state);

    Q_INVOKABLE void fitContain();
    Q_INVOKABLE void fitCover();
    Q_INVOKABLE void fitOneToOne();
    Q_INVOKABLE void zoomBy(double factor);
    Q_INVOKABLE void cancelDraft();
    void             finishDraft();
    Q_INVOKABLE void deleteSelectedRoi();

signals:
    void pixelProbeChanged();
    void imageChanged();
    void statusTextChanged();
    void transformChanged();
    void editingChanged();
    void readOnlyChanged();
    void drawShapeChanged();
    void selectedRoiChanged();
    void assistActiveChanged();
    void imagePresented(quint64 requestId);
    // Cursor in image coordinates. The host debounces and converts these into bounded requests.
    void assistHover(const QPointF &imagePoint);
    void assistLeave();
    void assistClicked(const QPointF &imagePoint, bool append);
    void assistCanceled();
    void roiCommitted(const visionrt::RoiEditTarget &target, const visionrt::EncodedRegion &roi);
    void roiDraftChanged(const visionrt::RoiEditTarget &target, const visionrt::EncodedRegion &roi);
    void roiDraftCanceled();
    void roiDeleted(const visionrt::RoiEditTarget &target);
    void roiEditCanceled();
    void roiSelected(const visionrt::RoiEditTarget &target);

protected:
    QSGNode *updatePaintNode(QSGNode *oldNode, UpdatePaintNodeData *) override;
    void     geometryChange(const QRectF &newGeometry, const QRectF &oldGeometry) override;
    void     mousePressEvent(QMouseEvent *event) override;
    void     mouseMoveEvent(QMouseEvent *event) override;
    void     mouseReleaseEvent(QMouseEvent *event) override;
    void     mouseDoubleClickEvent(QMouseEvent *event) override;
    void     mouseUngrabEvent() override;
    void     wheelEvent(QWheelEvent *event) override;
    void     keyPressEvent(QKeyEvent *event) override;
    void     keyReleaseEvent(QKeyEvent *event) override;
    void     focusOutEvent(QFocusEvent *event) override;
    void     hoverEnterEvent(QHoverEvent *event) override;
    void     hoverMoveEvent(QHoverEvent *event) override;
    void     hoverLeaveEvent(QHoverEvent *event) override;
    bool     event(QEvent *event) override;
    bool     eventFilter(QObject *watched, QEvent *event) override;

private:
    friend class RoiOverlay;
    enum class Gesture
    {
        None,
        Pan,
        Create,
        Move,
        Handle,
        Vertices,
        Assist
    };
    void                         applyFrame(const DisplayFrame &frame);
    void                         recalculateFit();
    void                         transformUpdated();
    void                         zoomAt(double factor, QPointF itemPoint);
    void                         selectRoi(int index);
    bool                         canEdit() const;
    void                         changeDraft(QPointF imagePoint, Qt::KeyboardModifiers modifiers = Qt::NoModifier);
    std::optional<EncodedRegion> completedDraft() const;
    std::vector<QPointF>         handles(const EncodedRegion &roi) const;
    const EncodedRegion        *activeRegion() const;
    bool                         updateCircleHandle(QPointF itemPoint);
    bool                         computeHoverCenterHandle(QPointF itemPoint) const;
    void                         flushPendingKeyCommit();
    int                          hitRoi(QPointF imagePoint) const;
    void                         commitDraft();
    void                         clearGesture();
    void                         setStatus(QString text);
    void                         updateCursor();
    void                         overlaysUpdated();
    void                         updatePixelProbe();
    QQuickPaintedItem           *overlay_ = nullptr;
    std::vector<RoiEditTarget>   inputTargets_;
    quint64                      uploadedRevision_   = std::numeric_limits<quint64>::max();
    qint64                       uploadedTextureKey_ = 0;

    QMetaObject::Connection      windowActiveConnection_;
    QPointer<QQuickWindow>       observedWindow_;
    QImage                       display_;
    int                          sourceType_    = -1;
    bool                         pointerInside_ = false;
    bool                         altHeld_ = false;
    QPointF                      pointerPosition_;
    QPointF                      lastProbePosition_;
    QString                      pixelText_;
    bool                         hoveringCenterHandle_ = false;
    QTimer                       keyCommitTimer_;
    std::optional<std::pair<RoiEditTarget, EncodedRegion>> pendingKeyCommit_;
    QSize                        imageSize_;
    QString                      statusText_;
    quint64                      generation_      = 0;
    quint64                      requestId_       = 0;
    quint64                      textureRevision_ = 0;
    bool                         requested_       = false;
    bool                         firstImage_      = true;
    ViewMode                     mode_            = Contain;
    double                       scale_           = 1;
    QPointF                      center_;
    RegionGroup                  inputs_;
    RegionGroup                  outputs_;
    std::vector<GeometryOverlay> geometry_;
    RegionGroup                  editable_;
    ROI                          assistPreview_;
    bool                         assistActive_ = false;
    std::optional<RoiEditTarget> target_;
    bool                         singleRectangle_ = false;
    bool                         readOnly_        = true;
    int                          drawShape_       = DrawNone;
    int                          selected_        = -1;
    Gesture                      gesture_         = Gesture::None;
    Qt::MouseButton              panButton_       = Qt::NoButton;
    QPointF                      anchor_;
    QTransform                   gestureInverse_;
    QPointF                      lastPointer_;
    QPointF                      circleHandleDirection_{1, 0};
    int                          handle_ = -1;
    std::optional<EncodedRegion> original_;
    std::optional<EncodedRegion> draft_;
    std::optional<EncodedRegion> inspectedInput_;
    std::vector<QPointF>         vertices_;
    bool                         draftChanged_ = false;
};

} // namespace visionrt

Q_DECLARE_METATYPE(visionrt::EncodedRegion)
Q_DECLARE_METATYPE(visionrt::RoiEditTarget)
