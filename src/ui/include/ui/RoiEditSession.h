#pragma once

#include "core/Project.h"
#include "ui/Export.h"
#include "ui/ImageViewport.h"

#include <QString>
#include <cstdint>
#include <optional>
#include <string>

namespace visionrt {

// Owns the ROI draft, frozen background and gesture baseline. The controller commits its value
// through the parameter's owner, so initialization, training and input values share this editor.
class VISIONRT_UI_API RoiEditSession
{
public:
    struct Start
    {
        NodeId                   node  = 0;
        RoiTargetKind            kind  = RoiTargetKind::Input;
        PortId                   input = 0;
        TypeSpec                 type;
        bool                     singleRectangle = false;
        InputBinding             original;
        PortValue                background;
        PortValue                draft;
        int                      displayIndex = 0;
        int                      resultIndex  = 0;
        ImageViewport::ViewState view{ImageViewport::Contain, 1.0, {}};
    };

    // Starts a new session and returns its id. Any previous session ends first.
    std::uint64_t begin(Start start);
    // Ends the session; the id is invalidated even when no new session starts.
    void          end();

    bool active() const noexcept
    {
        return draft_.has_value();
    }

    std::uint64_t id() const noexcept
    {
        return draft_ ? draft_->id : 0;
    }

    NodeId node() const noexcept
    {
        return draft_ ? draft_->node : 0;
    }

    PortId          input() const noexcept;
    PortStage parameterStage() const noexcept;
    const TypeSpec &type() const noexcept
    {
        static const TypeSpec empty;
        return draft_ ? draft_->type : empty;
    }

    bool singleRectangle() const noexcept
    {
        return draft_ && draft_->singleRectangle;
    }

    RoiEditTarget       target(int index = -1) const;
    const InputBinding &original() const noexcept;
    const PortValue    &background() const noexcept;
    const PortValue    &draft() const noexcept;

    int displayIndex() const noexcept
    {
        return draft_ ? draft_->displayIndex : 0;
    }

    void setDisplayIndex(int value);

    int resultIndex() const noexcept
    {
        return draft_ ? draft_->resultIndex : 0;
    }

    const ImageViewport::ViewState &view() const noexcept;

    bool commitGeometry(const RoiEditTarget &target, const EncodedRegion *value);
    bool   previewGeometry(const RoiEditTarget &target, const EncodedRegion &value);
    void   cancelPreview();

private:
    struct Draft
    {
        std::uint64_t            id;
        NodeId                   node;
        RoiTargetKind            kind;
        PortId                   input = 0;
        TypeSpec                 type;
        bool                     singleRectangle;
        InputBinding             original;
        PortValue                background;
        PortValue                draft;
        int                      displayIndex = 0;
        int                      resultIndex  = 0;
        ImageViewport::ViewState view{ImageViewport::Contain, 1.0, {}};
    };

    bool                     stageGeometry(const RoiEditTarget &target, const EncodedRegion *value);
    std::optional<PortValue> previewBase_;

    std::optional<Draft> draft_;
    std::uint64_t        serial_ = 0;
};

} // namespace visionrt
