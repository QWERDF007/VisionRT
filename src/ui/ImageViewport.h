#pragma once

#include "ui/DisplayConverter.h"

#include <QPointF>
#include <QQuickItem>
#include <QSize>
#include <QTransform>
#include <optional>
#include <string>
#include <vector>

namespace visionrt {

struct RoiEditTarget
{
    std::uint64_t nodeSeq = 0;
    std::string   inputPortId;
    int           imageIndex = 0;
    int           roiIndex   = -1; // -1 appends a new ROI; existing edits/deletion use an index.
    bool          operator==(const RoiEditTarget &) const = default;
};

class ImageViewport : public QQuickItem
{
    Q_OBJECT
    Q_PROPERTY(bool hasImage READ hasImage NOTIFY imageChanged)
    Q_PROPERTY(QString statusText READ statusText NOTIFY statusTextChanged)
    Q_PROPERTY(double zoom READ zoom NOTIFY transformChanged)
    Q_PROPERTY(ViewMode viewMode READ viewMode NOTIFY transformChanged)
    Q_PROPERTY(bool editing READ editing NOTIFY editingChanged)
    Q_PROPERTY(bool readOnly READ readOnly WRITE setReadOnly NOTIFY readOnlyChanged)
    Q_PROPERTY(int drawShape READ drawShape WRITE setDrawShape NOTIFY drawShapeChanged)
    Q_PROPERTY(int selectedRoi READ selectedRoi NOTIFY selectedRoiChanged)
public:
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
    void setDisplayFrame(const DisplayFrame &frame);
    void setImage(OwnedImage image, const QString &colorOrder, quint64 requestId);
    void clearImage(quint64 requestId);
    void setGeneration(quint64 generation);
    void setOverlays(std::vector<Roi> inputs, std::vector<Roi> outputs);
    // Caller freezes the matching background and performs the project edit command.
    // While editing, targetRois replaces input overlays; outputs remain read-only.
    void beginRoiEdit(RoiEditTarget target, std::vector<Roi> targetRois, bool readOnly = false);
    void endRoiEdit();

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

    ViewMode viewMode() const noexcept
    {
        return mode_;
    }

    bool editing() const noexcept
    {
        return target_.has_value();
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
    Q_INVOKABLE void finishDraft();
    Q_INVOKABLE void deleteSelectedRoi();

signals:
    void imageChanged();
    void statusTextChanged();
    void transformChanged();
    void editingChanged();
    void readOnlyChanged();
    void drawShapeChanged();
    void selectedRoiChanged();
    void imagePresented(quint64 requestId);
    void roiCommitted(const visionrt::RoiEditTarget &target, const visionrt::Roi &roi);
    void roiDeleted(const visionrt::RoiEditTarget &target);

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
    bool     event(QEvent *event) override;

private:
    enum class Gesture
    {
        None,
        Pan,
        Create,
        Move,
        Handle,
        Vertices
    };
    void                 applyFrame(const DisplayFrame &frame);
    void                 recalculateFit();
    void                 transformUpdated();
    void                 zoomAt(double factor, QPointF itemPoint);
    void                 selectRoi(int index);
    bool                 canEdit() const;
    void                 changeDraft(QPointF imagePoint);
    std::optional<Roi>   completedDraft() const;
    std::vector<QPointF> handles(const Roi &roi) const;
    int                  hitRoi(QPointF imagePoint) const;
    void                 commitDraft();
    void                 setStatus(QString text);
    void                 overlaysUpdated();

    std::unique_ptr<DisplayConverter> converter_;
    QImage                            display_;
    QSize                             imageSize_;
    QString                           statusText_;
    quint64                           generation_      = 0;
    quint64                           requestId_       = 0;
    quint64                           textureRevision_ = 0;
    quint64                           overlayRevision_ = 0;
    bool                              requested_       = false;
    bool                              firstImage_      = true;
    ViewMode                          mode_            = Contain;
    double                            scale_           = 1;
    QPointF                           center_;
    std::vector<Roi>                  inputs_;
    std::vector<Roi>                  outputs_;
    std::vector<Roi>                  editable_;
    std::optional<RoiEditTarget>      target_;
    bool                              readOnly_  = true;
    int                               drawShape_ = 0;
    int                               selected_  = -1;
    Gesture                           gesture_   = Gesture::None;
    Qt::MouseButton                   panButton_ = Qt::NoButton;
    QPointF                           anchor_;
    QPointF                           lastPointer_;
    int                               handle_ = -1;
    std::optional<Roi>                original_;
    std::optional<Roi>                draft_;
    std::vector<QPointF>              vertices_;
    bool                              draftChanged_ = false;
};

} // namespace visionrt

Q_DECLARE_METATYPE(visionrt::Roi)
Q_DECLARE_METATYPE(visionrt::RoiEditTarget)
