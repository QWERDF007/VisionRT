#pragma once

#include "ui/Export.h"

#include "core/Project.h"
#include "core/ToolDefinition.h"

#include <QImage>
#include <QString>
#include <QVariantList>

#include <filesystem>
#include <optional>
#include <vector>

namespace visionrt {

// One trained colour sample: the picture the rectangles were drawn on, its class name and the
// rectangles themselves as VisionRT encoded regions in that image's coordinates.
struct TrainingDraftRecord
{
    QString                    id;
    QString                    label;
    std::filesystem::path      image;
    std::vector<EncodedRegion> regions;
};

// The host half of a tool's training. Records are edited in the window and the asset file is the
// only persisted state; applying writes that file and reloads the node, so no second model copy can
// drift from what was trained.
class VISIONRT_UI_API TrainingSession
{
public:
    struct Begin
    {
        NodeId                     node = 0;
        TrainingKind               kind = TrainingKind::ColorSamples;
        std::filesystem::path      asset;
    };

    // Starts a session and reads the asset when it already exists. A malformed asset leaves an
    // empty session and reports the reason instead of silently discarding the user's file.
    void begin(Begin start);
    void end();

    bool          active() const noexcept { return active_; }
    NodeId        node() const noexcept { return start_.node; }
    TrainingKind  kind() const noexcept { return start_.kind; }
    // Colour samples label every rectangle; straight edges label the two reference edges instead.
    bool          colorSamples() const noexcept { return start_.kind == TrainingKind::ColorSamples; }
    // Rectangles one record may hold; 0 means the kind sets no fixed budget.
    int           regionBudget() const noexcept;
    const std::filesystem::path &asset() const noexcept { return start_.asset; }
    // Replaces the asset path when the project starts pointing somewhere else.
    void          setAsset(std::filesystem::path asset);

    const std::vector<TrainingDraftRecord> &records() const noexcept { return records_; }
    int                                selected() const noexcept { return selected_; }
    bool                               select(int index);

    // Writes the picture beside the asset and appends a record. The image is the one the user is
    // looking at, saved at full resolution; a thumbnail would train on pixels the tool never sees.
    std::optional<int> addImage(const QImage &image);
    bool               remove(int index);
    bool               setLabel(int index, const QString &label);
    bool               setRegions(int index, std::vector<EncodedRegion> regions);

    bool    dirty() const noexcept { return dirty_; }
    QString error() const noexcept { return error_; }
    // Records as QML rows: id, label, image path and a file URL for the thumbnail.
    QVariantList rows() const;

    // Refuses an asset the tool would reject: every record needs a class name and at least one
    // rectangle, and there must be at least one record.
    bool savable() const;
    bool save();

private:
    void load();

    Begin                      start_;
    bool                       active_   = false;
    bool                       dirty_    = false;
    QString                    error_;
    std::vector<TrainingDraftRecord> records_;
    int                        selected_ = -1;
};

} // namespace visionrt
