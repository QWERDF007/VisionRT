#pragma once

#include "core/ProjectEditor.h"
#include "ui/ImageViewport.h"

#include <QString>
#include <QVariantMap>

#include <array>
#include <cstdint>
#include <optional>
#include <string>

namespace visionrt {

// Owns the in-progress ROI edit: which input is being edited, the draft value, the frozen
// background and the view state the viewport returns to. It produces "target + new ROI value" and
// writes it through the existing ProjectEditor; the viewport and the controller never write the
// draft a second time.
class RoiEditSession
{
public:
    struct Start
    {
        NodeId        node            = 0;
        std::string   input;
        QString       flow;
        TypeSpec      type;
        bool          singleRectangle = false;
        InputBinding  original;
        PortValue     background;
        PortValue     draft;
        int           displayIndex    = 0;
        int           resultIndex     = 0;
        ImageViewport::ViewState view{ImageViewport::Contain, 1.0, {}};
    };

    struct Commit
    {
        bool accepted       = false;
        bool changedBinding = false;
    };

    explicit RoiEditSession(ProjectEditor &editor);

    // Starts a new session and returns its id. Any previous session ends first.
    std::uint64_t begin(Start start);
    // Ends the session; the id is invalidated even when no new session starts.
    void          end();

    bool               active() const noexcept { return draft_.has_value(); }
    std::uint64_t      id() const noexcept { return draft_ ? draft_->id : 0; }
    NodeId             node() const noexcept { return draft_ ? draft_->node : 0; }
    const std::string &input() const noexcept;
    const QString     &flow() const noexcept;
    const TypeSpec    &type() const noexcept;
    bool               singleRectangle() const noexcept { return draft_ && draft_->singleRectangle; }
    const InputBinding &original() const noexcept;
    const PortValue    &background() const noexcept;
    const PortValue    &draft() const noexcept;
    int                 displayIndex() const noexcept { return draft_ ? draft_->displayIndex : 0; }
    void                setDisplayIndex(int value);
    int                 resultIndex() const noexcept { return draft_ ? draft_->resultIndex : 0; }
    const ImageViewport::ViewState &view() const noexcept;
    bool                fieldsValid() const noexcept { return draft_ && draft_->fieldsValid; }
    void                setFieldsValid(bool value);

    // Applies one geometry edit (create, move, resize or delete) or a numeric rectangle and writes
    // the literal binding. Throws ContractError when the session or the value is no longer valid.
    Commit commitGeometry(const RoiEditTarget &target, const EncodedRegion *value);
    Commit commitRectangle(const std::array<double, 5> &values);

    // QML rectangle editor payload; empty unless the session edits a single rectangle.
    QVariantMap editor() const;

private:
    struct Draft
    {
        std::uint64_t            id;
        NodeId                   node;
        std::string              input;
        QString                  flow;
        TypeSpec                 type;
        bool                     singleRectangle;
        InputBinding             original;
        PortValue                background;
        PortValue                draft;
        int                      displayIndex = 0;
        int                      resultIndex  = 0;
        ImageViewport::ViewState view{ImageViewport::Contain, 1.0, {}};
        bool                     fieldsValid = true;
    };

    Commit writeLiteral(InputBinding committed);

    std::optional<Draft> draft_;
    std::uint64_t        serial_ = 0;
    ProjectEditor        &editor_;
};

} // namespace visionrt
