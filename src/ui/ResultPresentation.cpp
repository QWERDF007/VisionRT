#include "ResultPresentation.h"
#include "core/ProjectEditor.h"

#include <QClipboard>
#include <QGuiApplication>
#include <algorithm>
#include <type_traits>

namespace visionrt {
namespace {
RegionGroup imageRois(const PortValue &value, int index, bool singleRectangle = false)
{
    const auto *regions = shapeRegions(value);
    if (singleRectangle)
        index = 0;
    if (!regions || index < 0 || static_cast<std::size_t>(index) >= (*regions).size())
        return {};
    return (*regions)[index];
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
        .arg(QString::fromLatin1(stageName(ref.stage)))
        .arg(ref.id);
}

std::optional<PortRef> parseImageKey(const QString &key)
{
    const auto parts = key.split(QLatin1Char(':'));
    if (parts.size() != 2)
        return std::nullopt;
    bool       ok = false;
    const auto id = parts[1].toUInt(&ok);
    if (!ok)
        return std::nullopt;
    for (const auto stage : {PortStage::Init, PortStage::Train, PortStage::Inputs, PortStage::Outputs})
        if (parts[0] == QString::fromLatin1(stageName(stage))) return PortRef{stage, id};
    return std::nullopt;
}
} // namespace

ResultPresentation::ResultPresentation(PresentationHost &host, OutputModel &outputs, RoiEditSession &roi,
                                       QObject *parent, std::shared_ptr<ThumbnailStore> thumbnailStore)
    : host_(host)
    , outputs_(outputs)
    , roi_(roi)
    , converter_(parent)
    , thumbnailStore_(std::move(thumbnailStore))
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
    QObject::connect(&converter_, &DisplayConverter::thumbnailsConverted, &converter_,
                     [this](quint64 generation, quint64 requestId, const std::vector<QImage> &thumbnails)
                     {
                         if (!thumbnailStore_ || generation != generation_ || requestId != displayRequest_)
                             return;
                         std::vector<std::pair<QString, QImage>> entries;
                         entries.reserve(thumbnails.size());
                         for (std::size_t i = 0; i < thumbnails.size(); ++i)
                             entries.emplace_back(QString::number(i), thumbnails[i]);
                         thumbnailVersion_
                             = thumbnailStore_->replaceNamespace(QStringLiteral("results/"), std::move(entries));
                         if (pendingThumbnailRequest_ == requestId)
                             publishedThumbnailBatch_ = pendingThumbnailBatch_;
                         host_.thumbnailVersionChanged();
                     });
    QObject::connect(&converter_, &DisplayConverter::readersIdle, &converter_,
                     [this]
                     {
                         if (draining_)
                             finishDrain();
                     });
}

ResultPresentation::~ResultPresentation() = default;

void ResultPresentation::setSuspended(bool value)
{
    if (suspended_ == value)
        return;
    suspended_ = value;
    if (suspended_)
    {
        ++displayRequest_;
        converter_.invalidate(generation_);
        displayTargets_.clear();
        if (viewport_)
        {
            viewport_->setOverlays({}, {});
            viewport_->setGeometryOverlays({});
            viewport_->endRoiEdit();
        }
    }
}

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

std::optional<NodeDiagnostic> ResultPresentation::status(const NodeDefinition &node) const
{
    const auto currentResult = displaySnapshot(node);
    if (currentResult && currentResult->invocation == ProducedBy::Train)
        return NodeDiagnostic{node.id, node.toolId, currentResult->phase, currentResult->status};
    if (const auto *diagnostic = instance(node.id))
        return *diagnostic;
    const auto snapshot = displaySnapshot(node);
    if (snapshot && !snapshot->status)
        return NodeDiagnostic{node.id, node.toolId, snapshot->phase, snapshot->status};
    if (const auto *diagnostic = lifecycle(node.id))
        return *diagnostic;
    if (snapshot)
        return NodeDiagnostic{node.id, node.toolId, snapshot->phase, snapshot->status};
    return {};
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
    const auto current = currentResults_.find(node.id);
    const auto *snapshot = current == currentResults_.end() ? this->snapshot(node.id) : &current->second;
    if (!snapshot)
        return {};
    if ((*snapshot)->toolId != node.toolId)
        return {};
    for (const auto &[port, value] : (*snapshot)->outputs)
    {
        std::optional<TypeSpec> expected;
        if (isReservedPort(PortStage::Outputs, port))
            expected = hostPort(PortStage::Outputs, port).type;
        else if (const auto declaration = node.signatures.outputs.find(port);
                 declaration != node.signatures.outputs.end())
            expected = declaration->second;
        if (!expected)
            return {};
        if (isMissing(value))
            continue;
        const auto actual = typeOf(value);
        if (!actual || !compatible(*actual, *expected))
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
            currentResults_[node] = snapshot;
            snapshots_[node] = std::move(snapshot);
            changed          = true;
        }
    }
    return changed;
}

