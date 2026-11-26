#pragma once

#include "core/Project.h"
#include "core/ToolDefinition.h"
#include "ui/Export.h"

#include <QImage>
#include <QString>
#include <QVariantList>
#include <map>
#include <optional>
#include <vector>

namespace visionrt {

VISIONRT_UI_API QVariant trainingFieldVariant(const PortValue &value);

struct TrainingDraftImage
{
    TrainingImageId id = 0;
    QString source;
    cv::Mat pixels;
    ColorOrder colorOrder = ColorOrder::Automatic;
    QImage thumbnailImage;
    quint64 thumbnailRevision = 0;
};

struct TrainingDraftRecord
{
    TrainingRecordId number = 0;
    std::optional<TrainingImageId> image;
    Values fields;
    QString id() const { return QString::number(number); }
};

// Owns the editable values passed to train(). Dataset metadata only configures the record editor;
// it neither changes the call signature nor requires a picture or canvas field.
class VISIONRT_UI_API TrainingSession
{
public:
    struct Begin
    {
        NodeId node = 0;
        TrainingSpec spec;
        std::vector<PortDefinition> trainParams;
        Values parameters;
    };

    void begin(Begin start);
    void end();
    bool active() const noexcept { return active_; }
    NodeId node() const noexcept { return start_.node; }
    const TrainingSpec &spec() const noexcept { return start_.spec; }
    void synchronizeParameter(PortId parameter, const PortValue &value);
    void accept();
    bool dirty() const noexcept { return dirty_; }
    QString error() const { return error_; }

    const TrainingDatasetSpec *datasetSpec() const;
    std::optional<PortId> datasetParameter() const noexcept { return dataset_; }
    bool selectDataset(PortId parameter);
    const std::vector<PortDefinition> &recordParameters() const;
    const std::vector<PortDefinition> &toolParameters() const { return start_.trainParams; }
    const std::vector<TrainingDraftImage> &images() const;
    const std::vector<TrainingDraftRecord> &records() const;
    int selected() const;
    bool select(int index);
    std::optional<int> addRecord(const cv::Mat &pixels = {}, ColorOrder colorOrder = ColorOrder::Automatic, const QString &source = {});
    bool remove(int index);
    bool setField(int index, PortId field, PortValue value);
    std::optional<PortValue> value(int index, PortId field) const;
    std::optional<PortId> roiParameter() const;
    bool selectRoiParameter(PortId field);
    std::vector<EncodedRegion> regions(int index) const;
    bool setRegions(int index, std::vector<EncodedRegion> regions);
    int regionBudget() const;
    std::optional<PortValue> parameter(PortId id) const;
    bool setParameter(PortId id, PortValue value);
    Values parameters() const;
    QVariantList rows() const;
    QVariantList fields(int index) const;
    DataRef<TrainingSet> trainingSet() const;
    std::map<PortId, DataRef<TrainingSet>> trainingSets() const;
    bool savable() const;
    bool validate();

private:
    struct Dataset
    {
        std::vector<TrainingDraftImage> images;
        std::vector<TrainingDraftRecord> records;
        int selected = -1;
        TrainingImageId nextImage = 1;
        TrainingRecordId nextRecord = 1;
        std::optional<PortId> roi;
    };
    Dataset *data();
    const Dataset *data() const;
    const PortDefinition *field(PortId id) const;
    static DataRef<TrainingSet> snapshot(const Dataset &data);
    static Dataset draft(const TrainingSet &value);
    static void pruneImages(Dataset &data);

    Begin start_;
    bool active_ = false;
    bool dirty_ = false;
    QString error_;
    Values parameters_;
    std::map<PortId, Dataset> datasets_;
    std::optional<PortId> dataset_;
};

} // namespace visionrt
