#include "ui/AppController_p.h"

#include <algorithm>

namespace visionrt {
namespace {
std::vector<Roi> imageRois(const PortValue &value, int index)
{
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

void AppController::Impl::updateResults(bool newSnapshot)
{
    const auto *node = currentNode();
    const auto *tool = currentTool();
    if (!node)
    {
        outputs.clear();
        imageOutput.clear();
        index = count = 0;
    }
    else
    {
        const auto found    = snapshots.find(node->id);
        const auto snapshot = found == snapshots.end() ? ResultSlot::Snapshot{} : found->second;
        std::map<std::string, std::size_t, std::less<>> references;
        for (const auto &[id, type] : node->signatures.outputs)
            references[id] = editor.referenceCount(flow.toStdString(), {node->id, id});
        outputs.setResult(tool, snapshot, references, &node->signatures.outputs);
        std::vector<std::string> imagePorts;
        if (tool)
        {
            for (const auto &output : tool->outputs)
                if (output.type.type == Type::ImageBatch)
                    imagePorts.push_back(output.id);
        }
        else
            for (const auto &[id, type] : node->signatures.outputs)
                if (type.type == Type::ImageBatch)
                    imagePorts.push_back(id);
        if (std::find(imagePorts.begin(), imagePorts.end(), imageOutput.toStdString()) == imagePorts.end())
            imageOutput = imagePorts.empty() ? QString{} : workbench::q(imagePorts.front());
        const auto *reference = imagePorts.empty() ? nullptr : images(snapshot, imagePorts.front());
        count                 = reference ? static_cast<int>(reference->images().size()) : 0;
        if (newSnapshot || index >= count)
            index = std::clamp(index, 0, std::max(0, count - 1));
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
        viewport->beginRoiEdit(*displayRoiTarget, displayInputs, state != RuntimeState::Idle);
    else
        viewport->endRoiEdit();
}

void AppController::Impl::updateDisplay()
{
    const ImageBatch    *batch = nullptr;
    ResultSlot::Snapshot snapshot;
    QString              color;
    displayInputs.clear();
    displayOutputs.clear();
    displayRoiTarget.reset();
    int selectedIndex = index;
    if (roi)
    {
        batch            = &roi->images;
        selectedIndex    = roi->index;
        displayInputs    = imageRois(roi->rois, selectedIndex);
        displayRoiTarget = RoiEditTarget{roi->node, roi->input, selectedIndex, -1};
    }
    else if (const auto *node = currentNode())
    {
        const auto found = snapshots.find(node->id);
        if (found != snapshots.end())
            snapshot = found->second;
        batch = images(snapshot, imageOutput.toStdString());
        if (const auto *tool = currentTool())
        {
            const auto *image = workbench::port(tool->outputs, imageOutput.toStdString());
            if (image)
            {
                color = workbench::q(image->colorOrder);
                for (const auto &port : tool->outputs)
                    if (port.type.type == Type::RoiBatch && port.imagePort == image->id && snapshot)
                    {
                        const auto value = snapshot->outputs.find(port.id);
                        if (value != snapshot->outputs.end())
                        {
                            auto values = imageRois(value->second, selectedIndex);
                            displayOutputs.insert(displayOutputs.end(), values.begin(), values.end());
                        }
                    }
                if (!image->sameGeometryAsInput.empty() && snapshot)
                    for (const auto &port : tool->inputs)
                        if (port.type.type == Type::RoiBatch && port.imageInput == image->sameGeometryAsInput)
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

PortValue AppController::Impl::resolvedInput(const NodeDefinition &node, const std::string &input) const
{
    const auto found = node.inputs.find(input);
    if (found == node.inputs.end())
        return Missing{};
    if (found->second.mode == BindingMode::Literal)
        return found->second.literal;
    if (!found->second.source)
        return Missing{};
    const auto snapshot = snapshots.find(found->second.source->node);
    if (snapshot == snapshots.end())
        return Missing{};
    const auto output = snapshot->second->outputs.find(found->second.source->port);
    return output == snapshot->second->outputs.end() ? PortValue{Missing{}} : output->second;
}

void AppController::Impl::finishRoi()
{
    if (!roi)
        return;
    const auto view = roi->view;
    index           = roi->resultIndex;
    roi.reset();
    if (viewport)
    {
        viewport->endRoiEdit();
        viewport->restoreViewState(view);
    }
}

void AppController::Impl::commitRoi(const RoiEditTarget &target, const Roi *value)
{
    if (!idle() || !roi || target.nodeSeq != roi->node || target.inputPortId != roi->input
        || target.imageIndex != roi->index)
        return;
    const auto *node = currentNode();
    if (!node || node->id != target.nodeSeq)
        return;
    const auto binding = node->inputs.find(target.inputPortId);
    if (binding == node->inputs.end() || binding->second.mode != BindingMode::Literal)
        return;
    std::vector<std::vector<Roi>> draft(roi->rois.images().begin(), roi->rois.images().end());
    if (target.imageIndex < 0 || static_cast<std::size_t>(target.imageIndex) >= roi->images.images().size())
        return;
    draft.resize(std::max(draft.size(), static_cast<std::size_t>(target.imageIndex) + 1));
    auto &current = draft[target.imageIndex];
    if (target.roiIndex < 0)
    {
        if (!value)
            return;
        current.push_back(*value);
    }
    else if (static_cast<std::size_t>(target.roiIndex) < current.size())
    {
        if (value)
            current[target.roiIndex] = *value;
        else
            current.erase(current.begin() + target.roiIndex);
    }
    else
        return;
    RoiBatch committed(std::move(draft));
    try
    {
        if (editor.setLiteral(flow.toStdString(), target.nodeSeq, target.inputPortId, committed))
        {
            roi->rois = std::move(committed);
            changed();
        }
    }
    catch (const std::exception &error)
    {
        report(workbench::q(error.what()));
    }
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
        for (const auto &output : tool->outputs)
            if (output.type.type == Type::ImageBatch)
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
    return impl_->roi ? static_cast<int>(impl_->roi->images.images().size()) : impl_->count;
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

bool AppController::editRoi(const QString &parameter, bool copyUpstream)
{
    if (!impl_->idle() || !impl_->viewport)
        return false;
    try
    {
        const auto *node        = impl_->currentNode();
        const auto *declaration = impl_->parameter(parameter.toStdString(), false);
        if (!node || !declaration || declaration->type.type != Type::RoiBatch)
            throw ContractError("Selected parameter is not an ROI input");
        if (declaration->imageInput.empty())
            throw ContractError("ROI input has no image_input association");
        const auto  imageValue = impl_->resolvedInput(*node, declaration->imageInput);
        const auto *images     = std::get_if<ImageBatch>(&imageValue);
        if (!images || images->images().empty())
            throw ContractError("No input image cache; run the image source/upstream first");
        auto roiValue = impl_->resolvedInput(*node, parameter.toStdString());
        if (isMissing(roiValue))
            roiValue = RoiBatch{};
        const auto *currentRois = std::get_if<RoiBatch>(&roiValue);
        if (!currentRois)
            throw ContractError("ROI input cache has incompatible type");
        const auto binding = node->inputs.find(parameter.toStdString());
        if (binding == node->inputs.end())
            throw ContractError("Unknown ROI input");
        const auto id = node->id;
        if (binding->second.mode == BindingMode::Upstream)
        {
            if (!copyUpstream)
                throw ContractError("Upstream ROI is read-only; explicitly copy current value to literal first");
            auto literal    = binding->second;
            literal.literal = roiValue;
            literal.mode    = BindingMode::Literal;
            literal.source.reset();
            impl_->editor.setBinding(impl_->flow.toStdString(), id, parameter.toStdString(), std::move(literal));
            impl_->changed();
        }
        impl_->finishRoi();
        impl_->roi = Impl::RoiPreview{id,           parameter.toStdString(),     *images, *currentRois, 0,
                                      impl_->index, impl_->viewport->viewState()};
        impl_->updateDisplay();
        emit resultsChanged();
        return true;
    }
    catch (const std::exception &error)
    {
        impl_->report(workbench::q(error.what()));
        return false;
    }
}

void AppController::endRoiPreview()
{
    impl_->finishRoi();
    impl_->updateResults();
}
} // namespace visionrt
