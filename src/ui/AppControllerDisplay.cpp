#include "ui/AppController_p.h"

#include <algorithm>

namespace visionrt {
namespace {
std::vector<Roi> imageRois(const PortValue &value, int index)
{
    if (const auto *rectangle = std::get_if<Roi>(&value))
        return {*rectangle};
    const auto *rois = std::get_if<RoiBatch>(&value);
    if (!rois || index < 0 || static_cast<std::size_t>(index) >= rois->images().size())
        return {};
    return rois->images()[index];
}

const ImageBatch *images(const ResultSlot::Snapshot &snapshot, const std::string &port)
{
    if (!snapshot)
        return nullptr;
    const auto value = snapshot->outputs.find(port);
    return value == snapshot->outputs.end() ? nullptr : std::get_if<ImageBatch>(&value->second);
}
} // namespace

ResultSlot::Snapshot AppController::Impl::displaySnapshot(const NodeDefinition &node) const
{
    const auto found = snapshots.find(node.id);
    if (found == snapshots.end())
        return {};
    const auto &snapshot = found->second;
    if (snapshot->toolId != node.toolId)
        return {};
    for (const auto &[port, value] : snapshot->outputs)
    {
        const auto declaration = node.signatures.outputs.find(port);
        if (declaration == node.signatures.outputs.end())
            return {};
        if (isMissing(value))
            continue;
        const auto actual = typeOf(value);
        if (!actual || !compatible(*actual, declaration->second))
            return {};
    }
    return snapshot;
}

void AppController::Impl::updateResults()
{
    const auto *node = currentNode();
    const auto *tool = currentTool();
    if (!node)
    {
        outputs.clear();
        imageOutput.clear();
        index = 0;
    }
    else
    {
        const auto snapshot = displaySnapshot(*node);
        std::map<std::string, std::size_t, std::less<>> references;
        if (const auto *current = currentFlow())
            for (const auto &relation : portRelations(*current).active)
                if (relation.source.node == node->id)
                    ++references[relation.source.port];
        outputs.setResult(tool, snapshot, references, &node->signatures.outputs);
        std::vector<std::string> imagePorts;
        for (const auto &[id, type] : node->signatures.outputs)
            if (type.type == Type::ImageBatch)
                imagePorts.push_back(id);
        if (std::find(imagePorts.begin(), imagePorts.end(), imageOutput.toStdString()) == imagePorts.end())
            imageOutput = imagePorts.empty() ? QString{} : workbench::q(imagePorts.front());
    }
    updateDisplay();
    emit q->resultsChanged();
}

void AppController::Impl::applyOverlays()
{
    if (!viewport)
        return;
    viewport->setOverlays(displayInputs, displayOutputs);
    if (displayRoiTarget)
        viewport->beginRoiEdit(*displayRoiTarget, imageRois(roi->draft, roi->index), state != RuntimeState::Idle,
                               roi->type.type);
    else
        viewport->endRoiEdit();
}

void AppController::Impl::updateDisplay()
{
    if (roi)
    {
        const auto *node = currentNode();
        const auto *declaration = parameter(roi->input, false);
        if (!node || node->id != roi->node || !declaration
            || !equalValues(roi->background, resolvedInput(*node, declaration->imageInput)))
            finishRoi();
    }
    const ImageBatch    *batch = nullptr;
    ResultSlot::Snapshot snapshot;
    QString              color;
    std::string          imageInput;
    displayInputs.clear();
    displayOutputs.clear();
    displayRoiTarget.reset();
    int selectedIndex = index;
    if (const auto *node = currentNode())
        snapshot = displaySnapshot(*node);
    std::string fallbackInputPort;
    if (!roi)
    {
        batch = images(snapshot, imageOutput.toStdString());
        const bool executionFailed = snapshot && snapshot->status.code != 0;
        if ((!batch || batch->images().empty()) && executionFailed && currentNode())
        {
            const auto *node = currentNode();
            if (const auto *tool = currentTool())
            {
                const auto *selectedImage = workbench::port(tool->outputs, imageOutput.toStdString());
                if (selectedImage && !selectedImage->sameGeometryAsInput.empty())
                {
                    if (const auto *val = resolvedInputRef(*node, selectedImage->sameGeometryAsInput))
                    {
                        if (const auto *inputBatch = std::get_if<ImageBatch>(val); inputBatch && !inputBatch->images().empty())
                        {
                            batch = inputBatch;
                            fallbackInputPort = selectedImage->sameGeometryAsInput;
                        }
                    }
                }
                if (!batch || batch->images().empty())
                {
                    for (const auto &port : tool->inputs)
                    {
                        if (port.type.type == Type::ImageBatch)
                        {
                            if (const auto *val = resolvedInputRef(*node, port.id))
                            {
                                if (const auto *inputBatch = std::get_if<ImageBatch>(val); inputBatch && !inputBatch->images().empty())
                                {
                                    batch = inputBatch;
                                    fallbackInputPort = port.id;
                                    break;
                                }
                            }
                        }
                    }
                }
            }
        }
        count = batch ? static_cast<int>(batch->images().size()) : 0;
        index = std::clamp(index, 0, std::max(0, count - 1));
        selectedIndex = index;
    }
    if (roi)
    {
        batch            = std::get_if<ImageBatch>(&roi->background);
        selectedIndex    = roi->index;
        displayRoiTarget = RoiEditTarget{roi->node, roi->input, selectedIndex,
                                        std::holds_alternative<Roi>(roi->draft) ? 0 : -1, roi->id};
        const auto *declaration = parameter(roi->input, false);
        const auto *node = currentNode();
        imageInput = declaration->imageInput;
        if (const auto *tool = currentTool())
            for (const auto &port : tool->inputs)
                if (port.id != roi->input && (port.type.type == Type::RoiBatch || port.type.type == Type::Rect)
                    && port.imageInput == imageInput)
                {
                    auto values = imageRois(resolvedInput(*node, port.id), selectedIndex);
                    displayInputs.insert(displayInputs.end(), values.begin(), values.end());
                }
    }
    else if (const auto *node = currentNode())
    {
        if (const auto *tool = currentTool())
        {
            const auto *image = workbench::port(tool->outputs, imageOutput.toStdString());
            if (image)
                color = workbench::q(image->colorOrder);

            if (!fallbackInputPort.empty())
            {
                if (color.isEmpty())
                {
                    if (const auto *inPort = workbench::port(tool->inputs, fallbackInputPort))
                        color = workbench::q(inPort->colorOrder);
                }
                for (const auto &port : tool->inputs)
                {
                    if ((port.type.type == Type::RoiBatch || port.type.type == Type::Rect)
                        && port.imageInput == fallbackInputPort)
                    {
                        if (snapshot)
                        {
                            const auto value = snapshot->roiInputs.find(port.id);
                            if (value != snapshot->roiInputs.end())
                            {
                                auto values = imageRois(value->second, selectedIndex);
                                displayInputs.insert(displayInputs.end(), values.begin(), values.end());
                                continue;
                            }
                        }
                        if (const auto *val = resolvedInputRef(*node, port.id))
                        {
                            auto values = imageRois(*val, selectedIndex);
                            displayInputs.insert(displayInputs.end(), values.begin(), values.end());
                        }
                    }
                }
            }
            else if (image)
            {
                if (!image->sameGeometryAsInput.empty() && snapshot)
                    for (const auto &port : tool->inputs)
                        if ((port.type.type == Type::RoiBatch || port.type.type == Type::Rect)
                            && port.imageInput == image->sameGeometryAsInput)
                        {
                            const auto value = snapshot->roiInputs.find(port.id);
                            if (value != snapshot->roiInputs.end())
                            {
                                auto values = imageRois(value->second, selectedIndex);
                                displayInputs.insert(displayInputs.end(), values.begin(), values.end());
                            }
                        }
            }
        }
    }
    if (const auto *tool = currentTool(); tool && snapshot)
        for (const auto &port : tool->outputs)
        {
            if (port.type.type != Type::RoiBatch && port.type.type != Type::Rect)
                continue;
            const auto *image = workbench::port(tool->outputs, port.imagePort);
            if (!image || (roi ? image->sameGeometryAsInput != imageInput : image->id != imageOutput.toStdString()))
                continue;
            const auto value = snapshot->outputs.find(port.id);
            if (value != snapshot->outputs.end())
            {
                auto values = imageRois(value->second, selectedIndex);
                displayOutputs.insert(displayOutputs.end(), values.begin(), values.end());
            }
        }
    std::vector<OwnedImage>              requestedImages;
    std::vector<QPointer<ImageViewport>> targets;
    std::vector<QPointer<ImageViewport>> empty;
    auto                                 append = [&](QPointer<ImageViewport> target, int itemIndex)
    {
        if (!target)
            return;
        target->setGeneration(generation);
        if (!batch || itemIndex < 0 || static_cast<std::size_t>(itemIndex) >= batch->images().size())
        {
            empty.push_back(target);
            return;
        }
        requestedImages.push_back(batch->images()[itemIndex]);
        targets.push_back(target);
    };
    append(viewport, selectedIndex);
    std::erase_if(thumbnails, [](const auto &target) { return target.isNull(); });
    for (const auto &target : thumbnails)
    {
        if (target->isVisible())
            append(target, target->property("thumbnailIndex").toInt());
    }
    const bool identical = targets == displayTargets && color == displayColor
                        && requestedImages.size() == displayImages.size()
                        && std::equal(requestedImages.begin(), requestedImages.end(), displayImages.begin(),
                                      [](const auto &a, const auto &b)
                                      {
                                          return a.owner() == b.owner() && a.data() == b.data() && a.rows() == b.rows()
                                              && a.cols() == b.cols();
                                      });
    if (identical)
    {
        for (const auto &target : empty)
        {
            target->clearImage(displayRequest);
            target->setOverlays({}, {});
            target->endRoiEdit();
        }
        if (presentedRequest == displayRequest && std::find(targets.begin(), targets.end(), viewport) != targets.end())
            applyOverlays();
        return;
    }
    ++displayRequest;
    displayTargets = std::move(targets);
    displayImages  = requestedImages;
    displayColor   = color;
    for (const auto &target : empty)
    {
        target->clearImage(displayRequest);
        target->setOverlays({}, {});
        target->endRoiEdit();
    }
    if (requestedImages.empty())
    {
        converter.invalidate(generation);
        return;
    }
    converter.requestBatch(std::move(requestedImages), std::move(color), displayRequest, generation);
}

const PortValue *AppController::Impl::resolvedInputRef(const NodeDefinition &node, const std::string &input) const
{
    const auto found = node.inputs.find(input);
    if (found == node.inputs.end())
        return nullptr;
    if (found->second.mode == BindingMode::Literal)
        return &found->second.literal;
    if (!found->second.source)
        return nullptr;
    const auto snapshot = snapshots.find(found->second.source->node);
    if (snapshot == snapshots.end())
        return nullptr;
    const auto output = snapshot->second->outputs.find(found->second.source->port);
    return output == snapshot->second->outputs.end() ? nullptr : &output->second;
}

PortValue AppController::Impl::resolvedInput(const NodeDefinition &node, const std::string &input) const
{
    const auto *ref = resolvedInputRef(node, input);
    return ref ? *ref : PortValue{Missing{}};
}


void AppController::attachViewport(QObject *object)
{
    auto *target = qobject_cast<ImageViewport *>(object);
    if (!target)
        return;
    const auto thumbnail = target->property("thumbnailIndex");
    if (thumbnail.isValid())
    {
        if (std::find(impl_->thumbnails.begin(), impl_->thumbnails.end(), target) == impl_->thumbnails.end())
        {
            impl_->thumbnails.push_back(target);
            QObject::connect(target, &QQuickItem::visibleChanged, this, [this] { impl_->updateDisplay(); });
        }
        target->setReadOnly(true);
    }
    else if (impl_->viewport != target)
    {
        if (impl_->viewport)
            QObject::disconnect(impl_->viewport, nullptr, this, nullptr);
        impl_->viewport = target;
        QObject::connect(target, &ImageViewport::roiCommitted, this,
                         [this](const RoiEditTarget &key, const Roi &roi) { impl_->commitRoi(key, &roi); });
        QObject::connect(target, &ImageViewport::roiDeleted, this,
                         [this](const RoiEditTarget &key) { impl_->commitRoi(key, nullptr); });
        QObject::connect(target, &ImageViewport::roiEditCanceled, this, &AppController::endRoiPreview);
        QObject::connect(target, &ImageViewport::drawShapeChanged, this, [this] { emit resultsChanged(); });
    }
    impl_->updateDisplay();
}

QVariantList AppController::imageOutputs() const
{
    QVariantList result;
    if (impl_->roi)
    {
        result.push_back(QVariantMap{
            {  "id",                                               "__input_preview"},
            {"name", QStringLiteral("输入预览 · ") + workbench::q(impl_->roi->input)}
        });
        return result;
    }
    if (const auto *tool = impl_->currentTool())
    {
        const auto *node = impl_->currentNode();
        for (const auto &output : tool->outputs)
            if (node && node->signatures.outputs.contains(output.id)
                && node->signatures.outputs.at(output.id).type == Type::ImageBatch)
                result.push_back(QVariantMap{
                    {  "id",   workbench::q(output.id)},
                    {"name", workbench::q(output.name)}
                });
    }
    else if (const auto *node = impl_->currentNode())
        for (const auto &[id, type] : node->signatures.outputs)
            if (type.type == Type::ImageBatch)
                result.push_back(QVariantMap{
                    {  "id", workbench::q(id)},
                    {"name", workbench::q(id)}
                });
    return result;
}

int AppController::imageIndex() const
{
    return impl_->roi ? impl_->roi->index : impl_->index;
}

int AppController::imageCount() const
{
    if (!impl_->roi)
        return impl_->count;
    const auto *images = std::get_if<ImageBatch>(&impl_->roi->background);
    return images ? static_cast<int>(images->images().size()) : 0;
}

QString AppController::selectedImageOutput() const
{
    return impl_->roi ? QStringLiteral("__input_preview") : impl_->imageOutput;
}

void AppController::setImageIndex(int index)
{
    const int clamped = std::clamp(index, 0, std::max(0, imageCount() - 1));
    if (clamped == imageIndex())
        return;
    if (impl_->viewport)
        impl_->viewport->cancelDraft();
    if (impl_->roi)
        impl_->roi->index = clamped;
    else
        impl_->index = clamped;
    impl_->updateDisplay();
    emit resultsChanged();
}

void AppController::setImageOutput(const QString &output)
{
    if (impl_->roi || impl_->imageOutput == output)
        return;
    const auto choices = imageOutputs();
    if (std::none_of(choices.begin(), choices.end(),
                     [&](const auto &choice) { return choice.toMap().value("id").toString() == output; }))
        return;
    impl_->imageOutput = output;
    impl_->updateDisplay();
    emit resultsChanged();
}

} // namespace visionrt
