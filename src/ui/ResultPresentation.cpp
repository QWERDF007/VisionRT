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

QString imageKey(const PortRef &ref)
{
    return QStringLiteral("%1:%2")
        .arg(ref.stage == PortStage::Inputs ? QStringLiteral("inputs") : QStringLiteral("outputs"))
        .arg(ref.id);
}

std::optional<PortRef> parseImageKey(const QString &key)
{
    const auto parts = key.split(QLatin1Char(':'));
    if (parts.size() != 2)
        return std::nullopt;
    bool      ok = false;
    const auto id = parts[1].toUInt(&ok);
    if (!ok || id == 0)
        return std::nullopt;
    if (parts[0] == QStringLiteral("inputs"))
        return PortRef{PortStage::Inputs, id};
    if (parts[0] == QStringLiteral("outputs"))
        return PortRef{PortStage::Outputs, id};
    return std::nullopt;
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
    imageSelection_.reset();
    selectionNode_.reset();
    index_     = 0;
    count_     = 0;
    displayed_.reset();
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
        const auto  nodeId = node ? std::optional<NodeId>{node->id} : std::nullopt;
        if (nodeId != displayed_)
        {
            displayed_ = nodeId;
            outputs_.clear();
            // Results are not readable yet; when they are, this node starts its view again rather
            // than keeping a selection whose value no longer exists.
            imageSelection_.reset();
            selectionNode_.reset();
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
    const auto *node   = host_.selectedNode();
    const auto  nodeId = node ? std::optional<NodeId>{node->id} : std::nullopt;
    displayed_         = nodeId;
    const auto *tool   = host_.effectiveTool();
    if (!node)
    {
        outputs_.clear();
        imageSelection_.reset();
        selectionNode_.reset();
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
        // A node's first view starts on its first offered image. After that the selection only
        // survives while its real port is still offered; a hidden or removed port leaves the
        // viewport empty instead of being replaced by another image on the user's behalf.
        const auto choices = imageChoices();
        if (choices.empty())
        {
            // A node with nothing to show cannot hold a selection; a port that only exists on
            // another node is not carried over to this one.
            imageSelection_.reset();
            selectionNode_.reset();
        }
        else if (!selectionNode_ || *selectionNode_ != node->id)
        {
            // First view of this node: open on its first offered image.
            imageSelection_ = choices.front().ref;
            selectionNode_  = node->id;
        }
        else if (imageSelection_
                 && std::none_of(choices.begin(), choices.end(),
                                 [&](const ImageChoice &choice) { return choice.ref == *imageSelection_; }))
            // The chosen port is no longer offered: clear it, never jump to another image.
            imageSelection_.reset();
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

    // The selector lists real ports only. When the selected port is no longer offered it is dropped,
    // never replaced: the viewport goes empty rather than showing a port the user did not choose.
    const auto choices = imageChoices();
    if (!roi_.active() && imageSelection_
        && std::none_of(choices.begin(), choices.end(),
                        [&](const ImageChoice &choice) { return choice.ref == *imageSelection_; }))
        imageSelection_.reset();

    const auto           *node = host_.selectedNode();
    const auto           *tool = host_.effectiveTool();
    const DataRef<Image> *batch = nullptr;
    ResultSlot::Snapshot  snapshot;
    QString               color;
    PortId                roiImageInput = 0;
    displayInputs_.clear();
    displayOutputs_.clear();
    displayRoiTarget_.reset();
    int selectedIndex = index_;
    if (node)
        snapshot = displaySnapshot(*node);
    const PortDefinition *selectedPort = nullptr;
    if (!roi_.active() && imageSelection_ && tool)
    {
        const auto &ports = imageSelection_->stage == PortStage::Inputs ? tool->inputs : tool->outputs;
        selectedPort      = findPort(ports, imageSelection_->id);
        if (!selectedPort)
            imageSelection_.reset();
        else if (imageSelection_->stage == PortStage::Outputs)
            batch = images(snapshot, selectedPort->id);
        else if (node)
        {
            const auto *value = resolvedInput(*node, selectedPort->id);
            batch             = value ? std::get_if<DataRef<Image>>(value) : nullptr;
        }
        if (batch && batch->get().empty())
            batch = nullptr;
        count_        = batch ? static_cast<int>(batch->get().size()) : 0;
        index_        = std::clamp(index_, 0, std::max(0, count_ - 1));
        selectedIndex = index_;
        if (selectedPort)
            color = text(selectedPort->colorOrder);
    }
    else if (roi_.active())
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
        if (declaration && node)
        {
            roiImageInput = declaration->image ? declaration->image->id : 0;
            if (tool)
                for (const auto &port : tool->inputs)
                    if (port.id != roi_.input() && port.type.type == Type::ROI && port.image
                        && *port.image == PortRef{PortStage::Inputs, roiImageInput})
                    {
                        const auto *value = resolvedInput(*node, port.id);
                        if (!value)
                            continue;
                        auto values = imageRois(*value, selectedIndex, port.editor == "rect");
                        displayInputs_.insert(displayInputs_.end(), values.begin(), values.end());
                    }
        }
    }
    else
    {
        count_ = 0;
        index_ = 0;
    }

    if (!roi_.active() && node && tool && snapshot && imageSelection_)
    {
        // Geometry declared on the displayed image itself.
        for (const auto &port : tool->inputs)
        {
            if (port.type.type != Type::ROI || !port.image || *port.image != *imageSelection_)
                continue;
            const auto value = snapshot->roiInputs.find(port.id);
            if (value != snapshot->roiInputs.end())
            {
                auto values = imageRois(value->second, selectedIndex, port.editor == "rect");
                displayInputs_.insert(displayInputs_.end(), values.begin(), values.end());
            }
        }
        // An output image declares which input image shares its coordinates. Only that declared
        // input contributes overlays; equal pixels or a first-non-empty input never do.
        if (imageSelection_->stage == PortStage::Outputs && selectedPort && selectedPort->sameGeometryAs)
            for (const auto &port : tool->inputs)
                if (port.type.type == Type::ROI && port.image && *port.image == *selectedPort->sameGeometryAs)
                {
                    const auto value = snapshot->roiInputs.find(port.id);
                    if (value != snapshot->roiInputs.end())
                    {
                        auto values = imageRois(value->second, selectedIndex, port.editor == "rect");
                        displayInputs_.insert(displayInputs_.end(), values.begin(), values.end());
                    }
                }
    }

    if (tool && snapshot)
        for (const auto &port : tool->outputs)
        {
            if (port.type.type != Type::ROI || !port.image)
                continue;
            const auto *imagePort = port.image->stage == PortStage::Inputs ? findPort(tool->inputs, port.image->id)
                                                                          : findPort(tool->outputs, port.image->id);
            if (!imagePort)
                continue;
            const bool matches
                = roi_.active()
                      ? (port.image->stage == PortStage::Inputs
                             ? port.image->id == roiImageInput
                             : (imagePort->sameGeometryAs && imagePort->sameGeometryAs->stage == PortStage::Inputs
                                && imagePort->sameGeometryAs->id == roiImageInput))
                      : (imageSelection_ && *port.image == *imageSelection_);
            if (!matches)
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

std::vector<ResultPresentation::ImageChoice> ResultPresentation::imageChoices() const
{
    std::vector<ImageChoice> choices;
    const auto              *tool = host_.effectiveTool();
    const auto              *node = host_.selectedNode();
    if (!tool || !node)
        return choices;

    const auto add = [&](const std::vector<PortDefinition> &ports, PortStage stage, const TypeMap &signatures,
                         bool visibleOnly)
    {
        for (const auto &port : ports)
        {
            if (port.type.type != Type::Image || !signatures.contains(port.id))
                continue;
            if (visibleOnly && !port.imageVisible)
                continue;
            const PortRef ref{stage, port.id};
            if (std::any_of(choices.begin(), choices.end(), [&](const auto &choice) { return choice.ref == ref; }))
                continue;
            choices.push_back({ref, text(port.nameZh)});
        }
    };

    add(tool->outputs, PortStage::Outputs, node->signatures.outputs, true);
    add(tool->inputs, PortStage::Inputs, node->signatures.inputs, true);

    // The ROI session edits one declared input image. It stays selectable as that real port even
    // when the author did not offer it to the general selector; no substitute image is listed.
    if (roi_.active() && roi_.node() == node->id)
        if (const auto *declaration = host_.parameter(roi_.input(), false);
            declaration && declaration->image && declaration->image->stage == PortStage::Inputs)
            add(tool->inputs, PortStage::Inputs, node->signatures.inputs, false);
    return choices;
}

QVariantList ResultPresentation::imageOutputs() const
{
    QVariantList result;
    for (const auto &choice : imageChoices())
        result.push_back(QVariantMap{
            {  "id",                                         imageKey(choice.ref)},
            {  "name",                                               choice.name},
            {"stage", choice.ref.stage == PortStage::Inputs ? "inputs" : "outputs"}
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

QString ResultPresentation::selectedImageOutput() const
{
    // An ROI session always presents its frozen background, which is the real declared input.
    if (roi_.active())
    {
        if (const auto *declaration = host_.parameter(roi_.input(), false); declaration && declaration->image)
            return imageKey(*declaration->image);
        return {};
    }
    return imageSelection_ ? imageKey(*imageSelection_) : QString{};
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

bool ResultPresentation::setImageOutput(const QString &output)
{
    if (roi_.active())
        return false;
    const auto parsed = parseImageKey(output);
    if (!parsed || imageSelection_ == parsed)
        return false;
    const auto choices = imageChoices();
    if (std::none_of(choices.begin(), choices.end(),
                     [&](const ImageChoice &choice) { return choice.ref == *parsed; }))
        return false;
    imageSelection_ = parsed;
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