bool ResultPresentation::setActive(NodeId node, bool active)
{
    if (active == active_.contains(node))
        return false;
    if (active)
        active_.insert(node);
    else
        active_.erase(node);
    return true;
}

void ResultPresentation::present(NodeId node, ResultSlot::Snapshot snapshot)
{
    currentResults_.insert_or_assign(node, std::move(snapshot));
}

void ResultPresentation::forgetNode(NodeId node)
{
    snapshots_.erase(node);
    currentResults_.erase(node);
    active_.erase(node);
    lifecycle_.erase(node);
    instances_.erase(node);
}

void ResultPresentation::retain(const std::set<NodeId> &existing)
{
    std::erase_if(currentResults_, [&](const auto &item) { return !existing.contains(item.first); });
    std::erase_if(snapshots_, [&](const auto &item) { return !existing.contains(item.first); });
    std::erase_if(lifecycle_, [&](const auto &item) { return !existing.contains(item.first); });
    std::erase_if(instances_, [&](const auto &item) { return !existing.contains(item.first); });
    std::erase_if(active_, [&](NodeId node) { return !existing.contains(node); });
}

void ResultPresentation::clearThumbnails()
{
    publishedThumbnailBatch_.reset();
    pendingThumbnailBatch_.reset();
    pendingThumbnailRequest_ = 0;
    if (thumbnailStore_)
        thumbnailStore_->clearNamespace(QStringLiteral("results/"));
    if (thumbnailVersion_ != 0)
    {
        thumbnailVersion_ = 0;
        host_.thumbnailVersionChanged();
    }
}

void ResultPresentation::clearDisplay()
{
    converter_.invalidate(generation_);
    clearThumbnails();
    if (viewport_)
    {
        ++displayRequest_;
        viewport_->clearImage(displayRequest_);
        viewport_->setOverlays({}, {});
        viewport_->endRoiEdit();
    }
    displayInputTargets_.clear();
    displayInputs_.clear();
    displayOutputs_.clear();
    displayGeometry_.clear();
    displayRoiTarget_.reset();
    displayTargets_.clear();
    outputs_.clear();
    imageSelection_.reset();
    selectionNode_.reset();
    index_ = 0;
    count_ = 0;
    displayed_.reset();
}

void ResultPresentation::clear()
{
    readback_.reset();
    snapshots_.clear();
    currentResults_.clear();
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
        const auto                                    snapshot = displaySnapshot(*node);
        std::map<PortId, std::vector<OutputRelation>> relations;
        if (const auto *current = host_.currentFlow())
        {
            struct Entry
            {
                const NodeDefinition *target = nullptr;
                PortStage             stage  = PortStage::Inputs;
                PortId                port   = 0;
            };
            std::map<PortId, std::vector<Entry>> grouped;
            for (const auto &relation : portRelations(*current).active)
            {
                if (relation.source.node != node->id)
                    continue;
                const auto found = std::ranges::find(current->nodes, relation.target.node, &NodeDefinition::id);
                if (found != current->nodes.end())
                    grouped[relation.source.port].push_back({&*found, relation.target.stage, relation.target.port});
            }
            for (auto &[sourcePort, entries] : grouped)
            {
                std::sort(entries.begin(), entries.end(),
                          [](const Entry &a, const Entry &b)
                          {
                              if (a.target->position.y != b.target->position.y)
                                  return a.target->position.y < b.target->position.y;
                              if (a.target->position.x != b.target->position.x)
                                  return a.target->position.x < b.target->position.x;
                              if (a.target->id != b.target->id)
                                  return a.target->id < b.target->id;
                              if (a.stage != b.stage)
                                  return static_cast<int>(a.stage) < static_cast<int>(b.stage);
                              return a.port < b.port;
                          });
                auto &list = relations[sourcePort];
                list.reserve(entries.size());
                for (const auto &entry : entries)
                    list.push_back({QString::number(entry.target->id), entry.port,
                                    text(entry.target->title()) + QLatin1Char('-')
                                        + host_.inputPortName(*entry.target, entry.stage, entry.port),
                                    entry.stage});
            }
        }
        // An output name may only look actionable when a legal and still free downstream input
        // exists. The answer comes from the same candidate/validate rule the binding menu uses.
        const std::map<PortId, bool> *connectable = nullptr;
        if (const auto *flow = host_.currentFlow())
            connectable = host_.refreshConnectable(*flow, *node);
        outputs_.setResult(tool, snapshot, relations, connectable, &node->signatures.outputs,
                           lifecycleShared(node->id), status(*node));
        // A node opens on a real image rather than an empty canvas; an explicit choice survives
        // while its port is still offered.
        if (selectionNode_ != std::optional<NodeId>{node->id})
        {
            selectionNode_ = node->id;
            imageSelection_.reset();
        }
        ensureImageSelection(*node, snapshot);
    }
    updateDisplay();
    host_.resultsChanged();
}

