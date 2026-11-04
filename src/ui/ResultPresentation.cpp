#include "ui/ResultPresentation.h"

#include <QClipboard>
#include <QGuiApplication>

#include <algorithm>
#include <type_traits>

namespace visionrt {
namespace {
RegionGroup imageRois(const PortValue &value, int index, bool singleRectangle = false)
{
    const auto *regions = std::get_if<DataRef<ROI>>(&value);
    if (singleRectangle)
        index = 0;
    if (!regions || index < 0 || static_cast<std::size_t>(index) >= regions->get().size())
        return {};
    return regions->get()[index];
}

const DataRef<Image> *images(const ResultSlot::Snapshot &snapshot, PortId port)
{
    if (!snapshot)
        return nullptr;
    const auto value = snapshot->outputs.find(port);
    return value == snapshot->outputs.end() ? nullptr : std::get_if<DataRef<Image>>(&value->second);
}

QVariantMap textTarget(QVariantMap context)
{
    context.remove("publication");
    return context;
}

QString text(const std::string &value)
{
    return QString::fromUtf8(value.data(), static_cast<qsizetype>(value.size()));
}

const PortDefinition *findPort(const std::vector<PortDefinition> &ports, PortId id)
{
    for (const auto &value : ports)
        if (value.id == id)
            return &value;
    return nullptr;
}
} // namespace

ResultPresentation::ResultPresentation(PresentationHost &host, OutputModel &outputs, RoiEditSession &roi,
                                       QObject *parent)
    : host_(host)
    , outputs_(outputs)
    , roi_(roi)
    , converter_(parent)
{
    QObject::connect(&converter_, &DisplayConverter::batchConverted, &converter_,
                     [this](const std::vector<DisplayFrame> &frames)
                     {
                         for (const auto &frame : frames)
                         {
                             if (frame.generation != generation_ || frame.requestId != displayRequest_
                                 || frame.batchIndex < 0
                                 || static_cast<std::size_t>(frame.batchIndex) >= displayTargets_.size())
                                 continue;
                             if (auto target = displayTargets_[frame.batchIndex])
                             {
                                 target->setDisplayFrame(frame);
                                 if (target == viewport_)
                                     applyOverlays();
                             }
                         }
                         updateReadback();
                     });
    QObject::connect(&converter_, &DisplayConverter::readersIdle, &converter_,
                     [this]
                     {
                         if (draining_)
                             finishDrain();
                     });
}

ResultPresentation::~ResultPresentation() = default;

const ResultSlot::Snapshot *ResultPresentation::snapshot(NodeId node) const
{
    const auto found = snapshots_.find(node);
    return found == snapshots_.end() ? nullptr : &found->second;
}

ResultSlot::Snapshot ResultPresentation::sharedSnapshot(NodeId node) const
{
    const auto found = snapshots_.find(node);
    return found == snapshots_.end() ? ResultSlot::Snapshot{} : found->second;
}

bool ResultPresentation::active(NodeId node) const
{
    return active_.contains(node);
}

const NodeDiagnostic *ResultPresentation::lifecycle(NodeId node) const
{
    const auto found = lifecycle_.find(node);
    return found == lifecycle_.end() ? nullptr : found->second.get();
}

std::shared_ptr<const NodeDiagnostic> ResultPresentation::lifecycleShared(NodeId node) const
{
    const auto found = lifecycle_.find(node);
    return found == lifecycle_.end() ? nullptr : found->second;
}

const NodeDiagnostic *ResultPresentation::instance(NodeId node) const
{
    const auto found = instances_.find(node);
    return found == instances_.end() ? nullptr : found->second.get();
}

ResultSlot::Snapshot ResultPresentation::displaySnapshot(const NodeDefinition &node) const
{
    const auto *snapshot = this->snapshot(node.id);
    if (!snapshot)
        return {};
    if ((*snapshot)->toolId != node.toolId)
        return {};
    for (const auto &[port, value] : (*snapshot)->outputs)
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
    return *snapshot;
}

bool ResultPresentation::applyNode(NodeId node, bool active, const std::shared_ptr<const NodeDiagnostic> &lifecycle,
                                   const std::shared_ptr<const NodeDiagnostic> &instance, ResultSlot::Snapshot snapshot,
                                   std::uint64_t generation)
{
    bool changed = false;
    if (active != active_.contains(node))
    {
        changed = true;
        if (active)
            active_.insert(node);
        else
            active_.erase(node);
    }
    if (lifecycle)
    {
        const auto previous = lifecycle_.find(node);
        if (previous == lifecycle_.end() || previous->second != lifecycle)
        {
            lifecycle_[node] = lifecycle;
            changed          = true;
        }
    }
    else if (lifecycle_.erase(node) > 0)
        changed = true;
    if (instance)
    {
        const auto previous = instances_.find(node);
        if (previous == instances_.end() || previous->second != instance)
        {
            instances_[node] = instance;
            changed          = true;
        }
    }
    else if (instances_.erase(node) > 0)
        changed = true;
    if (snapshot && snapshot->generation == generation)
    {
        const auto previous = snapshots_.find(node);
        if (previous == snapshots_.end() || previous->second != snapshot)
        {
            snapshots_[node] = std::move(snapshot);
            changed          = true;
        }
    }
    return changed;
}

void ResultPresentation::forgetNode(NodeId node)
{
    snapshots_.erase(node);
    active_.erase(node);
    lifecycle_.erase(node);
    instances_.erase(node);
}

void ResultPresentation::retain(const std::set<NodeId> &existing)
{
    std::erase_if(snapshots_, [&](const auto &item) { return !existing.contains(item.first); });
    std::erase_if(lifecycle_, [&](const auto &item) { return !existing.contains(item.first); });
    std::erase_if(instances_, [&](const auto &item) { return !existing.contains(item.first); });
    std::erase_if(active_, [&](NodeId node) { return !existing.contains(node); });
}

void ResultPresentation::clearDisplay()
{
    converter_.invalidate(generation_);
    if (viewport_)
    {
        ++displayRequest_;
        viewport_->clearImage(displayRequest_);
        viewport_->setOverlays({}, {});
        viewport_->endRoiEdit();
    }
    displayInputs_.clear();
    displayOutputs_.clear();
    displayRoiTarget_.reset();
    displayTargets_.clear();
    outputs_.clear();
    imageOutput_ = 0;
    index_    = 0;
    count_    = 0;
    displayed_ = 0;
}

void ResultPresentation::clear()
{
    readback_.reset();
    snapshots_.clear();
    active_.clear();
    lifecycle_.clear();
    instances_.clear();
    converter_.invalidate(generation_);
}

void ResultPresentation::updateResults()
{
    if (!readsAllowed())
    {
        const auto *node   = host_.selectedNode();
        const auto  nodeId = node ? node->id : 0;
        if (nodeId != displayed_)
        {
            displayed_ = nodeId;
            outputs_.clear();
            imageOutput_ = 0;
            index_ = 0;
            if (viewport_)
            {
                viewport_->clearImage(displayRequest_);
                viewport_->setOverlays({}, {});
                viewport_->endRoiEdit();
            }
            host_.resultsChanged();
        }
        return;
    }
    const auto *node = host_.selectedNode();
    displayed_       = node ? node->id : 0;
    const auto *tool = host_.effectiveTool();
    if (!node)
    {
        outputs_.clear();
        imageOutput_ = 0;
        index_ = 0;
    }
    else
    {
        const auto snapshot = displaySnapshot(*node);
        std::map<PortId, std::size_t> references;
        if (const auto *current = host_.currentFlow())
            for (const auto &relation : portRelations(*current).active)
                if (relation.source.node == node->id)
                    ++references[relation.source.port];
        // An output name may only look actionable when a legal and still free downstream input
        // exists. The answer comes from the same candidate/validate rule the binding menu uses.
        const std::map<PortId, bool> *connectable = nullptr;
        if (const auto *flow = host_.currentFlow())
            connectable = host_.refreshConnectable(*flow, *node);
        outputs_.setResult(tool, snapshot, references, connectable, &node->signatures.outputs,
                           lifecycleShared(node->id));
        std::vector<PortId> imagePorts;
        if (tool)
            for (const auto &port : tool->outputs)
                if (port.type.type == Type::Image && node->signatures.outputs.contains(port.id))
                    imagePorts.push_back(port.id);
        if (std::find(imagePorts.begin(), imagePorts.end(), imageOutput_) == imagePorts.end())
            imageOutput_ = imagePorts.empty() ? 0 : imagePorts.front();
    }
    updateDisplay();
    host_.resultsChanged();
}

void ResultPresentation::applyOverlays()
{
    if (!viewport_)
        return;
    viewport_->setOverlays(displayInputs_, displayOutputs_);
    if (displayRoiTarget_)
        viewport_->beginRoiEdit(*displayRoiTarget_, imageRois(roi_.draft(), roi_.displayIndex(), roi_.singleRectangle()),
                                !host_.runtimeIdle(), roi_.singleRectangle());
    else
        viewport_->endRoiEdit();
}

const PortValue *ResultPresentation::resolvedInput(const NodeDefinition &node, PortId input) const
{
    const auto found = node.inputs.find(input);
    if (found == node.inputs.end())
        return nullptr;
    if (found->second.mode == BindingMode::Literal)
        return &found->second.literal;
    if (!found->second.source)
        return nullptr;
    const auto *snapshot = this->snapshot(found->second.source->node);
    if (!snapshot)
        return nullptr;
    const auto output = (*snapshot)->outputs.find(found->second.source->port);
    return output == (*snapshot)->outputs.end() ? nullptr : &output->second;
}

void ResultPresentation::updateDisplay()
{
    if (!readsAllowed())
        return;
    if (roi_.active())
    {
        const auto *node        = host_.selectedNode();
        const auto *declaration = host_.parameter(roi_.input(), false);
        // An image-free editor freezes a Missing background; a missing port must compare equal to it
        // instead of ending the session that just started.
        const PortValue  missing{Missing{}};
        const auto      *background
            = node && declaration && declaration->image ? resolvedInput(*node, declaration->image->id) : nullptr;
        const PortValue &current    = background ? *background : missing;
        if (!node || node->id != roi_.node() || !declaration
            || !equalValues(roi_.background(), current))
        {
            host_.endRoiEdit();
            if (!readsAllowed())
                return;
        }
    }
    const DataRef<Image> *batch = nullptr;
    ResultSlot::Snapshot  snapshot;
    QString               color;
    PortId                imageInput = 0;
    displayInputs_.clear();
    displayOutputs_.clear();
    displayRoiTarget_.reset();
    int selectedIndex = index_;
    if (const auto *node = host_.selectedNode())
        snapshot = displaySnapshot(*node);
    PortId      fallbackInputPort = 0;
    if (!roi_.active())
    {
        batch = images(snapshot, imageOutput_);
        const bool executionFailed = snapshot && snapshot->status.code != 0;
        if ((!batch || batch->get().empty()) && executionFailed && host_.selectedNode())
        {
            const auto *node = host_.selectedNode();
            if (const auto *tool = host_.effectiveTool())
            {
                const auto *selectedImage = findPort(tool->outputs, imageOutput_);
                if (selectedImage && selectedImage->sameGeometryAs)
                {
                    if (const auto *val = resolvedInput(*node, selectedImage->sameGeometryAs->id))
                    {
                        if (const auto *inputBatch = std::get_if<DataRef<Image>>(val);
                            inputBatch && !inputBatch->get().empty())
                        {
                            batch            = inputBatch;
                            fallbackInputPort = selectedImage->sameGeometryAs->id;
                        }
                    }
                }
                if (!batch || batch->get().empty())
                {
                    for (const auto &port : tool->inputs)
                    {
                        if (port.type.type != Type::Image)
                            continue;
                        if (const auto *val = resolvedInput(*node, port.id))
                        {
                            if (const auto *inputBatch = std::get_if<DataRef<Image>>(val);
                                inputBatch && !inputBatch->get().empty())
                            {
                                batch            = inputBatch;
                                fallbackInputPort = port.id;
                                break;
                            }
                        }
                    }
                }
            }
        }
        count_        = batch ? static_cast<int>(batch->get().size()) : 0;
        index_        = std::clamp(index_, 0, std::max(0, count_ - 1));
        selectedIndex = index_;
    }
    if (roi_.active())
    {
        batch         = std::get_if<DataRef<Image>>(&roi_.background());
        selectedIndex = roi_.displayIndex();
        displayRoiTarget_ = RoiEditTarget{roi_.node(),
                                          roi_.input(),
                                          selectedIndex,
                                          roi_.singleRectangle()
                                                  && !std::get<DataRef<ROI>>(roi_.draft()).get().empty()
                                              ? 0
                                              : -1,
                                          roi_.id()};
        const auto *declaration = host_.parameter(roi_.input(), false);
        const auto *node        = host_.selectedNode();
        if (declaration && node)
        {
            imageInput = declaration->image ? declaration->image->id : 0;
            if (const auto *tool = host_.effectiveTool())
                for (const auto &port : tool->inputs)
                    if (port.id != roi_.input() && port.type.type == Type::ROI && port.image
                        && port.image->id == imageInput)
                    {
                        const auto *value = resolvedInput(*node, port.id);
                        if (!value)
                            continue;
                        auto values = imageRois(*value, selectedIndex, port.editor == "rect");
                        displayInputs_.insert(displayInputs_.end(), values.begin(), values.end());
                    }
        }
    }
    else if (const auto *node = host_.selectedNode())
    {
        if (const auto *tool = host_.effectiveTool())
        {
            const auto *image = findPort(tool->outputs, imageOutput_);
            if (image)
                color = text(image->colorOrder);

            if (fallbackInputPort != 0)
            {
                if (color.isEmpty())
                {
                    if (const auto *inPort = findPort(tool->inputs, fallbackInputPort))
                        color = text(inPort->colorOrder);
                }
                for (const auto &port : tool->inputs)
                {
                    if (port.type.type != Type::ROI || !port.image || port.image->id != fallbackInputPort)
                        continue;
                    if (snapshot)
                    {
                        const auto value = snapshot->roiInputs.find(port.id);
                        if (value != snapshot->roiInputs.end())
                        {
                            auto values = imageRois(value->second, selectedIndex, port.editor == "rect");
                            displayInputs_.insert(displayInputs_.end(), values.begin(), values.end());
                            continue;
                        }
                    }
                    if (const auto *val = resolvedInput(*node, port.id))
                    {
                        auto values = imageRois(*val, selectedIndex, port.editor == "rect");
                        displayInputs_.insert(displayInputs_.end(), values.begin(), values.end());
                    }
                }
            }
            else if (image && image->sameGeometryAs && snapshot)
            {
                for (const auto &port : tool->inputs)
                    if (port.type.type == Type::ROI && port.image
                        && port.image->id == image->sameGeometryAs->id)
                    {
                        const auto value = snapshot->roiInputs.find(port.id);
                        if (value != snapshot->roiInputs.end())
                        {
                            auto values = imageRois(value->second, selectedIndex, port.editor == "rect");
                            displayInputs_.insert(displayInputs_.end(), values.begin(), values.end());
                        }
                    }
            }
        }
    }
    if (const auto *tool = host_.effectiveTool(); tool && snapshot)
        for (const auto &port : tool->outputs)
        {
            if (port.type.type != Type::ROI)
                continue;
            if (!port.image)
                continue;
            const auto &imagePorts = port.image->stage == PortStage::Outputs ? tool->outputs : tool->inputs;
            const auto *image      = findPort(imagePorts, port.image->id);
            if (!image
                || (roi_.active() ? (!image->sameGeometryAs || image->sameGeometryAs->id != imageInput)
                                  : image->id != imageOutput_))
                continue;
            const auto value = snapshot->outputs.find(port.id);
            if (value != snapshot->outputs.end())
            {
                auto values = imageRois(value->second, selectedIndex, port.editor == "rect");
                displayOutputs_.insert(displayOutputs_.end(), values.begin(), values.end());
            }
        }
    Image                                requestedImages;
    std::vector<QPointer<ImageViewport>> targets;
    std::vector<QPointer<ImageViewport>> empty;
    auto                                 append = [&](QPointer<ImageViewport> target, int itemIndex)
    {
        if (!target)
            return;
        target->setGeneration(generation_);
        if (!batch || itemIndex < 0 || static_cast<std::size_t>(itemIndex) >= batch->get().size())
        {
            empty.push_back(target);
            return;
        }
        requestedImages.push_back(batch->get()[itemIndex]);
        targets.push_back(target);
    };
    append(viewport_, selectedIndex);
    std::erase_if(thumbnails_, [](const auto &target) { return target.isNull(); });
    for (const auto &target : thumbnails_)
        if (target->isVisible())
            append(target, target->property("thumbnailIndex").toInt());
    ++displayRequest_;
    displayTargets_ = std::move(targets);
    for (const auto &target : empty)
    {
        target->clearImage(displayRequest_);
        target->setOverlays({}, {});
        target->endRoiEdit();
    }
    if (requestedImages.empty())
    {
        converter_.invalidate(generation_);
        applyOverlays();
        return;
    }
    converter_.requestBatch(DataRef<Image>(std::move(requestedImages), {batch->owner()}), std::move(color),
                            displayRequest_, generation_);
}

void ResultPresentation::adoptViewport(ImageViewport *target)
{
    viewport_ = target;
    updateDisplay();
}

void ResultPresentation::adoptThumbnail(ImageViewport *target)
{
    if (std::find(thumbnails_.begin(), thumbnails_.end(), target) == thumbnails_.end())
        thumbnails_.push_back(target);
    updateDisplay();
}

ImageViewport *ResultPresentation::viewport() const
{
    return viewport_;
}

QVariantList ResultPresentation::imageOutputs() const
{
    QVariantList result;
    if (roi_.active())
    {
        result.push_back(QVariantMap{
            {  "id",                                                                   0},
            {"name", QStringLiteral("输入预览 · ") + QString::fromStdString(portLabel(roi_.input()))}
        });
        return result;
    }
    if (const auto *tool = host_.effectiveTool())
    {
        const auto *node = host_.selectedNode();
        for (const auto &output : tool->outputs)
            if (node && node->signatures.outputs.contains(output.id)
                && node->signatures.outputs.at(output.id).type == Type::Image)
                result.push_back(QVariantMap{
                    {  "id",   output.id},
                    {"name", text(output.nameZh)}
                });
    }
    else if (const auto *node = host_.selectedNode())
        for (const auto &[id, type] : node->signatures.outputs)
            if (type.type == Type::Image)
                result.push_back(QVariantMap{
                    {  "id", id},
                    {"name", text(portLabel(id))}
                });
    return result;
}

int ResultPresentation::imageIndex() const
{
    return roi_.active() ? roi_.displayIndex() : index_;
}

int ResultPresentation::imageCount() const
{
    if (!roi_.active())
        return count_;
    const auto *images = std::get_if<DataRef<Image>>(&roi_.background());
    return images ? static_cast<int>(images->get().size()) : 0;
}

int ResultPresentation::selectedImageOutput() const
{
    // 0 is the reserved status-code number, so it can never be a business image output: it means
    // "the ROI editor's background image".
    return roi_.active() ? 0 : static_cast<int>(imageOutput_);
}

bool ResultPresentation::setImageIndex(int index)
{
    const int clamped = std::clamp(index, 0, std::max(0, imageCount() - 1));
    if (clamped == imageIndex())
        return false;
    if (viewport_)
        viewport_->cancelDraft();
    if (roi_.active())
        roi_.setDisplayIndex(clamped);
    else
        index_ = clamped;
    updateDisplay();
    if (viewport_)
        viewport_->forceActiveFocus();
    return true;
}

void ResultPresentation::restoreImageIndex(int index)
{
    if (roi_.active())
        roi_.setDisplayIndex(index);
    else
        index_ = index;
}

bool ResultPresentation::setImageOutput(int output)
{
    if (roi_.active() || output <= 0 || imageOutput_ == static_cast<PortId>(output))
        return false;
    const auto choices = imageOutputs();
    if (std::none_of(choices.begin(), choices.end(),
                     [&](const auto &choice) { return choice.toMap().value("id").toInt() == output; }))
        return false;
    imageOutput_ = static_cast<PortId>(output);
    updateDisplay();
    if (viewport_)
        viewport_->forceActiveFocus();
    return true;
}

bool ResultPresentation::readsAllowed() const
{
    if (host_.closing())
        return false;
    return host_.runtimeIdle() || readback_.has_value();
}

bool ResultPresentation::setReadback(const RuntimeReadback *readback)
{
    if (!readback)
    {
        const bool changed = readback_.has_value();
        readback_.reset();
        return changed;
    }
    const bool changed = readback_ != *readback;
    readback_          = *readback;
    return changed;
}

void ResultPresentation::updateReadback()
{
    if (draining_ || host_.closing())
        return;
    if (textMailbox_)
    {
        bool done = false;
        {
            std::lock_guard lock(textMailbox_->mutex);
            done = textMailbox_->done;
        }
        if (done)
            consumeTextMailbox(textMailbox_);
    }
    if (!readsAllowed())
        return;
    // Text reads need the publication to stay valid, so they are served before the readback barrier
    // is acknowledged.
    while (!textTask_.valid() && !readRequests_.empty())
    {
        auto intent = std::move(readRequests_.front());
        readRequests_.pop_front();
        // The queued context dropped its publication; rebuild the live one and serve only when the
        // identity still matches, exactly as the direct entry would.
        const auto current = host_.portContext(intent.context.value("port").toInt());
        if (intent.context == textTarget(current))
            requestText(current);
    }
    const bool textQueued = !readRequests_.empty();
    if (readback_ && runtime_ && !textTask_.valid() && !textQueued && !converter_.isBusy()
        && !converter_.hasPending())
    {
        if (runtime_->acknowledgeReadback(*readback_))
            readback_.reset();
    }
}

void ResultPresentation::requestText(const QVariantMap &context)
{
    if (host_.closing() || draining_)
        return;
    if (!context.value("output").toBool() || !host_.contextCurrent(context))
        return;
    if (!readsAllowed())
    {
        const auto target = textTarget(context);
        const bool alreadyQueued
            = std::any_of(readRequests_.begin(), readRequests_.end(),
                          [&](const auto &item) { return item.context == target; });
        if (!alreadyQueued)
            readRequests_.push_back(TextRequest{target});
        return;
    }
    if (textTask_.valid())
    {
        host_.reportDiagnostic(QStringLiteral("正在生成完整输出，请稍候"));
        return;
    }
    auto captured = outputs_.capture(static_cast<PortId>(context.value("port").toUInt()));
    if (!captured)
        return;
    // Copy always runs; a large value is formatted on the bounded worker rather than gated behind a
    // second confirmation dialog, and it is never truncated into something that looks complete.
    const auto generation = generation_;
    auto       mailbox    = std::make_shared<TextMailbox>();
    mailbox->generation   = generation;
    textMailbox_          = mailbox;
    ++textPending_;
    textTask_ = std::async(
        std::launch::async,
        [captured = std::move(*captured), generation, context, mailbox, this]
        {
            try
            {
                auto text = OutputModel::fullText(captured);
                {
                    std::lock_guard lock(mailbox->mutex);
                    mailbox->value = TextResult{std::move(text), generation, context};
                    mailbox->done  = true;
                }
            }
            catch (...)
            {
                std::lock_guard lock(mailbox->mutex);
                mailbox->error = std::current_exception();
                mailbox->done  = true;
            }
            --textPending_;
            // The future is released on the GUI thread; posting through the converter keeps this
            // module free of a QObject identity of its own.
            QMetaObject::invokeMethod(&converter_, [this, mailbox] { consumeTextMailbox(mailbox); },
                                      Qt::QueuedConnection);
        });
}

void ResultPresentation::consumeTextMailbox(std::shared_ptr<TextMailbox> mailbox)
{
    if (!mailbox)
        return;
    const bool current = mailbox == textMailbox_;
    if (current)
        textMailbox_.reset();
    // Never block the GUI thread. The async body has already decremented the pending counter before
    // it queues this call, so releasing the future here cannot wait on formatting work.
    if (textTask_.valid() && textPending_.load() == 0)
        textTask_ = {};
    if (!current || mailbox->generation != generation_)
    {
        if (draining_)
            finishDrain();
        return;
    }

    std::exception_ptr        error;
    std::optional<TextResult> result;
    {
        std::lock_guard lock(mailbox->mutex);
        if (!mailbox->done)
            return;
        error  = mailbox->error;
        result = std::move(mailbox->value);
    }

    if (error)
    {
        try
        {
            std::rethrow_exception(error);
        }
        catch (const std::exception &e)
        {
            host_.reportFailure(e, QStringLiteral("输出文本格式化失败"));
        }
        if (!draining_)
            updateReadback();
        else
            finishDrain();
        return;
    }

    if (result && host_.contextCurrent(result->context))
    {
        // The only product intent for the full output is "copy this value"; the same text is also
        // delivered so the caller can show that the read finished.
        QGuiApplication::clipboard()->setText(result->text);
        host_.deliverText(result->text);
    }
    if (!draining_)
        updateReadback();
    else
        finishDrain();
}

bool ResultPresentation::drainReady() const
{
    return !converter_.isBusy() && textPending_.load() == 0 && (!externalBusy_ || !externalBusy_());
}

void ResultPresentation::beginDrain(std::function<void()> continuation)
{
    if (draining_)
    {
        // Coalesce: the newest continuation wins, exactly one body runs after the drain.
        drainContinuation_ = std::move(continuation);
        return;
    }
    // Invalidate before waiting so a completed-but-not-yet-delivered conversion can not touch the
    // data the caller is about to mutate.
    converter_.invalidate(generation_);
    readRequests_.clear();
    if (drainReady())
    {
        if (continuation)
            continuation();
        return;
    }
    draining_          = true;
    drainContinuation_ = std::move(continuation);
    host_.runtimeChanged();
}

void ResultPresentation::finishDrain()
{
    if (!draining_ || !drainReady())
        return;
    draining_ = false;
    auto continuation = std::move(drainContinuation_);
    drainContinuation_ = {};
    if (continuation)
        continuation();
    // The public interaction predicate includes draining, so leaving the drain is a state change
    // the workspace must observe even when the runtime state itself stayed Idle.
    host_.runtimeChanged();
}

void ResultPresentation::drainReaders()
{
    struct DrainGuard
    {
        bool &flag;
        bool  previous;
        DrainGuard(bool &f) : flag(f), previous(f) { flag = true; }
        ~DrainGuard() { flag = previous; }
    } guard(draining_);

    converter_.waitForReads();
    if (textTask_.valid())
    {
        textTask_.wait();
        textTask_ = {};
    }
    if (textMailbox_)
    {
        bool done = false;
        {
            std::lock_guard lock(textMailbox_->mutex);
            done = textMailbox_->done;
        }
        if (done)
            consumeTextMailbox(textMailbox_);
        else
            textMailbox_.reset();
    }
    readRequests_.clear();
}

} // namespace visionrt
