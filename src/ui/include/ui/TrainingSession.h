#pragma once

#include "core/Project.h"
#include "core/ToolDefinition.h"
#include "ui/Export.h"

#include <QJsonObject>
#include <QString>
#include <QVariantList>
#include <QVariantMap>
#include <filesystem>
#include <map>
#include <optional>
#include <ranges>
#include <vector>

namespace visionrt {

// Presentation of one stored field for a control or a test. A region field reports its count; the
// canvas reads the regions themselves through TrainingSession::regions.
VISIONRT_UI_API QVariant trainingFieldVariant(const TrainingFieldValue &value);

// The training draft and the project speak different value types for the same data: a record field
// may hold what a port value holds, and a parameter is published into a port. One conversion pair
// keeps the two from drifting apart.
VISIONRT_UI_API TrainingFieldValue trainingFieldValueOf(const PortValue &value);
VISIONRT_UI_API PortValue          portValueOf(const TrainingFieldValue &value);

// One frame the draft owns. The pixels are an independent copy of the published value: a display
// conversion is only a derivative for the list, never the source a trainer reads. Records that
// grew from the same published frame point at one entry, so two labels painted on one picture
// travel as one image with two records.
struct TrainingDraftImage
{
    TrainingImageId id = 0;
    // Identity of the published frame this copy came from; a repeated add reuses the entry.
    QString         source;
    cv::Mat         pixels;
    ColorOrder      colorOrder = ColorOrder::Automatic;
    // Display-only PNG data URL for the record list.
    QString         thumbnail;
};

// One editable record. Fields are keyed by the declaration's field number, so renaming a field
// changes only its label in the panel and a trainer reads the same key it always did.
struct TrainingDraftRecord
{
    QString                              id;
    TrainingRecordId                     number = 0;
    TrainingImageId                      image  = 0;
    std::map<PortId, TrainingFieldValue> fields;
};

// The host half of a tool's training. "加入训练" and every edit live in memory; the only moment a
// draft may reach the disk is a successful apply, which is why the session never creates a file
// while the user is still editing.
class VISIONRT_UI_API TrainingSession
{
public:
    struct Begin
    {
        NodeId                node = 0;
        TrainingSpec          spec;
        std::filesystem::path asset;
        std::vector<PortDefinition> trainParams;
        Values parameters;
    };

    // Starts a session over the tool's declaration. An existing draft for the same node, spec and
    // asset is kept, so switching pages or selecting another node and coming back continues the
    // work instead of rereading a file that was never written.
    void begin(Begin start);
    void end();

    auto recordParameters() const
    {
        return start_.trainParams | std::views::filter([](const PortDefinition &p) { return p.scope == ParameterScope::Record; });
    }
    auto toolParameters() const
    {
        return start_.trainParams | std::views::filter([](const PortDefinition &p) { return p.scope == ParameterScope::Tool; });
    }
    PortId roiParameter() const;

    bool active() const noexcept
    {
        return active_;
    }

    NodeId node() const noexcept
    {
        return start_.node;
    }

    const TrainingSpec &spec() const noexcept
    {
        return start_.spec;
    }

    const std::filesystem::path &asset() const noexcept
    {
        return start_.asset;
    }

    // Follows a changed published reference. Unapplied edits survive save-as; a clean draft reloads.
    void setAsset(std::filesystem::path asset);

    const std::vector<TrainingDraftImage> &images() const noexcept
    {
        return images_;
    }

    const std::vector<TrainingDraftRecord> &records() const noexcept
    {
        return records_;
    }

    int selected() const noexcept
    {
        return selected_;
    }

    bool select(int index);

    // Copies the frame into the draft and appends (or, for a one-record tool, replaces) its record.
    // `source` identifies the published frame; two adds from one frame share one image entry.
    std::optional<int>                addImage(const cv::Mat &pixels, ColorOrder colorOrder, const QString &source);
    bool                              remove(int index);
    bool                              setField(int index, PortId field, TrainingFieldValue value);
    // One stored field value, for readers that do not go through the QML row projection.
    std::optional<TrainingFieldValue> value(int index, PortId field) const;
    // The canvas field's rectangles, read and replaced as a group.
    std::vector<EncodedRegion>        regions(int index) const;
    bool                              setRegions(int index, std::vector<EncodedRegion> regions);
    // Rectangles the canvas field accepts; 0 means the declaration sets no fixed budget.
    int                               regionBudget() const noexcept;

    // Tool-scoped values start from the declaration and remain in the draft until applied.
    std::optional<TrainingFieldValue>    parameter(PortId id) const;
    bool                                 setParameter(PortId id, TrainingFieldValue value);
    std::map<PortId, TrainingFieldValue> parameters() const;

    bool dirty() const noexcept
    {
        return dirty_;
    }

    QString error() const noexcept
    {
        return error_;
    }

    // The asset path a published draft now lives at. The records already equal that file, so the
    // session keeps them instead of rereading its own output.
    void adoptAsset(std::filesystem::path asset);

    // Records as QML rows: stable id, thumbnail data URL, region count of the canvas field and the
    // typed fields of the record. QML renders the fields it is given and knows no tool by name.
    QVariantList rows() const;
    QVariantList fields(int index) const;

    // The draft as one call value. Images are independent copies and fields keep their declared
    // types; a record names its image by number, never by list position.
    DataRef<TrainingSet> trainingSet() const;

    // The published manifest: typed records, image files and model files, all relative
    // to the manifest. The caller writes the files; this only states what the asset contains.
    QJsonObject manifest(const std::map<TrainingImageId, QString> &imageFiles,
                         const std::map<std::string, QString>     &artifactFiles) const;
    // Refuses a draft the trainer could not use: every declared field present, at least one record,
    // and the canvas field inside its declared region budget. `validate` reports the reason once.
    bool        savable() const;
    bool        validate();

private:
    void                         load();
    void                         materialiseThumbnail(TrainingDraftImage &image) const;
    const PortDefinition     *field(PortId id) const;
    const PortDefinition *parameterSpec(PortId id) const;
    void                         assignDefaults(TrainingDraftRecord &record) const;
    void                         pruneImages();

    Begin                                start_;
    bool                                 active_ = false;
    bool                                 dirty_  = false;
    QString                              error_;
    std::vector<TrainingDraftImage>      images_;
    std::vector<TrainingDraftRecord>     records_;
    std::map<PortId, TrainingFieldValue> parameters_;
    int                                  selected_   = -1;
    TrainingImageId                      nextImage_  = 1;
    TrainingRecordId                     nextRecord_ = 1;
};

} // namespace visionrt