void ResultPresentation::applyOverlays()
{
    if (!viewport_ || suspended_)
        return;
    viewport_->setOverlays(displayInputs_, displayOutputs_, displayInputTargets_);
    viewport_->setGeometryOverlays(displayGeometry_);
    if (displayRoiTarget_)
        viewport_->beginRoiEdit(*displayRoiTarget_,
                                imageRois(roi_.draft(), roi_.displayIndex(), roi_.singleRectangle()),
                                !host_.runtimeIdle(), roi_.singleRectangle());
    else
        viewport_->endRoiEdit();
}

const PortValue *ResultPresentation::resolvedBinding(const NodeDefinition &node, PortRef ref) const
{
    if (ref.stage == PortStage::Outputs)
        return nullptr;
    const auto &bindings = node.stageBindings(ref.stage);
    const auto  found    = bindings.find(ref.id);
    if (found == bindings.end())
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

const PortValue *ResultPresentation::resolvedInput(const NodeDefinition &node, PortId input) const
{
    return resolvedBinding(node, {PortStage::Inputs, input});
}

PortValue ResultPresentation::resolvedValue(const NodeDefinition &node, PortRef ref) const
{
    if (ref.stage == PortStage::Outputs)
    {
        const auto result = displaySnapshot(node);
        if (result)
            if (const auto found = result->outputs.find(ref.id); found != result->outputs.end())
                return found->second;
        return Missing{};
    }
    const auto *value = resolvedBinding(node, ref);
    return value ? *value : PortValue{Missing{}};
}

const PortDefinition *ResultPresentation::roiDeclaration() const
{
    const auto *tool = host_.effectiveTool();
    if (!tool) return nullptr;
    const auto stage = roi_.parameterStage();
    return findPort(stage == PortStage::Init ? tool->initParams : stage == PortStage::Train ? tool->trainParams : tool->inputs, roi_.input());
}

void ResultPresentation::updateDisplay()
{
    if (suspended_ || !readsAllowed())
        return;
    if (roi_.active())
    {
        const auto     *node        = host_.selectedNode();
        const auto     *declaration = roiDeclaration();
        // An image-free editor freezes a Missing background; a missing port must compare equal to it
        // instead of ending the session that just started.
        const PortValue missing{Missing{}};
        const auto current = node && declaration && declaration->image ? resolvedValue(*node, *declaration->image) : missing;
        if (!node || node->id != roi_.node() || !declaration
            || (roi_.parameterStage() != PortStage::Train && !equalValues(roi_.background(), current)))
        {
            host_.endRoiEdit();
            if (!readsAllowed())
                return;
        }
    }

    const auto           *node  = host_.selectedNode();
    const auto           *tool  = host_.effectiveTool();
    const DataRef<Image> *batch = nullptr;
    PortValue selectedValue = Missing{};
    ResultSlot::Snapshot  snapshot;
    ColorOrder            color         = ColorOrder::Automatic;
    PortId                roiImageInput = 0;
    displayInputTargets_.clear();
    displayInputs_.clear();
    displayOutputs_.clear();
    displayGeometry_.clear();
    displayRoiTarget_.reset();
    int selectedIndex = index_;
    if (node)
        snapshot = displaySnapshot(*node);
    // The selector lists real ports only, and the panel stays on one of them: a withdrawn port is
    // replaced by the first offered image that carries data instead of leaving the viewport empty.
    if (node)
        ensureImageSelection(*node, snapshot);
    const PortDefinition *selectedPort = nullptr;
    if (!roi_.active() && imageSelection_ && tool)
    {
        const auto &ports = imageSelection_->stage == PortStage::Inputs ? tool->inputs : imageSelection_->stage == PortStage::Outputs ? tool->outputs
                          : imageSelection_->stage == PortStage::Init ? tool->initParams : tool->trainParams;
        selectedPort      = findPort(ports, imageSelection_->id);
        if (!selectedPort)
            imageSelection_.reset();
        else if (imageSelection_->stage == PortStage::Outputs)
            batch = images(snapshot, selectedPort->id);
        else if (node)
        {
            selectedValue = resolvedValue(*node, *imageSelection_);
            batch = std::get_if<DataRef<Image>>(&selectedValue);
        }
        if (batch && batch->get().empty())
            batch = nullptr;
        count_        = batch ? static_cast<int>(batch->get().size()) : 0;
        index_        = std::clamp(index_, 0, std::max(0, count_ - 1));
        selectedIndex = index_;
        if (selectedPort)
            color = selectedPort->colorOrder;
    }
    else if (roi_.active())
    {
        batch             = std::get_if<DataRef<Image>>(&roi_.background());
        selectedIndex     = roi_.displayIndex();
        displayRoiTarget_ = roi_.target(roi_.singleRectangle() && !shapeRegions(roi_.draft())->empty() ? 0 : -1);
        const auto *declaration = roiDeclaration();
        if (declaration && node)
        {
            roiImageInput = declaration->image ? declaration->image->id : 0;
            if (tool && roi_.parameterStage() == PortStage::Inputs)
                for (const auto &port : tool->inputs)
                    if (port.id != roi_.input() && isShapeType(port.type.type) && port.image
                        && *port.image == PortRef{PortStage::Inputs, roiImageInput})
                    {
                        const auto *value = resolvedInput(*node, port.id);
                        if (!value)
                            continue;
                        auto values = imageRois(*value, selectedIndex, port.editor == EditorKind::Rect);
                        for (std::size_t i = 0; i < values.size(); ++i)
                            displayInputTargets_.push_back({node->id, port.id, selectedIndex, static_cast<int>(i)});
                        displayInputs_.insert(displayInputs_.end(), values.begin(), values.end());
                    }
        }
    }
    else
    {
        count_ = 0;
        index_ = 0;
    }

    if (!roi_.active() && node && tool && imageSelection_)
    {
        // Geometry declared on the displayed image itself.
        for (const auto &port : tool->inputs)
        {
            if (!isShapeType(port.type.type) || !port.image || *port.image != *imageSelection_)
                continue;
            const auto *value = resolvedInput(*node, port.id);
            if (value)
            {
                auto values = imageRois(*value, selectedIndex, port.editor == EditorKind::Rect);
                for (std::size_t i = 0; i < values.size(); ++i)
                    displayInputTargets_.push_back({node->id, port.id, selectedIndex, static_cast<int>(i)});
                displayInputs_.insert(displayInputs_.end(), values.begin(), values.end());
            }
        }
        // An output image declares which input image shares its coordinates. Only that declared
        // input contributes overlays; equal pixels or a first-non-empty input never do.
        if (snapshot && imageSelection_->stage == PortStage::Outputs && selectedPort && selectedPort->sameGeometryAs)
            for (const auto &port : tool->inputs)
                if (isShapeType(port.type.type) && port.image && *port.image == *selectedPort->sameGeometryAs)
                {
                    const auto value = snapshot->geometryInputs.find(port.id);
                    if (value != snapshot->geometryInputs.end())
                    {
                        auto values = imageRois(value->second, selectedIndex, port.editor == EditorKind::Rect);
                        for (std::size_t i = 0; i < values.size(); ++i)
                            displayInputTargets_.push_back({node->id, port.id, selectedIndex, static_cast<int>(i)});
                        displayInputs_.insert(displayInputs_.end(), values.begin(), values.end());
                    }
                }
    }

    if (tool && snapshot)
        for (const auto &port : tool->outputs)
        {
            if (!isShapeType(port.type.type) || !port.image)
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
                auto values = imageRois(value->second, selectedIndex, port.editor == EditorKind::Rect);
                displayOutputs_.insert(displayOutputs_.end(), values.begin(), values.end());
            }
        }

    if (node && tool && batch && imageSelection_)
    {
        for (const auto &[stage, ports] : {std::pair{PortStage::Init, &tool->initParams}, {PortStage::Train, &tool->trainParams},
                                          {PortStage::Inputs, &tool->inputs}, {PortStage::Outputs, &tool->outputs}})
            for (const auto &port : *ports)
            {
                if ((port.type.type != Type::Direction && port.type.type != Type::CoordinateFrame2D) || !port.image) continue;
                const bool sameImage = *port.image == *imageSelection_
                    || (selectedPort && selectedPort->sameGeometryAs && *port.image == *selectedPort->sameGeometryAs);
                if (!sameImage) continue;
                auto value = resolvedValue(*node, {stage, port.id});
                if (stage == PortStage::Inputs && snapshot && selectedPort && selectedPort->sameGeometryAs)
                {
                    const auto found = snapshot->geometryInputs.find(port.id);
                    value = found == snapshot->geometryInputs.end() ? PortValue{Missing{}} : found->second;
                }
                Affine2D alignment;
                if (port.alignment)
                {
                    const auto raw = resolvedValue(*node, *port.alignment);
                    const auto *array = std::get_if<Array>(&raw);
                    if (!array) continue;
                    try { alignment = alignmentsFromArray(*array, batch->get().size()).at(selectedIndex); }
                    catch (const ContractError &) { continue; }
                }
                if (auto overlay = GeometryOverlay::fromValue(value, selectedIndex, stage != PortStage::Outputs, alignment))
                    displayGeometry_.push_back(std::move(*overlay));
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
            // A run in flight has not published yet, and an empty viewport would throw away the
            // frame the user is looking at. Nothing is invented: the previous picture stays until
            // the run answers, and an idle node with no data still shows nothing.
            if (!host_.runtimeIdle() && target->hasImage())
                return;
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
        target->setGeometryOverlays({});
        target->endRoiEdit();
    }
    std::optional<DataRef<Image>> thumbnailBatch;
    if (batch && !batch->get().empty() && thumbnailStore_ && thumbnailsEnabled_)
    {
        ThumbnailBatchKey key;
        key.generation = generation_;
        key.color      = color;
        key.frames.reserve(batch->get().size());
        for (const auto &mat : batch->get())
            key.frames.push_back({mat.data, mat.rows, mat.cols, mat.type(), mat.step[0]});
        if (!publishedThumbnailBatch_ || *publishedThumbnailBatch_ != key)
        {
            pendingThumbnailBatch_   = std::move(key);
            pendingThumbnailRequest_ = displayRequest_;
            thumbnailBatch           = DataRef<Image>(batch->get(), {batch->owner()});
        }
    }
    if (requestedImages.empty() && !thumbnailBatch)
    {
        converter_.invalidate(generation_);
        if (host_.runtimeIdle() && (!batch || batch->get().empty()))
            clearThumbnails();
        applyOverlays();
        return;
    }
    converter_.requestBatch(DataRef<Image>(std::move(requestedImages), {batch->owner()}), std::move(color),
                            displayRequest_, generation_, std::move(thumbnailBatch));
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

void ResultPresentation::setThumbnailsEnabled(bool enabled)
{
    if (thumbnailsEnabled_ == enabled)
        return;
    thumbnailsEnabled_ = enabled;
    if (thumbnailsEnabled_)
        updateDisplay();
    else
        clearThumbnails();
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

    const auto add
        = [&](const std::vector<PortDefinition> &ports, PortStage stage, const TypeMap &signatures, bool visibleOnly)
    {
        for (const auto &port : ports)
        {
            // The resolved signature is the only truth about what a port carries: a type-derived
            // port has no declared type of its own until its source is known.
            const auto signature = signatures.find(port.id);
            if (signature == signatures.end() || signature->second.type != Type::Image)
                continue;
            // A port whose type comes from another node cannot declare display attributes at all —
            // the declaration rejects type_from together with display — so it is offered whenever
            // it resolves to an image. A plain port appears only when the author marked it visible.
            const bool derived = port.typeFrom.has_value() || node->outputTypeSources.contains(port.id);
            if (visibleOnly && !port.imageVisible && !derived)
                continue;
            const PortRef ref{stage, port.id};
            if (std::any_of(choices.begin(), choices.end(), [&](const auto &choice) { return choice.ref == ref; }))
                continue;
            choices.push_back({ref, text(port.nameZh)});
        }
    };

    add(tool->outputs, PortStage::Outputs, node->signatures.outputs, true);
    add(tool->inputs, PortStage::Inputs, node->signatures.inputs, true);
    add(tool->initParams, PortStage::Init, node->signatures.initParams, true);
    add(tool->trainParams, PortStage::Train, node->signatures.trainParams, true);

    // An ROI session edits its declared input image, which the visible-only pass above already
    // listed when the author offers it. Nothing hidden is added here: the session owns the
    // background it displays, and the selector must never expose another input.
    return choices;
}

void ResultPresentation::ensureImageSelection(const NodeDefinition &node, const ResultSlot::Snapshot &snapshot)
{
    if (roi_.active())
        return;
    // An empty choice list means the catalog cannot answer yet — a run in flight, a contract still
    // resolving — and that is not a reason to drop the user's port.
    const auto choices = imageChoices();
    if (choices.empty())
        return;
    if (imageSelection_
        && std::any_of(choices.begin(), choices.end(),
                       [&](const ImageChoice &choice) { return choice.ref == *imageSelection_; }))
        return;
    // Prefer a port that already carries images: a declared but still empty output would show a
    // name with a blank canvas, which is the state this default exists to avoid.
    const auto carriesImages = [&](const ImageChoice &choice)
    {
        if (choice.ref.stage == PortStage::Outputs)
        {
            const auto *batch = images(snapshot, choice.ref.id);
            return batch && !batch->get().empty();
        }
        const auto value = resolvedValue(node, choice.ref);
        const auto *batch = std::get_if<DataRef<Image>>(&value);
        return batch && !batch->get().empty();
    };
    const auto available = std::find_if(choices.begin(), choices.end(), carriesImages);
    imageSelection_      = (available != choices.end() ? available : choices.begin())->ref;
}

QVariantList ResultPresentation::imageOutputs() const
{
    QVariantList result;
    for (const auto &choice : imageChoices())
        result.push_back(QVariantMap{
            {   "id",               imageKey(choice.ref)},
            { "name",                        choice.name},
            {"stage", static_cast<int>(choice.ref.stage)}
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
    // An ROI session presents the image its input edits as the canvas background. That is an internal
    // read for editing, not a selection: it is reported only when the selector would offer the same
    // choice, so an image the author hid can never be read back as the user's selection.
    if (roi_.active())
    {
        const auto *declaration = roiDeclaration();
        if (!declaration || !declaration->image)
            return {};
        const auto key     = imageKey(*declaration->image);
        const auto choices = imageChoices();
        const bool offered = std::any_of(choices.begin(), choices.end(),
                                         [&](const ImageChoice &choice) { return imageKey(choice.ref) == key; });
        return offered ? key : QString{};
    }
    return imageSelection_ ? imageKey(*imageSelection_) : QString{};
}

QString ResultPresentation::thumbnailUrl(int index) const
{
    if (thumbnailVersion_ == 0 || index < 0 || index >= imageCount())
        return {};
    return QStringLiteral("image://visionrt_results/%1?v=%2").arg(index).arg(thumbnailVersion_);
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
    if (std::none_of(choices.begin(), choices.end(), [&](const ImageChoice &choice) { return choice.ref == *parsed; }))
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
        const auto current = host_.portContext(intent.value("port").toInt());
        if (intent == textTarget(current))
            requestText(current);
    }
    const bool textQueued = !readRequests_.empty();
    if (readback_ && runtime_ && !textTask_.valid() && !textQueued && !converter_.isBusy() && !converter_.hasPending())
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
            = std::any_of(readRequests_.begin(), readRequests_.end(), [&](const auto &item) { return item == target; });
        if (!alreadyQueued)
            readRequests_.push_back(target);
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
    textTask_ = std::async(std::launch::async,
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
                               QMetaObject::invokeMethod(
                                   &converter_, [this, mailbox] { consumeTextMailbox(mailbox); }, Qt::QueuedConnection);
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
    draining_          = false;
    auto continuation  = std::move(drainContinuation_);
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

        DrainGuard(bool &f)
            : flag(f)
            , previous(f)
        {
            flag = true;
        }

        ~DrainGuard()
        {
            flag = previous;
        }
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
