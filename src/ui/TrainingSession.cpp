#include "ui/TrainingSession.h"

#include "core/ToolPaths.h"

#include <QFile>
#include <QImageReader>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSaveFile>
#include <QUrl>
#include <QUuid>

#include "visionrt/contracts/ROI.h"

#include <algorithm>
#include <cmath>

namespace visionrt {
namespace {

QString utf8Text(const std::filesystem::path &path)
{
    const auto text = path.generic_u8string();
    return QString::fromUtf8(reinterpret_cast<const char *>(text.data()), static_cast<qsizetype>(text.size()));
}

std::filesystem::path pathFromText(const QString &text)
{
    return std::filesystem::path(text.toStdWString());
}

// Stored paths stay relative while the asset lives under the installation, so a project copied to
// another machine keeps its own training data and an author never sees a machine path.
QString storedPath(const std::filesystem::path &path)
{
    std::error_code    error;
    const auto         root     = softwareDataDir();
    const auto         relative = std::filesystem::relative(path, root, error);
    if (!error && !relative.empty() && relative.begin() != relative.end() && *relative.begin() != "..")
        return utf8Text(relative);
    return utf8Text(path);
}

std::filesystem::path resolvedPath(const QString &text)
{
    const auto path = pathFromText(text);
    return path.is_absolute() ? path : softwareDataDir() / path;
}

bool rectangleOf(const EncodedRegion &region, double (&values)[5])
{
    try
    {
        const RegionView view(region);
        if (view.kind() != RoiKind::Rectangle)
            return false;
        const auto rectangle = view.rectangle();
        const auto center    = rectangle.center();
        values[0] = center.x;
        values[1] = center.y;
        values[2] = rectangle.width();
        values[3] = rectangle.height();
        values[4] = rectangle.angle();
        return true;
    }
    catch (const ContractError &)
    {
        return false;
    }
}

QString nextRecordId()
{
    return QUuid::createUuid().toString(QUuid::WithoutBraces);
}



bool readRegions(const QJsonArray &values, std::vector<EncodedRegion> &regions)
{
    for (const auto &region : values)
    {
        const auto coordinates = region.toArray();
        if (coordinates.size() != 5)
            return false;
        const double centerX = coordinates[0].toDouble();
        const double centerY = coordinates[1].toDouble();
        const double width   = coordinates[2].toDouble();
        const double height  = coordinates[3].toDouble();
        const double angle   = coordinates[4].toDouble();
        if (!(width > 0.0) || !(height > 0.0))
            return false;
        regions.push_back(makeRectangle(centerX - width / 2.0, centerY - height / 2.0, width, height, angle));
    }
    return true;
}

// A reference edge is stored as its endpoints and the band searched across it. The rectangle on
// screen is the same geometry: its long side is the edge, its short side is the search range.
bool appendSegmentRegion(const QJsonObject &segment, std::vector<EncodedRegion> &regions)
{
    for (const char *endpoint : {"from", "to"})
    {
        const auto value = segment.value(endpoint).toArray();
        if (value.size() != 2)
            return false;
    }
    const auto search = segment.value("search").toArray();
    if (search.size() != 5)
        return false;
    const double centerX = search[0].toDouble();
    const double centerY = search[1].toDouble();
    const double width   = search[2].toDouble();
    const double height  = search[3].toDouble();
    if (!(width > 0.0) || !(height > 0.0))
        return false;
    regions.push_back(makeRectangle(centerX - width / 2.0, centerY - height / 2.0, width, height,
                                    search[4].toDouble()));
    return true;
}

constexpr double kPi = 3.14159265358979323846;
} // namespace

void TrainingSession::begin(Begin start)
{
    end();
    start_  = std::move(start);
    active_ = true;
    load();
}

void TrainingSession::end()
{
    start_    = {};
    active_   = false;
    dirty_    = false;
    error_.clear();
    records_.clear();
    selected_ = -1;
}

void TrainingSession::setAsset(std::filesystem::path asset)
{
    if (asset == start_.asset)
        return;
    start_.asset = std::move(asset);
    // The records belong to the old file; a new path starts from what that file contains.
    records_.clear();
    selected_ = -1;
    dirty_    = false;
    error_.clear();
    load();
}

int TrainingSession::regionBudget() const noexcept
{
    return start_.kind == TrainingKind::StraightEdges ? 2 : 0;
}

bool TrainingSession::select(int index)
{
    if (index < -1 || index >= static_cast<int>(records_.size()) || index == selected_)
        return false;
    selected_ = index;
    return true;
}

void TrainingSession::load()
{
    if (start_.asset.empty() || !std::filesystem::is_regular_file(start_.asset))
        return;
    QFile file(QString::fromStdWString(start_.asset.wstring()));
    if (!file.open(QIODevice::ReadOnly))
    {
        error_ = QStringLiteral("无法读取训练数据：%1").arg(file.errorString());
        return;
    }
    QJsonParseError parseError;
    const auto      document = QJsonDocument::fromJson(file.readAll(), &parseError);
    if (parseError.error != QJsonParseError::NoError || !document.isObject())
    {
        error_ = QStringLiteral("训练数据不是有效的 JSON：%1").arg(parseError.errorString());
        return;
    }
    const auto root = document.object();
    if (root.value("schema_version").toInt(0) != kTrainingAssetVersion
        || root.value("kind").toInt(-1) != static_cast<int>(start_.kind))
    {
        error_ = QStringLiteral("训练数据格式不正确");
        return;
    }
    if (start_.kind == TrainingKind::StraightEdges)
    {
        const auto image = root.value("reference_image").toString();
        if (image.isEmpty())
        {
            error_ = QStringLiteral("训练数据缺少参考图");
            return;
        }
        TrainingRecord record;
        record.id    = nextRecordId();
        record.label = QStringLiteral("参考图");
        record.image = resolvedPath(image);
        for (const auto &value : root.value("segments").toArray())
            if (!appendSegmentRegion(value.toObject(), record.regions))
            {
                error_ = QStringLiteral("参考边数据不正确，需要端点与搜索框");
                return;
            }
        records_.clear();
        records_.push_back(std::move(record));
        selected_ = 0;
        dirty_    = false;
        return;
    }
    std::vector<TrainingRecord> records;
    for (const auto &value : root.value("records").toArray())
    {
        const auto item = value.toObject();
        TrainingRecord record;
        record.id    = item.value("id").toString();
        record.label = item.value("label").toString();
        const auto  image = item.value("image").toString();
        const auto &regions = item.value("regions").toArray();
        if (record.id.isEmpty() || image.isEmpty())
        {
            error_ = QStringLiteral("训练记录缺少编号或图片");
            return;
        }
        record.image = resolvedPath(image);
        if (!readRegions(regions, record.regions))
        {
            error_ = QStringLiteral("训练区域需要五个坐标，且宽高必须为正数");
            return;
        }
        records.push_back(std::move(record));
    }
    records_  = std::move(records);
    selected_ = records_.empty() ? -1 : 0;
    dirty_    = false;
}

std::optional<int> TrainingSession::addImage(const QImage &image)
{
    if (!active_)
        return std::nullopt;
    if (image.isNull())
    {
        error_ = QStringLiteral("当前没有可加入训练的图像");
        return std::nullopt;
    }
    if (start_.asset.empty())
    {
        error_ = QStringLiteral("训练数据路径未配置");
        return std::nullopt;
    }
    const auto imageDirectory
        = start_.asset.parent_path() / (start_.asset.stem().wstring() + L"-images");
    std::error_code error;
    std::filesystem::create_directories(imageDirectory, error);
    if (error)
    {
        error_ = QStringLiteral("无法创建训练图片目录：%1").arg(QString::fromStdString(error.message()));
        return std::nullopt;
    }
    const auto name = QStringLiteral("record-%1.png").arg(QUuid::createUuid().toString(QUuid::WithoutBraces));
    const auto file = imageDirectory / name.toStdWString();
    if (!image.save(QString::fromStdWString(file.wstring()), "PNG"))
    {
        error_ = QStringLiteral("无法写入训练图片");
        return std::nullopt;
    }
    TrainingRecord record;
    record.id    = nextRecordId();
    record.label = QStringLiteral("颜色%1").arg(records_.size() + 1);
    record.image = file;
    if (start_.kind == TrainingKind::StraightEdges)
    {
        // A straight-edge model names one reference picture. Replacing it keeps the trained edges
        // only while the new picture has the same geometry; otherwise the rectangles would no
        // longer describe the part and the user has to draw them again.
        record.label = QStringLiteral("参考图");
        if (records_.size() == 1
            && QImageReader(QString::fromStdWString(records_.front().image.wstring())).size() == image.size())
            record.regions = records_.front().regions;
        records_.clear();
    }
    records_.push_back(std::move(record));
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
    selected_ = records_.empty() ? -1 : std::min(index, static_cast<int>(records_.size()) - 1);
    dirty_    = true;
    return true;
}

bool TrainingSession::setLabel(int index, const QString &label)
{
    if (index < 0 || index >= static_cast<int>(records_.size()))
        return false;
    if (records_[index].label == label)
        return false;
    records_[index].label = label;
    dirty_                = true;
    return true;
}

bool TrainingSession::setRegions(int index, std::vector<EncodedRegion> regions)
{
    if (index < 0 || index >= static_cast<int>(records_.size()))
        return false;
    records_[index].regions = std::move(regions);
    dirty_                  = true;
    return true;
}

QVariantList TrainingSession::rows() const
{
    QVariantList result;
    for (std::size_t index = 0; index < records_.size(); ++index)
    {
        const auto &record = records_[index];
        const auto  path   = QString::fromStdWString(record.image.wstring());
        result.append(QVariantMap{{"id", record.id},
                                  {"label", record.label},
                                  {"image", path},
                                  {"imageUrl", QUrl::fromLocalFile(path).toString()},
                                  {"regions", static_cast<int>(record.regions.size())},
                                  {"selected", static_cast<int>(index) == selected_}});
    }
    return result;
}

bool TrainingSession::savable() const
{
    if (records_.empty())
        return false;
    if (start_.kind == TrainingKind::StraightEdges)
    {
        // The tool needs exactly two non-parallel edges; a band thinner than a pixel carries no
        // gradient and a short edge carries too few samples to fit a line.
        if (records_.size() != 1 || records_.front().regions.size() != 2)
            return false;
        return std::all_of(records_.front().regions.begin(), records_.front().regions.end(),
                           [](const EncodedRegion &region)
                           {
                               double values[5]{};
                               if (!rectangleOf(region, values))
                                   return false;
                               const double longSide  = std::max(values[2], values[3]);
                               const double shortSide = std::min(values[2], values[3]);
                               return longSide >= 8.0 && shortSide >= 3.0;
                           });
    }
    return std::all_of(records_.begin(), records_.end(), [](const TrainingRecord &record)
                       { return !record.label.trimmed().isEmpty() && !record.regions.empty(); });
}

bool TrainingSession::save()
{
    if (!active_)
        return false;
    if (start_.asset.empty())
    {
        error_ = QStringLiteral("训练数据路径未配置");
        return false;
    }
    if (!savable())
    {
        if (records_.empty())
            error_ = QStringLiteral("训练集为空");
        else if (start_.kind == TrainingKind::StraightEdges)
            error_ = QStringLiteral("直边训练需要参考图上的两个矩形：长边是参考边，短边是搜索范围");
        else
            error_ = QStringLiteral("每条训练记录都需要颜色名称和至少一个矩形");
        return false;
    }

    QJsonObject root{{"schema_version", kTrainingAssetVersion}, {"kind", static_cast<int>(start_.kind)}};
    if (start_.kind == TrainingKind::StraightEdges)
    {
        QJsonArray segments;
        for (const auto &region : records_.front().regions)
        {
            double values[5]{};
            if (!rectangleOf(region, values))
            {
                error_ = QStringLiteral("训练区域必须是矩形");
                return false;
            }
            const bool   alongWidth = values[2] >= values[3];
            const double length     = alongWidth ? values[2] : values[3];
            const double band       = alongWidth ? values[3] : values[2];
            const double angle      = std::remainder(alongWidth ? values[4] : values[4] + 90.0, 360.0);
            const double radians    = angle * kPi / 180.0;
            const double halfX      = std::cos(radians) * length / 2.0;
            const double halfY      = std::sin(radians) * length / 2.0;
            segments.append(QJsonObject{
                {"name", QStringLiteral("参考边%1").arg(segments.size() + 1)},
                {"from", QJsonArray{values[0] - halfX, values[1] - halfY}},
                {"to", QJsonArray{values[0] + halfX, values[1] + halfY}},
                {"search", QJsonArray{values[0], values[1], length, band, angle}}});
        }
        root.insert("reference_image", storedPath(records_.front().image));
        root.insert("segments", segments);
    }
    else
    {
        QJsonArray records;
        for (const auto &record : records_)
        {
            QJsonArray regions;
            for (const auto &region : record.regions)
            {
                double values[5]{};
                if (!rectangleOf(region, values))
                {
                    error_ = QStringLiteral("训练区域必须是矩形");
                    return false;
                }
                regions.append(QJsonArray{values[0], values[1], values[2], values[3], values[4]});
            }
            records.append(QJsonObject{{"id", record.id},
                                       {"label", record.label},
                                       {"image", storedPath(record.image)},
                                       {"regions", regions}});
        }
        root.insert("records", records);
    }

    std::error_code error;
    std::filesystem::create_directories(start_.asset.parent_path(), error);
    QSaveFile file(QString::fromStdWString(start_.asset.wstring()));
    if (!file.open(QIODevice::WriteOnly))
    {
        error_ = QStringLiteral("无法写入训练数据：%1").arg(file.errorString());
        return false;
    }
    const auto content = QJsonDocument(root).toJson(QJsonDocument::Indented);
    if (file.write(content) != content.size() || !file.commit())
    {
        error_ = QStringLiteral("无法提交训练数据：%1").arg(file.errorString());
        return false;
    }
    dirty_ = false;
    error_.clear();
    return true;
}

} // namespace visionrt
