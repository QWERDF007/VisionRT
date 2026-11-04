#include "ui/RoiEditSession.h"

#include "visionrt/contracts/Value.h"

#include <algorithm>
#include <span>
#include <utility>

namespace visionrt {

RoiEditSession::RoiEditSession(ProjectEditor &editor)
    : editor_(editor)
{
}

std::uint64_t RoiEditSession::begin(Start start)
{
    end();
    Draft draft{++serial_,
                start.node,
                std::move(start.input),
                std::move(start.flow),
                std::move(start.type),
                start.singleRectangle,
                std::move(start.original),
                std::move(start.background),
                std::move(start.draft),
                start.displayIndex,
                start.resultIndex,
                std::move(start.view),
                true};
    draft_ = std::move(draft);
    return draft_->id;
}

void RoiEditSession::end()
{
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

const QString &RoiEditSession::flow() const noexcept
{
    static const QString empty;
    return draft_ ? draft_->flow : empty;
}

const TypeSpec &RoiEditSession::type() const noexcept
{
    static const TypeSpec empty;
    return draft_ ? draft_->type : empty;
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

void RoiEditSession::setFieldsValid(bool value)
{
    if (draft_)
        draft_->fieldsValid = value;
}

RoiEditSession::Commit RoiEditSession::writeLiteral(InputBinding committed)
{
    Commit result;
    const bool changed = editor_.setBinding(draft_->flow.toStdString(), draft_->node, draft_->input,
                                            std::move(committed));
    result.accepted       = true;
    result.changedBinding = changed;
    return result;
}

RoiEditSession::Commit RoiEditSession::commitGeometry(const RoiEditTarget &target, const EncodedRegion *value)
{
    if (!draft_ || target.session != draft_->id || target.nodeSeq != draft_->node || target.inputPortId != draft_->input
        || target.imageIndex != draft_->displayIndex)
        return {};
    const auto *images = std::get_if<DataRef<Image>>(&draft_->background);
    if (!images || target.imageIndex < 0 || static_cast<std::size_t>(target.imageIndex) >= images->get().size())
        return {};

    if (draft_->singleRectangle)
        draft_->draft = DataRef<ROI>(value ? ROI{{*value}} : ROI{});
    else
    {
        auto updated = std::get<DataRef<ROI>>(draft_->draft).get();
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
        draft_->draft = DataRef<ROI>(std::move(updated));
    }
    validateValue(draft_->type, draft_->draft);
    InputBinding committed{BindingMode::Literal, draft_->draft, std::nullopt};
    draft_->original = committed;
    return writeLiteral(std::move(committed));
}

RoiEditSession::Commit RoiEditSession::commitRectangle(const std::array<double, 5> &values)
{
    if (!draft_ || !draft_->singleRectangle)
        return {};
    DataRef<ROI> rectangle(ROI{{makeRectangle(values[0], values[1], values[2], values[3], values[4])}});
    validateValue(draft_->type, rectangle);
    draft_->draft = std::move(rectangle);
    if (draft_->original.mode == BindingMode::Upstream)
        return {true, false};
    InputBinding committed{BindingMode::Literal, draft_->draft, draft_->original.source};
    draft_->original = committed;
    return writeLiteral(std::move(committed));
}

QVariantMap RoiEditSession::editor() const
{
    if (!draft_ || !draft_->singleRectangle)
        return {};
    QVariantList coordinates;
    const auto  &regions = std::get<DataRef<ROI>>(draft_->draft).get();
    if (!regions.empty() && !regions[0].empty())
    {
        try
        {
            const auto rectangle = RegionView(regions[0][0]).rectangle();
            for (const double component : {rectangle.left(), rectangle.top(), rectangle.width(), rectangle.height(),
                                           rectangle.angle()})
                coordinates.push_back(component);
        }
        catch (const ContractError &)
        {
            // The editor shows a non-rectangle draft as raw numbers; commit reports the actual
            // problem instead of hiding the value from the user.
            for (const auto component : std::span<const double>(regions[0][0]).subspan(1))
                coordinates.push_back(component);
        }
    }
    const auto *images = std::get_if<DataRef<Image>>(&draft_->background);
    const auto *image  = images && !images->get().empty() ? &images->get()[draft_->displayIndex] : nullptr;
    return {
        {         "id",   QString::number(draft_->id)},
        {       "node", QString::number(draft_->node)},
        {      "input",                                   draft_->input},
        {  "rectangle",                   coordinates},
        { "imageWidth",           image ? image->cols : 0},
        {"imageHeight",           image ? image->rows : 0}
    };
}

} // namespace visionrt
