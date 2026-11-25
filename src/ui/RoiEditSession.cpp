#include "ui/RoiEditSession.h"

#include "visionrt/contracts/Value.h"

#include <algorithm>
#include <utility>

namespace visionrt {

std::uint64_t RoiEditSession::begin(Start start)
{
    end();
    Draft draft{++serial_,
                start.node,
                start.kind,
                std::move(start.input),
                std::move(start.type),
                start.singleRectangle,
                std::move(start.original),
                std::move(start.background),
                std::move(start.draft),
                start.displayIndex,
                start.resultIndex,
                std::move(start.view)};
    draft_ = std::move(draft);
    return draft_->id;
}

void RoiEditSession::end()
{
    previewBase_.reset();
    if (!draft_)
        return;
    // Bumping the serial invalidates late viewport callbacks and assist previews even when no new
    // session follows.
    ++serial_;
    draft_.reset();
}

PortId RoiEditSession::input() const noexcept
{
    return draft_ ? draft_->input : 0;
}

PortStage RoiEditSession::parameterStage() const noexcept
{
    if (!draft_) return PortStage::Inputs;
    return draft_->kind == RoiTargetKind::Initialization ? PortStage::Init
        : draft_->kind == RoiTargetKind::TrainingParameter || draft_->kind == RoiTargetKind::Training ? PortStage::Train
        : PortStage::Inputs;
}

RoiEditTarget RoiEditSession::target(int index) const
{
    return draft_ ? RoiEditTarget{draft_->node, draft_->input, draft_->displayIndex, index, draft_->id, draft_->kind}
                  : RoiEditTarget{};
}

const InputBinding &RoiEditSession::original() const noexcept
{
    static const InputBinding empty{};
    return draft_ ? draft_->original : empty;
}

const PortValue &RoiEditSession::background() const noexcept
{
    static const PortValue empty;
    return draft_ ? draft_->background : empty;
}

const PortValue &RoiEditSession::draft() const noexcept
{
    static const PortValue empty;
    return draft_ ? draft_->draft : empty;
}

const ImageViewport::ViewState &RoiEditSession::view() const noexcept
{
    static const ImageViewport::ViewState empty{ImageViewport::Contain, 1.0, {}};
    return draft_ ? draft_->view : empty;
}

void RoiEditSession::setDisplayIndex(int value)
{
    if (draft_)
        draft_->displayIndex = value;
}

bool RoiEditSession::stageGeometry(const RoiEditTarget &target, const EncodedRegion *value)
{
    if (!draft_ || target.kind != draft_->kind || target.session != draft_->id || target.nodeSeq != draft_->node
        || target.inputPortId != draft_->input || target.imageIndex != draft_->displayIndex)
        return {};
    const auto *images = std::get_if<DataRef<Image>>(&draft_->background);
    if (target.imageIndex < 0
        || (images && !images->get().empty() && static_cast<std::size_t>(target.imageIndex) >= images->get().size()))
        return {};

    if (draft_->singleRectangle)
        draft_->draft = shapeValue(draft_->type.type, value ? ROI{{*value}} : ROI{});
    else
    {
        auto updated = *shapeRegions(draft_->draft);
        updated.resize(std::max(updated.size(), static_cast<std::size_t>(target.imageIndex) + 1));
        auto &current = updated[static_cast<std::size_t>(target.imageIndex)];
        if (target.roiIndex < 0 && value)
            current.push_back(*value);
        else if (target.roiIndex >= 0 && static_cast<std::size_t>(target.roiIndex) < current.size())
        {
            if (value)
                current[static_cast<std::size_t>(target.roiIndex)] = *value;
            else
                current.erase(current.begin() + target.roiIndex);
        }
        else
            return {};
        draft_->draft = shapeValue(draft_->type.type, std::move(updated));
    }
    validateValue(draft_->type, draft_->draft);
    return true;
}

void RoiEditSession::cancelPreview()
{
    if (previewBase_ && draft_)
        draft_->draft = *previewBase_;
    previewBase_.reset();
}

bool RoiEditSession::previewGeometry(const RoiEditTarget &target, const EncodedRegion &value)
{
    if (!draft_)
        return false;
    if (!previewBase_)
        previewBase_ = draft_->draft;
    draft_->draft = *previewBase_;
    return stageGeometry(target, &value);
}

bool RoiEditSession::commitGeometry(const RoiEditTarget &target, const EncodedRegion *value)
{
    cancelPreview();
    if (!stageGeometry(target, value))
        return {};
    draft_->original = {BindingMode::Literal, draft_->draft, std::nullopt};
    return true;
}

} // namespace visionrt
