#include "tools/CpuTools.h"

#include "core/ToolRegistry.h"

#include <opencv2/imgcodecs.hpp>
#include <opencv2/imgproc.hpp>

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <algorithm>
#include <cmath>
#include <limits>

namespace visionrt {
namespace {
class Inputs
{
public:
    Inputs(const Values &values, const ToolDefinition &definition)
        : values_(values)
        , definition_(definition)
    {
    }

    const PortValue &value(const std::string &id) const
    {
        if (const auto found = values_.find(id); found != values_.end())
            return found->second;
        for (const auto &port : definition_.inputs)
            if (port.id == id)
                return port.defaultValue;
        throw ContractError("Unknown input: " + id);
    }

    template<class T>
    const T &get(const std::string &id) const
    {
        const auto *result = std::get_if<T>(&value(id));
        if (!result)
            throw ContractError("Missing or invalid input: " + id);
        return *result;
    }

    std::int64_t integer(const std::string &id, std::int64_t minimum,
                         std::int64_t maximum = std::numeric_limits<std::int64_t>::max()) const
    {
        const auto result = get<std::int64_t>(id);
        if (result < minimum || result > maximum)
            throw ContractError("Invalid input: " + id);
        return result;
    }

    const std::string &text(const std::string &id) const
    {
        return get<Utf8>(id).str();
    }

private:
    const Values         &values_;
    const ToolDefinition &definition_;
};

void requireU8(const OwnedImage &image)
{
    if (image.empty() || CV_MAT_DEPTH(image.type()) != CV_8U
        || (image.channels() != 1 && image.channels() != 3 && image.channels() != 4))
        throw ContractError("Expected nonempty uint8 gray/BGR/BGRA image");
}

cv::Mat gray(const OwnedImage &image)
{
    requireU8(image);
    if (image.channels() == 1)
        return image.readOnlyMat();
    cv::Mat result;
    cv::cvtColor(image.readOnlyMat(), result, image.channels() == 3 ? cv::COLOR_BGR2GRAY : cv::COLOR_BGRA2GRAY);
    return result;
}

template<class T>
NumericBuffer buffer(DType dtype, std::vector<std::int64_t> shape, const std::vector<T> &values)
{
    return NumericBuffer::copy(dtype, std::move(shape), std::as_bytes(std::span(values)));
}

Values readImages(const Inputs &inputs, const ToolServices &services)
{
    const auto &mode  = inputs.text("mode");
    const auto &color = inputs.text("color");
    const auto  limit = inputs.integer("limit", 0);
    int         flags;
    if (color == "unchanged")
        flags = cv::IMREAD_UNCHANGED;
    else if (color == "bgr")
        flags = cv::IMREAD_COLOR;
    else if (color == "gray")
        flags = cv::IMREAD_GRAYSCALE;
    else
        throw ContractError("Invalid color: " + color);
    if (inputs.text("path").empty())
        throw ContractError("Image path is empty");
    const auto    resolved = services.paths.resolve(inputs.text("path")).u8string();
    const QString path     = QDir::fromNativeSeparators(
        QString::fromUtf8(reinterpret_cast<const char *>(resolved.data()), static_cast<qsizetype>(resolved.size())));
    QStringList files;
    if (mode == "file")
        files.push_back(path);
    else if (mode == "directory")
    {
        const QFileInfo info(path);
        if (!info.exists() || !info.isDir() || !info.isReadable())
            throw ContractError("Cannot read directory: " + path.toUtf8().toStdString());
        QDir              directory(path);
        const QStringList extensions{"png", "jpg", "jpeg", "bmp", "tif", "tiff"};
        const auto        entries = directory.entryInfoList(QDir::Files | QDir::Hidden | QDir::System, QDir::NoSort);

        struct Candidate
        {
            QFileInfo      file;
            std::u32string normalizedName;
            std::u32string originalName;
        };

        std::vector<Candidate> candidates;
        candidates.reserve(entries.size());
        for (const auto &entry : entries)
        {
            if (!extensions.contains(entry.suffix().toLower()))
                continue;
            const auto name = entry.fileName();
            candidates.push_back(
                {entry, name.normalized(QString::NormalizationForm_C).toStdU32String(), name.toStdU32String()});
        }
        std::sort(candidates.begin(), candidates.end(),
                  [](const Candidate &a, const Candidate &b)
                  {
                      const int comparison = a.normalizedName.compare(b.normalizedName);
                      return comparison == 0 ? a.originalName < b.originalName : comparison < 0;
                  });
        for (const auto &candidate : candidates)
        {
            files.push_back(candidate.file.absoluteFilePath());
            if (limit != 0 && files.size() >= limit)
                break;
        }
    }
    else
        throw ContractError("Invalid mode: " + mode);
    std::vector<OwnedImage>  images;
    std::vector<std::string> paths;
    images.reserve(files.size());
    paths.reserve(files.size());
    for (const auto &filePath : files)
    {
        QFile      file(filePath);
        const auto name = filePath.toUtf8().toStdString();
        if (!file.open(QIODevice::ReadOnly))
            throw ContractError("Cannot read image: " + name);
        const QByteArray bytes = file.readAll();
        if (file.error() != QFileDevice::NoError || bytes.isEmpty() || bytes.size() > std::numeric_limits<int>::max())
            throw ContractError("Cannot read image bytes: " + name);
        cv::Mat decoded;
        try
        {
            const cv::Mat encoded(1, static_cast<int>(bytes.size()), CV_8UC1, const_cast<char *>(bytes.constData()));
            decoded = cv::imdecode(encoded, flags);
        }
        catch (const cv::Exception &error)
        {
            throw ContractError("Cannot decode image: " + name + ": " + error.what());
        }
        if (decoded.empty())
            throw ContractError("Cannot decode image: " + name);
        images.push_back(OwnedImage::take(std::move(decoded)));
        paths.push_back(name);
    }
    const auto count = static_cast<std::int64_t>(paths.size());
    return {
        {"images", ImageBatch(std::move(images))},
        {"paths", StringArray(1, Layout::Dense, {StringBuffer({count}, std::move(paths))})}
    };
}

Values projection(const Inputs &inputs)
{
    const auto &images   = inputs.get<ImageBatch>("images");
    const auto &axis     = inputs.text("axis");
    const auto &polarity = inputs.text("polarity");
    if (axis != "x" && axis != "y")
        throw ContractError("Invalid projection axis");
    if (polarity != "bright" && polarity != "dark")
        throw ContractError("Invalid polarity");
    const bool                    horizontal    = axis == "x";
    const bool                    bright        = polarity == "bright";
    const auto                    threshold     = inputs.integer("threshold", 0, 255);
    const auto                    minimumPixels = inputs.integer("min_pixels", 1);
    const auto                    minimumExtent = inputs.integer("min_extent", 1);
    const auto                    gap           = inputs.integer("merge_gap", 0);
    const auto                    padding       = inputs.integer("padding", 0);
    std::vector<std::vector<Roi>> output;
    output.reserve(images.images().size());
    for (const auto &image : images.images())
    {
        const auto                grayscale = gray(image);
        const int                 length    = horizontal ? image.cols() : image.rows();
        std::vector<std::int64_t> counts(length, 0);
        for (int y = 0; y < image.rows(); ++y)
        {
            const auto *row = grayscale.ptr<unsigned char>(y);
            for (int x = 0; x < image.cols(); ++x)
                if (bright ? row[x] >= threshold : row[x] < threshold)
                    ++counts[horizontal ? x : y];
        }
        std::vector<Roi> rois;
        const auto       append = [&](int start, int end)
        {
            if (end - start < minimumExtent)
                return;
            const int left  = static_cast<int>(start - std::min<std::int64_t>(padding, start));
            const int right = static_cast<int>(end + std::min<std::int64_t>(padding, length - end));
            rois.push_back(horizontal ? Roi::rectangle(static_cast<float>(left), 0, static_cast<float>(right - left),
                                                       static_cast<float>(image.rows()), 0)
                                      : Roi::rectangle(0, static_cast<float>(left), static_cast<float>(image.cols()),
                                                       static_cast<float>(right - left), 0));
        };
        int start = -1;
        int end   = -1;
        for (int index = 0; index < length; ++index)
        {
            if (counts[index] < minimumPixels)
                continue;
            if (start < 0)
                start = index;
            else if (index - end > gap)
            {
                append(start, end);
                start = index;
            }
            end = index + 1;
        }
        if (start >= 0)
            append(start, end);
        output.push_back(std::move(rois));
    }
    return {
        {"images",                      images},
        {  "rois", RoiBatch(std::move(output))}
    };
}

using Operation = Values (*)(const Inputs &, const ToolServices &);

bool insidePolygon(std::span<const cv::Point2d> vertices, double x, double y)
{
    bool inside = false;
    for (std::size_t i = 0, previous = vertices.size() - 1; i < vertices.size(); previous = i++)
    {
        const auto  &a         = vertices[previous];
        const auto  &b         = vertices[i];
        const double cross     = (x - a.x) * (b.y - a.y) - (y - a.y) * (b.x - a.x);
        const double tolerance = 32 * std::numeric_limits<double>::epsilon()
                               * std::max(1.0, std::abs((x - a.x) * (b.y - a.y)) + std::abs((y - a.y) * (b.x - a.x)));
        if (std::abs(cross) <= tolerance && x >= std::min(a.x, b.x) && x <= std::max(a.x, b.x)
            && y >= std::min(a.y, b.y) && y <= std::max(a.y, b.y))
            return true;
        if ((a.y > y) != (b.y > y) && x < (b.x - a.x) * (y - a.y) / (b.y - a.y) + a.x)
            inside = !inside;
    }
    return inside;
}

Values crop(const Inputs &inputs)
{
    const auto &images = inputs.get<ImageBatch>("images");
    const auto &rois   = inputs.get<RoiBatch>("rois");
    if (images.images().size() != rois.images().size())
        throw ContractError("Crop image/ROI batch lengths differ");
    std::vector<OwnedImage>   output;
    std::vector<std::int64_t> indices;
    std::vector<double>       origins;
    for (std::size_t b = 0; b < images.images().size(); ++b)
    {
        const auto &image = images.images()[b];
        for (std::size_t n = 0; n < rois.images()[b].size(); ++n)
        {
            const auto       &roi     = rois.images()[b][n];
            const std::string context = "Crop b=" + std::to_string(b) + " n=" + std::to_string(n) + ": ";
            if (roi.kind() == RoiKind::Segment || roi.kind() == RoiKind::PointSet)
                throw ContractError(context + "line and point-set ROI have no supported area");
            const auto                   encoded = roi.encoded();
            std::array<cv::Point2d, 4>   corners;
            std::vector<cv::Point2d>     polygon;
            std::span<const cv::Point2d> vertices;
            double                       minimumX, minimumY, maximumX, maximumY;
            if (roi.kind() == RoiKind::Circle)
            {
                minimumX = static_cast<double>(encoded[1]) - encoded[3];
                minimumY = static_cast<double>(encoded[2]) - encoded[3];
                maximumX = static_cast<double>(encoded[1]) + encoded[3];
                maximumY = static_cast<double>(encoded[2]) + encoded[3];
            }
            else
            {
                if (roi.kind() == RoiKind::Rectangle)
                {
                    corners  = roi.rectangleCorners();
                    vertices = corners;
                }
                else
                {
                    polygon.reserve((encoded.size() - 1) / 2);
                    for (std::size_t i = 1; i < encoded.size(); i += 2)
                        polygon.emplace_back(encoded[i], encoded[i + 1]);
                    vertices = polygon;
                }
                minimumX = maximumX = vertices[0].x;
                minimumY = maximumY = vertices[0].y;
                for (const auto &vertex : vertices)
                {
                    minimumX = std::min(minimumX, vertex.x);
                    minimumY = std::min(minimumY, vertex.y);
                    maximumX = std::max(maximumX, vertex.x);
                    maximumY = std::max(maximumY, vertex.y);
                }
            }
            // Remove only floating-point rotation roundoff at integer boundaries.
            const auto snap = [](double value)
            {
                const double nearest = std::round(value);
                return std::abs(value - nearest)
                            <= 32 * std::numeric_limits<double>::epsilon() * std::max(1.0, std::abs(value))
                         ? nearest
                         : value;
            };
            const int left
                = static_cast<int>(std::clamp(std::floor(snap(minimumX)), 0.0, static_cast<double>(image.cols())));
            const int top
                = static_cast<int>(std::clamp(std::floor(snap(minimumY)), 0.0, static_cast<double>(image.rows())));
            const int right
                = static_cast<int>(std::clamp(std::ceil(snap(maximumX)), 0.0, static_cast<double>(image.cols())));
            const int bottom
                = static_cast<int>(std::clamp(std::ceil(snap(maximumY)), 0.0, static_cast<double>(image.rows())));
            if (left >= right || top >= bottom)
                throw ContractError(context + "ROI is outside the image");
            const cv::Rect bounds(left, top, right - left, bottom - top);
            cv::Mat        mask(bounds.height, bounds.width, CV_8UC1, cv::Scalar(0));
            bool           any = false;
            for (int y = 0; y < bounds.height; ++y)
            {
                auto *row = mask.ptr<unsigned char>(y);
                for (int x = 0; x < bounds.width; ++x)
                {
                    const double centerX = left + x + 0.5;
                    const double centerY = top + y + 0.5;
                    bool         covered;
                    if (roi.kind() == RoiKind::Circle)
                    {
                        const double dx = centerX - encoded[1];
                        const double dy = centerY - encoded[2];
                        covered         = dx * dx + dy * dy <= static_cast<double>(encoded[3]) * encoded[3];
                    }
                    else
                        covered = insidePolygon(vertices, centerX, centerY);
                    if (covered)
                    {
                        row[x] = 255;
                        any    = true;
                    }
                }
            }
            if (!any)
                throw ContractError(context + "ROI has no valid pixel centers");
            cv::Mat cropped;
            image.readOnlyMat()(bounds).copyTo(cropped, mask);
            output.push_back(OwnedImage::take(std::move(cropped)));
            indices.push_back(static_cast<std::int64_t>(b));
            indices.push_back(static_cast<std::int64_t>(n));
            origins.push_back(left);
            origins.push_back(top);
        }
    }
    const auto count = static_cast<std::int64_t>(output.size());
    return {
        {"images", ImageBatch(std::move(output))},
        {"source_indices", NumericArray(DType::Int64, 2, Layout::Dense, {buffer(DType::Int64, {count, 2}, indices)})},
        {"origins", NumericArray(DType::Float64, 2, Layout::Dense, {buffer(DType::Float64, {count, 2}, origins)})}
    };
}

Values preprocess(const Inputs &inputs)
{
    const auto &images    = inputs.get<ImageBatch>("images");
    const auto &operation = inputs.text("operation");
    if (operation != "identity" && operation != "gray" && operation != "gaussian" && operation != "threshold")
        throw ContractError("Invalid preprocessing operation: " + operation);
    int          kernel    = 0;
    std::int64_t threshold = 0;
    if (operation == "gaussian")
    {
        kernel = static_cast<int>(inputs.integer("kernel", 1, std::numeric_limits<int>::max()));
        if (kernel % 2 == 0)
            throw ContractError("Gaussian kernel must be positive and odd");
    }
    if (operation == "threshold")
        threshold = inputs.integer("threshold", 0, 255);
    for (const auto &image : images.images()) requireU8(image);
    if (operation == "identity")
        return {
            {"images", images}
        };
    std::vector<OwnedImage> output;
    output.reserve(images.images().size());
    for (const auto &image : images.images())
    {
        cv::Mat processed;
        if (operation == "gaussian")
        {
            cv::GaussianBlur(image.readOnlyMat(), processed, cv::Size(kernel, kernel), 0, 0, cv::BORDER_REFLECT_101);
        }
        else if (operation == "threshold")
        {
            cv::compare(gray(image), cv::Scalar(threshold), processed, cv::CMP_GE);
        }
        else
        {
            processed = image.channels() == 1 ? image.clone() : gray(image);
        }
        output.push_back(OwnedImage::take(std::move(processed)));
    }
    return {
        {"images", ImageBatch(std::move(output))}
    };
}

Values filter(const Inputs &inputs)
{
    const auto &images       = inputs.get<ImageBatch>("images");
    const auto &rois         = inputs.get<RoiBatch>("rois");
    const auto &scores       = inputs.get<NumericArray>("scores");
    const auto &classes      = inputs.get<NumericArray>("classes");
    const auto &names        = inputs.get<StringArray>("class_names");
    const auto &allowedArray = inputs.get<NumericArray>("allowed_classes");
    const auto  minimumScore = inputs.get<double>("min_score");
    const auto  minimumArea  = inputs.get<double>("min_area");
    if (!std::isfinite(minimumScore) || minimumScore < 0 || minimumScore > 1 || !std::isfinite(minimumArea)
        || minimumArea < 0)
        throw ContractError("Invalid filter score/area threshold");
    const auto allowed   = allowedArray.buffers()[0].values<std::int64_t>();
    const auto batchSize = images.images().size();
    if (rois.images().size() != batchSize || scores.buffers().size() != batchSize
        || classes.buffers().size() != batchSize || names.buffers().size() != batchSize)
        throw ContractError("Detection batch lengths differ");
    std::vector<std::vector<Roi>> filteredRois;
    std::vector<NumericBuffer>    filteredScores, filteredClasses;
    std::vector<StringBuffer>     filteredNames;
    std::vector<std::int64_t>     counts;
    filteredRois.reserve(batchSize);
    filteredScores.reserve(batchSize);
    filteredClasses.reserve(batchSize);
    filteredNames.reserve(batchSize);
    counts.reserve(batchSize);
    for (std::size_t b = 0; b < batchSize; ++b)
    {
        const auto &imageRois    = rois.images()[b];
        const auto  imageScores  = scores.buffers()[b].values<float>();
        const auto  imageClasses = classes.buffers()[b].values<std::int64_t>();
        const auto  imageNames   = names.buffers()[b].values();
        if (imageScores.size() != imageRois.size() || imageClasses.size() != imageRois.size()
            || imageNames.size() != imageRois.size())
            throw ContractError("Detection field lengths differ at b=" + std::to_string(b));
        std::vector<Roi>          keptRois;
        std::vector<float>        keptScores;
        std::vector<std::int64_t> keptClasses;
        std::vector<std::string>  keptNames;
        keptRois.reserve(imageRois.size());
        keptScores.reserve(imageRois.size());
        keptClasses.reserve(imageRois.size());
        keptNames.reserve(imageRois.size());
        for (std::size_t n = 0; n < imageRois.size(); ++n)
        {
            if (!std::isfinite(imageScores[n]) || imageClasses[n] < 0)
                throw ContractError("Invalid detection score/class at b=" + std::to_string(b)
                                    + " n=" + std::to_string(n));
            if (imageRois[n].kind() != RoiKind::Rectangle)
                throw ContractError("Detection ROI must be a rectangle at b=" + std::to_string(b)
                                    + " n=" + std::to_string(n));
            const auto   encoded = imageRois[n].encoded();
            const double area    = static_cast<double>(encoded[3]) * encoded[4];
            if (imageScores[n] >= minimumScore && area >= minimumArea
                && (allowed.empty() || std::find(allowed.begin(), allowed.end(), imageClasses[n]) != allowed.end()))
            {
                keptRois.push_back(imageRois[n]);
                keptScores.push_back(imageScores[n]);
                keptClasses.push_back(imageClasses[n]);
                keptNames.push_back(imageNames[n]);
            }
        }
        const auto count = static_cast<std::int64_t>(keptRois.size());
        counts.push_back(count);
        filteredRois.push_back(std::move(keptRois));
        filteredScores.push_back(buffer(DType::Float32, {count}, keptScores));
        filteredClasses.push_back(buffer(DType::Int64, {count}, keptClasses));
        filteredNames.emplace_back(std::vector<std::int64_t>{count}, std::move(keptNames));
    }
    return {
        {"images", images},
        {"rois", RoiBatch(std::move(filteredRois))},
        {"scores", NumericArray(DType::Float32, 2, Layout::BatchRagged, std::move(filteredScores))},
        {"classes", NumericArray(DType::Int64, 2, Layout::BatchRagged, std::move(filteredClasses))},
        {"class_names", StringArray(2, Layout::BatchRagged, std::move(filteredNames))},
        {"counts", NumericArray(DType::Int64, 1, Layout::Dense,
         {buffer(DType::Int64, {static_cast<std::int64_t>(batchSize)}, counts)})}
    };
}

class CpuTool final : public IToolInstance
{
public:
    CpuTool(std::shared_ptr<const ToolDefinition> definition, ToolServices services, Operation operation)
        : definition_(std::move(definition))
        , services_(std::move(services))
        , operation_(operation)
    {
    }

