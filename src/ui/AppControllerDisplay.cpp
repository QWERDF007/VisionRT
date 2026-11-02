#include "ui/AppController_p.h"

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
        QObject::connect(target, &ImageViewport::roiCommitted, this,
                         [this](const RoiEditTarget &key, const EncodedRegion &roi)
                         { impl_->commitRoi(key, &roi); });
        QObject::connect(target, &ImageViewport::roiDeleted, this,
                         [this](const RoiEditTarget &key) { impl_->commitRoi(key, nullptr); });
        QObject::connect(target, &ImageViewport::roiSelected, this,
                         [this](int /*index*/, QPointF /*point*/)
                         {
                             if (!impl_->idle())
                                 return;
                             const auto *node = impl_->currentNode();
                             const auto *tool = impl_->effectiveTool();
                             if (!node || !tool)
                                 return;
                             for (const auto &port : tool->inputs)
                             {
                                 if (port.type.type == Type::ROI)
                                 {
                                     const auto found = node->inputs.find(port.id);
                                     if (found != node->inputs.end() && found->second.mode == BindingMode::Literal)
                                     {
                                         editRoi(workbench::q(port.id), true);
                                         break;
                                     }
                                 }
                             }
                         });
        QObject::connect(target, &ImageViewport::roiEditCanceled, this, &AppController::endRoiPreview);
        QObject::connect(target, &ImageViewport::drawShapeChanged, this, [this] { emit resultsChanged(); });
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
