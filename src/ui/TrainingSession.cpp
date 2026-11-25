#include "ui/TrainingSession.h"

#include "core/ToolValueValidation.h"
#include "ui/DisplayConverter.h"
#include "ui/ValueDisplay.h"

#include <QBuffer>
#include <QJsonArray>
#include <algorithm>

namespace visionrt {
namespace {
QString thumbnailOf(const cv::Mat &pixels, ColorOrder order)
{
    const auto frame = DisplayConverter::convert(pixels, order);
    const auto preview = frame.image.scaled(160, 120, Qt::KeepAspectRatio, Qt::SmoothTransformation);
    QByteArray bytes;
    QBuffer buffer(&bytes);
    buffer.open(QIODevice::WriteOnly);
    preview.save(&buffer, "PNG");
    return QStringLiteral("data:image/png;base64,") + QString::fromLatin1(bytes.toBase64());
}
}

QVariant trainingFieldVariant(const PortValue &value)
{
    if (const auto *v = std::get_if<bool>(&value)) return *v;
    if (const auto *v = std::get_if<double>(&value)) return *v;
    if (const auto *v = std::get_if<std::string>(&value)) return QString::fromStdString(*v);
    if (const auto *v = shapeRegions(value))
        return v->empty() ? 0 : static_cast<int>(v->front().size());
    return isMissing(value) ? QString{} : displayValueText(value, *typeOf(value));
}

void TrainingSession::begin(Begin start)
{
    if (active_ && start_.node == start.node && start_.spec == start.spec && start_.asset == start.asset) return;
    end();
    start_ = std::move(start);
    active_ = true;
    for (const auto &p : start_.trainParams) parameters_[p.id] = copyProjectLiteral(p.defaultValue);
    for (const auto &[id, value] : start_.parameters) parameters_[id] = copyProjectLiteral(value);
    for (const auto &spec : start_.spec.datasets)
    {
        datasets_.emplace(spec.parameter, Dataset{});
        if (!dataset_) dataset_ = spec.parameter;
        if (const auto found = parameters_.find(spec.parameter); found != parameters_.end())
            if (const auto *set = std::get_if<DataRef<TrainingSet>>(&found->second))
                datasets_.at(spec.parameter) = draft(set->get());
    }
}

void TrainingSession::end()
{
    start_ = {};
    active_ = dirty_ = false;
    error_.clear();
    parameters_.clear();
    datasets_.clear();
    dataset_.reset();
}

void TrainingSession::adoptAsset(std::filesystem::path asset)
{
    start_.asset = std::move(asset);
    dirty_ = false;
    error_.clear();
}

void TrainingSession::setAsset(std::filesystem::path asset)
{
    if (asset == start_.asset) return;
    start_.asset = std::move(asset);
}

TrainingSession::Dataset *TrainingSession::data()
{
    return dataset_ ? &datasets_.at(*dataset_) : nullptr;
}
const TrainingSession::Dataset *TrainingSession::data() const
{
    return dataset_ ? &datasets_.at(*dataset_) : nullptr;
}
const TrainingDatasetSpec *TrainingSession::datasetSpec() const
{
    if (!dataset_) return nullptr;
    const auto found = std::ranges::find(start_.spec.datasets, *dataset_, &TrainingDatasetSpec::parameter);
    return &*found;
}
bool TrainingSession::selectDataset(PortId parameter)
{
    if (!datasets_.contains(parameter)) return false;
    dataset_ = parameter;
    return true;
}
const std::vector<PortDefinition> &TrainingSession::recordParameters() const
{
    static const std::vector<PortDefinition> empty;
    const auto *spec = datasetSpec();
    return spec ? spec->fields : empty;
}
const PortDefinition *TrainingSession::field(PortId id) const
{
    const auto &fields = recordParameters();
    const auto found = std::ranges::find(fields, id, &PortDefinition::id);
    return found == fields.end() ? nullptr : &*found;
}
const std::vector<TrainingDraftImage> &TrainingSession::images() const
{
    static const std::vector<TrainingDraftImage> empty;
    return data() ? data()->images : empty;
}
const std::vector<TrainingDraftRecord> &TrainingSession::records() const
{
    static const std::vector<TrainingDraftRecord> empty;
    return data() ? data()->records : empty;
}
int TrainingSession::selected() const { return data() ? data()->selected : -1; }
bool TrainingSession::select(int index)
{
    auto *d = data();
    if (!d || index < -1 || index >= static_cast<int>(d->records.size()) || index == d->selected) return false;
    d->selected = index;
    return true;
}
std::optional<PortId> TrainingSession::roiParameter() const { return data() ? data()->roi : std::nullopt; }
bool TrainingSession::selectRoiParameter(PortId id)
{
    const auto *p = field(id);
    if (!p || !isShapeType(p->type.type)) return false;
    data()->roi = id;
    return true;
}
int TrainingSession::regionBudget() const
{
    const auto roi = roiParameter();
    return roi ? field(*roi)->maxRegions : 0;
}
void TrainingSession::pruneImages(Dataset &d)
{
    std::erase_if(d.images, [&](const auto &image) {
        return std::ranges::none_of(d.records, [&](const auto &record) { return record.image == image.id; });
    });
}
std::optional<int> TrainingSession::addRecord(const cv::Mat &pixels, ColorOrder order, const QString &source)
{
    auto *d = data();
    if (!d) return std::nullopt;
    const int maximum = datasetSpec()->maxRecords;
    if (maximum > 1 && static_cast<int>(d->records.size()) >= maximum)
    {
        error_ = QStringLiteral("训练记录已达到声明的上限");
        return std::nullopt;
    }
    Values carried;
    if (maximum == 1 && !d->records.empty())
    {
        const auto image = std::ranges::find(d->images, d->records.front().image, &TrainingDraftImage::id);
        if (image != d->images.end() && image->pixels.size() == pixels.size() && image->pixels.type() == pixels.type())
            carried = d->records.front().fields;
    }
    auto existing = std::ranges::find(d->images, source, &TrainingDraftImage::source);
    std::optional<TrainingImageId> id;
    if (!pixels.empty() && !source.isEmpty() && existing != d->images.end()) id = existing->id;
    else if (!pixels.empty())
    {
        id = d->nextImage++;
        d->images.push_back({*id, source, pixels.clone(), order, thumbnailOf(pixels, order)});
    }
    if (maximum == 1) d->records.clear();
    TrainingDraftRecord record{d->nextRecord++, id, {}};
    for (const auto &p : recordParameters())
        record.fields[p.id] = carried.contains(p.id) ? carried.at(p.id) : copyProjectLiteral(p.defaultValue);
    d->records.push_back(std::move(record));
    pruneImages(*d);
    d->selected = static_cast<int>(d->records.size()) - 1;
    dirty_ = true;
    error_.clear();
    return d->selected;
}
bool TrainingSession::remove(int index)
{
    auto *d = data();
    if (!d || index < 0 || index >= static_cast<int>(d->records.size())) return false;
    d->records.erase(d->records.begin() + index);
    pruneImages(*d);
    d->selected = d->records.empty() ? -1 : std::min(index, static_cast<int>(d->records.size()) - 1);
    dirty_ = true;
    return true;
}
bool TrainingSession::setField(int index, PortId id, PortValue value)
{
    auto *d = data();
    const auto *p = field(id);
    if (!d || !p || index < 0 || index >= static_cast<int>(d->records.size())) return false;
    try { validateToolValue(*p, value, ValueCompleteness::Draft); }
    catch (const ContractError &) { return false; }
    auto &stored = d->records[index].fields[id];
    if (equalValues(stored, value)) return false;
    stored = copyProjectLiteral(value);
    dirty_ = true;
    return true;
}
std::optional<PortValue> TrainingSession::value(int index, PortId id) const
{
    if (index < 0 || index >= static_cast<int>(records().size())) return std::nullopt;
    const auto &fields = records()[index].fields;
    const auto found = fields.find(id);
    return found == fields.end() ? std::nullopt : std::optional<PortValue>{found->second};
}
std::vector<EncodedRegion> TrainingSession::regions(int index) const
{
    if (const auto roi = roiParameter())
        if (const auto stored = value(index, *roi))
            if (const auto *r = shapeRegions(*stored); r && !r->empty()) return r->front();
    return {};
}
bool TrainingSession::setRegions(int index, std::vector<EncodedRegion> regions)
{
    const auto roi = roiParameter();
    return roi && setField(index, *roi, shapeValue(field(*roi)->type.type, ROI{std::move(regions)}));
}
std::optional<PortValue> TrainingSession::parameter(PortId id) const
{
    if (const auto d = datasets_.find(id); d != datasets_.end()) return snapshot(d->second);
    const auto found = parameters_.find(id);
    return found == parameters_.end() ? std::nullopt : std::optional<PortValue>{found->second};
}
bool TrainingSession::setParameter(PortId id, PortValue value)
{
    const auto p = std::ranges::find(start_.trainParams, id, &PortDefinition::id);
    if (p == start_.trainParams.end()) return false;
    try { validateToolValue(*p, value, ValueCompleteness::Draft); }
    catch (const ContractError &) { return false; }
    if (const auto current = parameter(id); current && equalValues(*current, value)) return false;
    if (datasets_.contains(id))
    {
        const auto roi = datasets_.at(id).roi;
        datasets_.at(id) = draft(std::get<DataRef<TrainingSet>>(value).get());
        datasets_.at(id).roi = roi;
    }
    else parameters_[id] = copyProjectLiteral(value);
    dirty_ = true;
    return true;
}
Values TrainingSession::parameters() const
{
    auto result = parameters_;
    for (const auto &[id, d] : datasets_) result[id] = snapshot(d);
    return result;
}
DataRef<TrainingSet> TrainingSession::snapshot(const Dataset &d)
{
    TrainingSet result;
    for (const auto &image : d.images)
        result.images.emplace(image.id, TrainingImage{image.id, image.pixels, image.colorOrder});
    for (const auto &r : d.records) result.records.emplace(r.number, TrainingRecord{r.number, r.image, r.fields});
    return DataRef<TrainingSet>(std::move(result));
}
TrainingSession::Dataset TrainingSession::draft(const TrainingSet &value)
{
    Dataset d;
    for (const auto &[id, image] : value.images)
    {
        d.images.push_back({id, {}, image.pixels.clone(), image.colorOrder, thumbnailOf(image.pixels, image.colorOrder)});
        d.nextImage = std::max(d.nextImage, id + 1);
    }
    for (const auto &[id, record] : value.records)
    {
        Values fields;
        for (const auto &[key, v] : record.fields) fields[key] = copyProjectLiteral(v);
        d.records.push_back({id, record.imageId, std::move(fields)});
        d.nextRecord = std::max(d.nextRecord, id + 1);
    }
    d.selected = d.records.empty() ? -1 : 0;
    return d;
}
DataRef<TrainingSet> TrainingSession::trainingSet() const
{
    return data() ? snapshot(*data()) : DataRef<TrainingSet>(TrainingSet{});
}
std::map<PortId, DataRef<TrainingSet>> TrainingSession::trainingSets() const
{
    std::map<PortId, DataRef<TrainingSet>> result;
    for (const auto &[id, d] : datasets_) result.emplace(id, snapshot(d));
    return result;
}
QVariantList TrainingSession::fields(int index) const
{
    QVariantList result;
    if (index < 0 || index >= static_cast<int>(records().size())) return result;
    for (const auto &p : recordParameters())
    {
        const auto v = value(index, p.id);
        result.append(QVariantMap{{"id", p.id}, {"name", QString::fromStdString(p.nameZh)},
            {"type", static_cast<int>(p.type.type)}, {"editor", static_cast<int>(p.editor)},
            {"roi", isShapeType(p.type.type)}, {"value", v ? trainingFieldVariant(*v) : QVariant{}}});
    }
    return result;
}
QVariantList TrainingSession::rows() const
{
    QVariantList result;
    for (std::size_t i = 0; i < records().size(); ++i)
    {
        const auto &r = records()[i];
        const auto found = std::ranges::find(images(), r.image, &TrainingDraftImage::id);
        result.append(QVariantMap{{"id", r.id()}, {"imageId", r.image ? QVariant(*r.image) : QVariant{}},
            {"imageUrl", found == images().end() ? QString{} : found->thumbnail},
            {"regions", static_cast<int>(regions(static_cast<int>(i)).size())}, {"selected", static_cast<int>(i) == selected()},
            {"fields", fields(static_cast<int>(i))}});
    }
    return result;
}
bool TrainingSession::savable() const
{
    if (!active_) return false;
    try
    {
        validateToolValues(start_.trainParams, parameters());
        for (const auto &spec : start_.spec.datasets)
        {
            const auto &d = datasets_.at(spec.parameter);
            if (spec.maxRecords > 0 && static_cast<int>(d.records.size()) > spec.maxRecords) return false;
            for (const auto &r : d.records) validateToolValues(spec.fields, r.fields);
        }
    }
    catch (const ContractError &) { return false; }
    return true;
}
bool TrainingSession::validate()
{
    if (savable()) { error_.clear(); return true; }
    if (error_.isEmpty()) error_ = QStringLiteral("训练参数不满足声明约束");
    return false;
}
QJsonObject TrainingSession::manifest(const std::map<std::string, QString> &artifacts) const
{
    QJsonArray files;
    for (const auto &a : start_.spec.artifacts)
        files.append(QJsonObject{{"id", QString::fromStdString(a.id)}, {"format", QString::fromStdString(a.format)},
                                 {"version", a.version}, {"file", artifacts.at(a.id)}});
    return {{"schema_version", kTrainingManifestVersion}, {"artifacts", files}};
}

} // namespace visionrt
