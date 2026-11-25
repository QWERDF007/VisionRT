#include "AppController_p.h"

#include <algorithm>

namespace visionrt {

void AppController::Impl::updateResults()
{
    presentation.updateResults();
    refreshAssist();
}

void AppController::Impl::updateDisplay()
{
    presentation.updateDisplay();
}

void AppController::attachViewport(QObject *object)
{
    auto *target = qobject_cast<ImageViewport *>(object);
    if (!target)
        return;
    const auto thumbnail = target->property("thumbnailIndex");
    if (thumbnail.isValid())
    {
        impl_->presentation.adoptThumbnail(target);
        QObject::connect(target, &QQuickItem::visibleChanged, this, [this] { impl_->updateDisplay(); });
        target->setReadOnly(true);
    }
    else if (impl_->presentation.viewport() != target)
    {
        if (auto *previous = impl_->presentation.viewport())
            QObject::disconnect(previous, nullptr, this, nullptr);
        impl_->presentation.adoptViewport(target);
        QObject::connect(target, &ImageViewport::imageChanged, this, &AppController::resultsChanged);
        QObject::connect(target, &ImageViewport::roiCommitted, this,
                         [this](const RoiEditTarget &key, const EncodedRegion &roi)
                         {
                             if (key.kind == RoiTargetKind::Training)
                                 impl_->commitTrainingRoi(key, &roi);
                             else
                                 impl_->commitRoi(key, &roi);
                         });
        QObject::connect(target, &ImageViewport::roiDeleted, this,
                         [this](const RoiEditTarget &key)
                         {
                             if (key.kind == RoiTargetKind::Training)
                                 impl_->commitTrainingRoi(key, nullptr);
                             else
                                 impl_->commitRoi(key, nullptr);
                         });
        QObject::connect(target, &ImageViewport::roiDraftChanged, this,
                         [this](const RoiEditTarget &key, const EncodedRegion &roi)
                         {
                             if (impl_->roiSession.previewGeometry(key, roi))
                             {
                                 emit parametersChanged();
                                 emit resultsChanged();
                             }
                         });
        QObject::connect(target, &ImageViewport::roiDraftCanceled, this,
                         [this]
                         {
                             impl_->roiSession.cancelPreview();
                             emit parametersChanged();
                         });
        QObject::connect(target, &ImageViewport::roiSelected, this,
                         [this](const RoiEditTarget &key)
                         {
                             // A training rectangle belongs to the training session; clicking it
                             // must not start a project input edit on the reference picture.
                             if (!impl_->idle() || impl_->trainingMode)
                                 return;
                             const auto *node = impl_->currentNode();
                             if (!node || node->id != key.nodeSeq)
                                 return;
                             const auto binding = node->inputs.find(key.inputPortId);
                             if (binding != node->inputs.end() && binding->second.mode == BindingMode::Upstream)
                             {
                                 const auto value = impl_->resolvedInput(*node, key.inputPortId);
                                 if (const auto *roi = shapeRegions(value); roi && !(*roi).empty())
                                 {
                                     const auto &group = (*roi).at((*roi).size() == 1 ? 0 : key.imageIndex);
                                     if (key.roiIndex >= 0 && key.roiIndex < static_cast<int>(group.size()))
                                         impl_->presentation.viewport()->inspectInputRegion(group[key.roiIndex]);
                                 }
                                 return;
                             }
                             if (binding != node->inputs.end() && binding->second.mode == BindingMode::Literal
                                 && editRoi(static_cast<int>(key.inputPortId), false))
                             {
                                 impl_->roiSession.setDisplayIndex(key.imageIndex);
                                 impl_->updateDisplay();
                                 if (auto *viewport = impl_->presentation.viewport())
                                     viewport->selectRegion(key.roiIndex);
                             }
                         });
        QObject::connect(target, &ImageViewport::roiEditCanceled, this,
                         [this]
                         {
                             // Leaving the window or pressing Esc ends the edit gesture, not the
                             // training view; the record and its draft stay selected.
                             if (impl_->trainingMode)
                                 return;
                             endRoiPreview();
                         });
        QObject::connect(target, &ImageViewport::drawShapeChanged, this, [this] { emit resultsChanged(); });
        QObject::connect(target, &ImageViewport::selectedRoiChanged, this, [this] { emit resultsChanged(); });
        QObject::connect(target, &ImageViewport::readOnlyChanged, this, [this] { emit resultsChanged(); });
        QObject::connect(target, &ImageViewport::editingChanged, this, [this] { emit resultsChanged(); });
    }
    impl_->updateDisplay();
}

QVariantList AppController::imageOutputs() const
{
    return impl_->presentation.imageOutputs();
}

int AppController::imageIndex() const
{
    return impl_->presentation.imageIndex();
}

int AppController::imageCount() const
{
    return impl_->presentation.imageCount();
}

QString AppController::selectedImageOutput() const
{
    return impl_->presentation.selectedImageOutput();
}

void AppController::setImageIndex(int index)
{
    // Which image of the current result is shown is view state, not a project edit: it may change
    // during a run and is presented as soon as the readback window allows. QML still hides the
    // control while the workspace is owned by a run.
    if (impl_->operations.isClosing())
        return;
    if (!impl_->presentation.setImageIndex(index))
        return;
    emit resultsChanged();
}

void AppController::setImageOutput(const QString &output)
{
    if (impl_->operations.isClosing())
        return;
    if (!impl_->presentation.setImageOutput(output))
        return;
    emit resultsChanged();
}

} // namespace visionrt