    ToolStatus initialize(const Values &parameters) override
    {
        return parameters.empty() ? ToolStatus{} : ToolStatus{1, "CPU functions have no init parameters"};
    }

    ToolResult execute(const Values &values) override
    {
        try
        {
            const Inputs inputs(values, *definition_);
            for (const auto &port : definition_->inputs)
            {
                const auto &value = inputs.value(port.id);
                if (isMissing(value))
                    throw ContractError("Missing input: " + port.id);
                validateValue(port.type, value);
            }
            return {{}, operation_(inputs, services_)};
        }
        catch (const std::exception &error)
        {
            ToolResult result{
                {1, error.what()},
                {}
            };
            for (const auto &port : definition_->outputs)
                if (port.id != "__status_code" && port.id != "__status_message")
                    result.outputs.emplace(port.id, Missing{});
            return result;
        }
    }

    ToolStatus release() override
    {
        return {};
    }

private:
    std::shared_ptr<const ToolDefinition> definition_;
    ToolServices                          services_;
    Operation                             operation_;
};
} // namespace

void registerCpuTools(ToolRegistry &registry)
{
    const std::pair<const char *, Operation> tools[]{
        {"visionrt.image.read", readImages},
        {"visionrt.roi.projection", [](const Inputs &inputs, const ToolServices &) { return projection(inputs); }},
        {"visionrt.image.crop", [](const Inputs &inputs, const ToolServices &) { return crop(inputs); }},
        {"visionrt.image.preprocess", [](const Inputs &inputs, const ToolServices &) { return preprocess(inputs); }},
        {"visionrt.detection.filter", [](const Inputs &inputs, const ToolServices &) { return filter(inputs); }}
    };
    for (const auto &[id, operation] : tools)
    {
        const auto *found = registry.find(id);
        if (!found)
            continue;
        auto definition = std::make_shared<const ToolDefinition>(*found);
        registry.registerCpp(definition->registryName, *definition,
                             [definition, operation](const ToolServices &services)
                             { return std::make_unique<CpuTool>(definition, services, operation); });
    }
}
} // namespace visionrt
