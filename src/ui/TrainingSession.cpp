#include "ui/TrainingSession.h"

#include "core/JsonFields.h"
#include "core/ToolPaths.h"
#include "core/ToolValueValidation.h"
#include "core/ValueJson.h"
#include "ui/DisplayConverter.h"
#include "ui/TrainingAssets.h"
#include "visionrt/contracts/ROI.h"

#include <QBuffer>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QUuid>
#include <algorithm>
#include <type_traits>
#include <vector>

namespace visionrt {
namespace {

QString utf8Text(const std::filesystem::path &path)
{
    const auto text = path.generic_u8string();
    return QString::fromUtf8(reinterpret_cast<const char *>(text.data()), static_cast<qsizetype>(text.size()));
}

QString nextRecordKey()
{
    return QUuid::createUuid().toString(QUuid::WithoutBraces);
}

bool sameGeometry(const cv::Mat &left, const cv::Mat &right)
{
    return !left.empty() && !right.empty() && left.rows == right.rows && left.cols == right.cols
        && left.type() == right.type();
}

TrainingFieldValue emptyFieldValue(const TypeSpec &type)
{
    switch (type.type)
    {
    case Type::Int:
        return std::int64_t{0};
    case Type::Float:
        return 0.0;
    case Type::Bool:
        return false;
    case Type::String:
        return std::string{};
    case Type::ROI:
        return DataRef<ROI>(ROI{});
    case Type::Array:
        return Array::empty(type.dtype.value_or(DType::Float64), type.rank > 0 ? type.rank : 1);
    default:
        return std::string{};
    }
}

// The controller turns a control's text into the declared field type; the session only knows that
// a value belongs to a field number.
EditorKind editorOf(const TypeSpec &type)
{
    switch (type.type)
    {
    case Type::Int:
    case Type::Float:
        return EditorKind::Number;
    case Type::Bool:
        return EditorKind::Switch;
    case Type::String:
        return EditorKind::Text;
    default:
        return EditorKind::Default;
    }
}

QString thumbnailOf(const cv::Mat &pixels, ColorOrder order)
{
    const auto frame = DisplayConverter::convert(pixels, order);
    if (frame.image.isNull())
        return {};
    const auto preview = frame.image.scaled(160, 120, Qt::KeepAspectRatio, Qt::SmoothTransformation);
    QByteArray encoded;
    QBuffer    buffer(&encoded);
    buffer.open(QIODevice::WriteOnly);
    if (!preview.save(&buffer, "PNG"))
        return {};
    return QStringLiteral("data:image/png;base64,") + QString::fromLatin1(encoded.toBase64());
}

bool sameFieldValue(const TrainingFieldValue &left, const TrainingFieldValue &right)
{
    if (left.index() != right.index())
        return false;
    if (const auto *value = std::get_if<std::int64_t>(&left))
        return *value == std::get<std::int64_t>(right);
    if (const auto *value = std::get_if<double>(&left))
        return *value == std::get<double>(right);
    if (const auto *value = std::get_if<bool>(&left))
        return *value == std::get<bool>(right);
    if (const auto *value = std::get_if<std::string>(&left))
        return *value == std::get<std::string>(right);
    return false;
}

} // namespace

QVariant trainingFieldVariant(const TrainingFieldValue &value)
{
    return std::visit(
        [](const auto &stored) -> QVariant
        {
            using Stored = std::decay_t<decltype(stored)>;
            if constexpr (std::is_same_v<Stored, std::int64_t>)
                return QString::fromStdString(formatInt64(stored));
            else if constexpr (std::is_same_v<Stored, double>)
                return stored;
            else if constexpr (std::is_same_v<Stored, bool>)
                return stored;
            else if constexpr (std::is_same_v<Stored, std::string>)
                return QString::fromStdString(stored);
            else if constexpr (std::is_same_v<Stored, DataRef<ROI>>)
                return stored.get().empty() ? 0 : static_cast<int>(stored.get().front().size());
            else
                return QVariant{};
        },
        value);
}

TrainingFieldValue trainingFieldValueOf(const PortValue &value)
{
    return std::visit(
        [](const auto &stored) -> TrainingFieldValue
        {
            using Stored = std::decay_t<decltype(stored)>;
            if constexpr (std::is_constructible_v<TrainingFieldValue, const Stored &>)
                return stored;
            else
                throw ContractError("该值不能作为训练字段");
        },
        value);
}

PortValue portValueOf(const TrainingFieldValue &value)
{
    return std::visit([](const auto &stored) -> PortValue { return stored; }, value);
}

void TrainingSession::begin(Begin start)
{
    if (active_ && start_.node == start.node && start_.spec == start.spec && start_.asset == start.asset)
        return;
    end();
    start_  = std::move(start);
    active_ = true;
    for (const auto &declared : toolParameters())
        if (!isMissing(declared.defaultValue))
            parameters_[declared.id] = trainingFieldValueOf(declared.defaultValue);
    load();
    for (const auto &[id, value] : start_.parameters) parameters_[id] = trainingFieldValueOf(value);
}

void TrainingSession::end()
{
    start_  = {};
    active_ = false;
    dirty_  = false;
    error_.clear();
    images_.clear();
    records_.clear();
    parameters_.clear();
    selected_   = -1;
    nextImage_  = 1;
    nextRecord_ = 1;
}

void TrainingSession::adoptAsset(std::filesystem::path asset)
{
    start_.asset = std::move(asset);
    dirty_       = false;
    error_.clear();
}

void TrainingSession::setAsset(std::filesystem::path asset)
{
    if (asset == start_.asset)
        return;
    // A session that never had a file (the first apply of an untrained node) adopts the path and
    // keeps its draft: the records are still only in memory, and there is no other file to read.
    // An unapplied draft keeps its records for the same reason: a project moved by "另存为" rewrites
    // the reference to sit beside the new file, but the edits the user has not committed yet are
    // still theirs and are not in any file to reload.
    const bool adopted = start_.asset.empty() || dirty_;
    start_.asset       = std::move(asset);
    if (adopted)
    {
        error_.clear();
        return;
    }
    // The records belong to the old file; a new path starts from what that file contains.
    images_.clear();
    records_.clear();
    parameters_.clear();
    for (const auto &declared : toolParameters())
        if (!isMissing(declared.defaultValue))
            parameters_[declared.id] = trainingFieldValueOf(declared.defaultValue);
    selected_ = -1;
    dirty_    = false;
    error_.clear();
    nextImage_  = 1;
    nextRecord_ = 1;
    load();
}

const PortDefinition *TrainingSession::parameterSpec(PortId id) const
{
    for (const auto &entry : toolParameters())
        if (entry.id == id)
            return &entry;
    return nullptr;
}

std::optional<TrainingFieldValue> TrainingSession::parameter(PortId id) const
{
    const auto found = parameters_.find(id);
    return found == parameters_.end() ? std::nullopt : std::optional<TrainingFieldValue>(found->second);
}

bool TrainingSession::setParameter(PortId id, TrainingFieldValue value)
{
    const auto *declared = parameterSpec(id);
    if (!declared)
        return false;
    try
    {
        validateToolValue(*declared, portValueOf(value));
    }
    catch (const ContractError &)
    {
        return false;
    }
    const auto found = parameters_.find(id);
    if (found != parameters_.end() && sameFieldValue(found->second, value))
        return false;
    parameters_[id] = std::move(value);
    dirty_          = true;
    return true;
}

std::map<PortId, TrainingFieldValue> TrainingSession::parameters() const
{
    return parameters_;
}

const PortDefinition *TrainingSession::field(PortId id) const
{
    for (const auto &entry : recordParameters())
        if (entry.id == id)
            return &entry;
    return nullptr;
}

PortId TrainingSession::roiParameter() const
{
    for (const auto &port : recordParameters()) if (port.roi) return port.id;
    return 0;
}

int TrainingSession::regionBudget() const noexcept
{
    const auto id = roiParameter();
    if (id == 0)
        return 0;
    for (const auto &entry : recordParameters())
        if (entry.id == id)
            return entry.maxRegions;
    return 0;
}

bool TrainingSession::select(int index)
{
    if (index < -1 || index >= static_cast<int>(records_.size()) || index == selected_)
        return false;
    selected_ = index;
    return true;
}

void TrainingSession::assignDefaults(TrainingDraftRecord &record) const
{
    for (const auto &entry : recordParameters()) record.fields.emplace(entry.id, isMissing(entry.defaultValue) ? emptyFieldValue(entry.type) : trainingFieldValueOf(entry.defaultValue));
}

void TrainingSession::materialiseThumbnail(TrainingDraftImage &image) const
{
    image.thumbnail = thumbnailOf(image.pixels, image.colorOrder);
}

void TrainingSession::load()
{
    if (start_.asset.empty())
        return;
    // A configured asset that is gone is a project-level fact the user has to see: silently showing
    // an empty draft would look like a node that was never trained.
    if (!std::filesystem::is_regular_file(start_.asset))
    {
        error_ = QStringLiteral("训练数据文件不存在：%1").arg(utf8Text(start_.asset));
        return;
    }
    // A malformed asset reports its reason and leaves an empty draft: a half-read file must not
    // look like a draft the user can edit and then overwrite.
    const auto fail = [&](const QString &message)
    {
        error_ = message;
        images_.clear();
        records_.clear();
        selected_   = -1;
        nextImage_  = 1;
        nextRecord_ = 1;
    };
    QFile file(QString::fromStdWString(start_.asset.wstring()));
    if (!file.open(QIODevice::ReadOnly))
    {
        fail(QStringLiteral("无法读取训练数据：%1").arg(file.errorString()));
        return;
    }
    QJsonParseError parseError;
    const auto      document = QJsonDocument::fromJson(file.readAll(), &parseError);
    if (parseError.error != QJsonParseError::NoError || !document.isObject())
    {
        fail(QStringLiteral("训练数据不是有效的 JSON：%1").arg(parseError.errorString()));
        return;
    }
    const auto root    = document.object();
    const auto version = root.value("schema_version").toInt(0);
    if (version != kTrainingManifestVersion)
    {
        fail(QStringLiteral("训练数据格式不正确"));
        return;
    }

    const auto assetBase = start_.asset.parent_path();

    const auto storeImage = [&](const QString &text, TrainingImageId id) -> std::optional<TrainingImageId>
    {
        if (id == 0 || std::ranges::any_of(images_, [&](const auto &image) { return image.id == id; }))
        {
            fail(QStringLiteral("训练原图编号缺失或重复"));
            return std::nullopt;
        }
        cv::Mat    pixels;
        ColorOrder order = ColorOrder::Automatic;
        try
        {
            const auto path = resolveToolPath(assetBase, json::relativePath(text.toStdString(), "训练原图.file"));
            training::readFrame(QString::fromStdWString(path.wstring()), pixels, order);
        }
        catch (const ContractError &error)
        {
            fail(QString::fromUtf8(error.what()));
            return std::nullopt;
        }
        TrainingDraftImage image;
        image.id         = id;
        nextImage_       = std::max(nextImage_, id + 1);
        image.source     = text;
        image.pixels     = std::move(pixels);
        image.colorOrder = order;
        materialiseThumbnail(image);
        images_.push_back(std::move(image));
        return images_.back().id;
    };

    {
        for (const auto &value : root.value("images").toArray())
        {
            const auto item = value.toObject();
            const auto text = item.value("file").toString();
            try
            {
                json::fields(item, {"id", "file"}, "训练原图");
            }
            catch (const ContractError &error)
            {
                fail(QString::fromUtf8(error.what()));
                return;
            }
            const auto id = item.value("id").toInteger();
            if (text.isEmpty() || id <= 0)
            {
                fail(QStringLiteral("训练原图缺少文件或编号"));
                return;
            }
            if (!storeImage(text, static_cast<TrainingImageId>(id)))
                return;
        }
        for (const auto &value : root.value("records").toArray())
        {
            const auto item   = value.toObject();
            const auto image  = static_cast<TrainingImageId>(item.value("image").toInteger());
            const auto stored = std::find_if(images_.begin(), images_.end(),
                                             [&](const TrainingDraftImage &entry) { return entry.id == image; });
            if (stored == images_.end())
            {
                fail(QStringLiteral("训练记录引用了不存在的训练图"));
                return;
            }
            TrainingDraftRecord record;
            const auto          storedId = item.value("id").toString();
            if (storedId.isEmpty())
            {
                fail(QStringLiteral("训练记录缺少编号"));
                return;
            }
            record.id     = storedId;
            record.number = nextRecord_++;
            record.image  = image;
            assignDefaults(record);
            const auto fields = item.value("fields").toObject();
            for (const auto &declared : recordParameters())
            {
                const auto encoded = fields.value(QString::number(declared.id));
                if (encoded.isUndefined())
                {
                    fail(QStringLiteral("训练记录缺少字段：%1").arg(QString::fromStdString(declared.nameZh)));
                    return;
                }
                try
                {
                    record.fields[declared.id] = trainingFieldValueOf(ValueJson::decode(encoded, declared.type));
                }
                catch (const ContractError &error)
                {
                    fail(QStringLiteral("训练字段 %1：%2")
                             .arg(QString::fromStdString(declared.nameZh), QString::fromUtf8(error.what())));
                    return;
                }
            }
            records_.push_back(std::move(record));
        }
        selected_ = records_.empty() ? -1 : 0;
        dirty_    = false;
        return;
    }
}

std::optional<int> TrainingSession::addImage(const cv::Mat &pixels, ColorOrder colorOrder, const QString &source)
{
    if (!active_)
        return std::nullopt;
    if (pixels.empty())
    {
        error_ = QStringLiteral("当前没有可加入训练的图像");
        return std::nullopt;
    }
    const int maxCount = start_.spec.maxRecords;
    if (maxCount > 1 && static_cast<int>(records_.size()) >= maxCount)
    {
        error_ = QStringLiteral("训练记录已达到声明的上限");
        return std::nullopt;
    }

    // A one-record tool replaces its reference picture. Edges drawn on a frame of the same geometry
    // still describe the part; a different size invalidates them and the user draws again.
    std::vector<EncodedRegion> carried;
    if (maxCount == 1 && !records_.empty())
    {
        const auto current = std::find_if(images_.begin(), images_.end(), [&](const TrainingDraftImage &image)
                                          { return image.id == records_.front().image; });
        if (current != images_.end() && sameGeometry(current->pixels, pixels))
            carried = regions(0);
    }

    TrainingImageId imageId = 0;
    if (!source.isEmpty())
    {
        const auto existing = std::find_if(images_.begin(), images_.end(),
                                           [&](const TrainingDraftImage &image) { return image.source == source; });
        if (existing != images_.end())
            imageId = existing->id;
    }
    if (imageId == 0)
    {
        TrainingDraftImage image;
        image.id         = nextImage_++;
        image.source     = source;
        image.pixels     = pixels.clone();
        image.colorOrder = colorOrder;
        materialiseThumbnail(image);
        images_.push_back(std::move(image));
        imageId = images_.back().id;
    }

    if (maxCount == 1)
        records_.clear();
    TrainingDraftRecord record;
    record.id     = nextRecordKey();
    record.number = nextRecord_++;
    record.image  = imageId;
    assignDefaults(record);
    if (const auto roi = roiParameter(); roi != 0)
        record.fields[roi] = DataRef<ROI>(ROI{carried});
    records_.push_back(std::move(record));
    pruneImages();
    selected_ = static_cast<int>(records_.size()) - 1;
    dirty_    = true;
    error_.clear();
    return selected_;
}

bool TrainingSession::remove(int index)
{
    if (index < 0 || index >= static_cast<int>(records_.size()))
        return false;
    records_.erase(records_.begin() + index);
    pruneImages();
    selected_ = records_.empty() ? -1 : std::min(index, static_cast<int>(records_.size()) - 1);
    dirty_    = true;
    return true;
}

void TrainingSession::pruneImages()
{
    std::erase_if(images_,
                  [&](const TrainingDraftImage &image)
                  {
                      return std::none_of(records_.begin(), records_.end(),
                                          [&](const TrainingDraftRecord &record) { return record.image == image.id; });
                  });
}

bool TrainingSession::setField(int index, PortId fieldId, TrainingFieldValue value)
{
    if (index < 0 || index >= static_cast<int>(records_.size()))
        return false;
    const auto *declared = field(fieldId);
    if (!declared)
        return false;
    try
    {
        validateToolValue(*declared, portValueOf(value));
    }
    catch (const ContractError &)
    {
        return false;
    }
    if (declared->roi)
    {
        const auto &groups = std::get<DataRef<ROI>>(value).get();
        if (groups.size() != 1 || (declared->maxRegions && groups.front().size() > static_cast<std::size_t>(declared->maxRegions)))
            return false;
    }
    const auto current = records_[index].fields.find(fieldId);
    if (current != records_[index].fields.end() && sameFieldValue(current->second, value))
        return false;
    records_[index].fields[fieldId] = std::move(value);
    dirty_                          = true;
    return true;
}

std::optional<TrainingFieldValue> TrainingSession::value(int index, PortId fieldId) const
{
    if (index < 0 || index >= static_cast<int>(records_.size()))
        return std::nullopt;
    const auto found = records_[index].fields.find(fieldId);
    return found == records_[index].fields.end() ? std::nullopt : std::optional<TrainingFieldValue>(found->second);
}

std::vector<EncodedRegion> TrainingSession::regions(int index) const
{
    const auto roi = roiParameter();
    if (roi == 0 || index < 0 || index >= static_cast<int>(records_.size()))
        return {};
    const auto found = records_[index].fields.find(roi);
    if (found == records_[index].fields.end())
        return {};
    const auto *value = std::get_if<DataRef<ROI>>(&found->second);
    if (!value || value->get().empty())
        return {};
    return value->get().front();
}

bool TrainingSession::setRegions(int index, std::vector<EncodedRegion> regions)
{
    return setField(index, roiParameter(), DataRef<ROI>(ROI{std::move(regions)}));
}

QVariantList TrainingSession::fields(int index) const
{
    QVariantList result;
    if (index < 0 || index >= static_cast<int>(records_.size()))
        return result;
    const auto &record = records_[index];
    for (const auto &declared : recordParameters())
    {
        const auto found = record.fields.find(declared.id);
        result.append(QVariantMap{
            {    "id",                                                  static_cast<uint>(declared.id)},
            {  "name",                                         QString::fromStdString(declared.nameZh)},
            {  "type",                                            static_cast<int>(declared.type.type)},
            {"editor",                                       static_cast<int>(editorOf(declared.type))},
            {   "roi",                                                                    declared.roi},
            { "value", found == record.fields.end() ? QVariant{} : trainingFieldVariant(found->second)}
        });
    }
    return result;
}

QVariantList TrainingSession::rows() const
{
    const auto   roi = roiParameter();
    QVariantList result;
    for (std::size_t index = 0; index < records_.size(); ++index)
    {
        const auto &record  = records_[index];
        const auto  image   = std::find_if(images_.begin(), images_.end(),
                                           [&](const TrainingDraftImage &entry) { return entry.id == record.image; });
        int         regions = 0;
        if (roi != 0)
            if (const auto found = record.fields.find(roi); found != record.fields.end())
                if (const auto *value = std::get_if<DataRef<ROI>>(&found->second))
                    regions = value->get().empty() ? 0 : static_cast<int>(value->get().front().size());
        result.append(QVariantMap{
            {      "id",                                             record.id},
            { "imageId",                       static_cast<uint>(record.image)},
            {"imageUrl", image == images_.end() ? QString{} : image->thumbnail},
            { "regions",                                               regions},
            {"selected",                  static_cast<int>(index) == selected_},
            {  "fields",                       fields(static_cast<int>(index))}
        });
    }
    return result;
}

DataRef<TrainingSet> TrainingSession::trainingSet() const
{
    TrainingSet set;
    for (const auto &image : images_)
        set.images.emplace(image.id, trainingFrame(image.id, image.pixels, image.colorOrder));
    for (const auto &record : records_)
    {
        TrainingRecord stored;
        stored.id      = record.number;
        stored.imageId = record.image;
        stored.fields  = record.fields;
        set.records.emplace(stored.id, std::move(stored));
    }
    validateTrainingSet(set);
    return DataRef<TrainingSet>(std::move(set));
}

QJsonObject TrainingSession::manifest(const std::map<TrainingImageId, QString> &imageFiles,
                                      const std::map<std::string, QString>     &artifactFiles) const
{
    QJsonObject root{
        {"schema_version", kTrainingManifestVersion}
    };
    QJsonArray images;
    for (const auto &image : images_)
    {
        const auto file = imageFiles.find(image.id);
        if (file == imageFiles.end())
            continue;
        images.append(QJsonObject{
            {  "id", static_cast<qint64>(image.id)},
            {"file",                  file->second}
        });
    }
    root.insert("images", images);

    QJsonArray records;
    for (const auto &record : records_)
    {
        QJsonObject fields;
        for (const auto &declared : recordParameters())
        {
            const auto found = record.fields.find(declared.id);
            if (found == record.fields.end())
                continue;
            fields.insert(QString::number(declared.id),
                          ValueJson::encode(portValueOf(found->second), declared.type, ValueJson::Purpose::Stored));
        }
        records.append(QJsonObject{
            {    "id",                         record.id},
            { "image", static_cast<qint64>(record.image)},
            {"fields",                            fields}
        });
    }
    root.insert("records", records);

    QJsonArray artifacts;
    for (const auto &declared : start_.spec.artifacts)
    {
        const auto file = artifactFiles.find(declared.id);
        if (file == artifactFiles.end())
            continue;
        artifacts.append(QJsonObject{
            {     "id",     QString::fromStdString(declared.id)},
            { "format", QString::fromStdString(declared.format)},
            {"version",                        declared.version},
            {   "file",                            file->second}
        });
    }
    root.insert("artifacts", artifacts);
    return root;
}

bool TrainingSession::savable() const
{
    if (records_.empty())
        return false;
    if (start_.spec.maxRecords > 0 && static_cast<int>(records_.size()) > start_.spec.maxRecords)
        return false;
    for (const auto &declared : toolParameters())
    {
        const auto found = parameters_.find(declared.id);
        if (found == parameters_.end())
            return false;
        try
        {
            validateToolValue(declared, portValueOf(found->second));
        }
        catch (const ContractError &)
        {
            return false;
        }
    }
    const auto roi = roiParameter();
    for (const auto &record : records_)
        for (const auto &declared : recordParameters())
        {
            const auto found = record.fields.find(declared.id);
            if (found == record.fields.end())
                return false;
            if (declared.type.type == Type::String)
            {
                const auto *text = std::get_if<std::string>(&found->second);
                if (!text || QString::fromStdString(*text).trimmed().isEmpty())
                    return false;
            }
            if (declared.roi)
            {
                const auto *value = std::get_if<DataRef<ROI>>(&found->second);
                if (!value)
                    return false;
                const auto count = value->get().empty() ? 0 : static_cast<int>(value->get().front().size());
                if (count < declared.minRegions)
                    return false;
                if (declared.maxRegions != 0 && count > declared.maxRegions)
                    return false;
            }
        }
    return roi == 0
        || std::any_of(records_.begin(), records_.end(),
                       [&](const TrainingDraftRecord &record)
                       {
                           const auto found = record.fields.find(roi);
                           if (found == record.fields.end())
                               return false;
                           const auto *value = std::get_if<DataRef<ROI>>(&found->second);
                           return value && !value->get().empty() && !value->get().front().empty();
                       });
}

bool TrainingSession::validate()
{
    if (!savable())
    {
        if (records_.empty())
            error_ = QStringLiteral("训练集为空");
        else
            error_ = QStringLiteral("训练记录和参数不满足声明约束");
        return false;
    }
    error_.clear();
    return true;
}

} // namespace visionrt
