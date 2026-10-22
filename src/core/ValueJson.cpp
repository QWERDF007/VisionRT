#include "core/ValueJson.h"

#include <QJsonArray>
#include <QJsonDocument>
#include <cmath>
#include <limits>

namespace visionrt {
namespace {
QString utf8(std::string_view value)
{
    return QString::fromUtf8(value.data(), static_cast<qsizetype>(value.size()));
}

std::int64_t integer(const QJsonValue &json)
{
    const auto value = json.toInteger(std::numeric_limits<qint64>::min());
    if (!json.isDouble() || value != json.toInteger(std::numeric_limits<qint64>::max()))
        throw ContractError("JSON 值必须是 int64 范围内的整数");
    return value;
}

std::string string(const QJsonValue &json)
{
    if (!json.isString())
        throw ContractError("JSON 值必须是字符串");
    return json.toString().toUtf8().toStdString();
}

QJsonArray array(const QJsonValue &json)
{
    if (!json.isArray())
        throw ContractError("JSON 值必须是数组");
    return json.toArray();
}

template<class Element>
Element number(const QJsonValue &json)
{
    if constexpr (std::is_same_v<Element, bool>)
    {
        if (!json.isBool())
            throw ContractError("JSON 值必须是布尔值");
        return json.toBool();
    }
    else if constexpr (std::is_integral_v<Element>)
    {
        const auto value = integer(json);
        if (value < std::numeric_limits<Element>::min() || value > std::numeric_limits<Element>::max())
            throw ContractError("JSON 数组元素超出声明的整数范围");
        return static_cast<Element>(value);
    }
    else
    {
        if (!json.isDouble() || !std::isfinite(json.toDouble()))
            throw ContractError("JSON 值必须是有限数值");
        const auto value = static_cast<Element>(json.toDouble());
        if (!std::isfinite(value))
            throw ContractError("JSON 数值超出声明的浮点范围");
        return value;
    }
}

std::vector<std::int64_t> shape(const QJsonObject &buffer, int rank)
{
    for (auto field = buffer.begin(); field != buffer.end(); ++field)
        if (field.key() != "shape" && field.key() != "data")
            throw ContractError("JSON 数组缓冲区包含未知字段：" + field.key().toStdString());
    const auto dimensions = array(buffer.value("shape"));
    const auto data = array(buffer.value("data"));
    if (dimensions.size() != rank)
        throw ContractError("JSON 数组形状与声明的 rank 不一致");
    std::vector<std::int64_t> result;
    result.reserve(static_cast<std::size_t>(dimensions.size()));
    for (const auto dimension : dimensions)
        result.push_back(integer(dimension));
    if (checkedElementCount(result) != static_cast<std::size_t>(data.size()))
        throw ContractError("JSON 数组数据长度与形状不一致");
    return result;
}

QJsonObject object(const QJsonValue &json)
{
    if (!json.isObject())
        throw ContractError("JSON 数组缓冲区必须是对象");
    return json.toObject();
}

template<class Element>
NumericBuffer numericBuffer(const QJsonValue &json, DType dtype, int rank)
{
    const auto buffer = object(json);
    auto dimensions = shape(buffer, rank);
    (void)checkedByteCount(dimensions, sizeof(Element));
    const auto data = array(buffer.value("data"));
    const auto count = static_cast<std::size_t>(data.size());
    auto values = std::make_unique_for_overwrite<Element[]>(count);
    for (std::size_t index = 0; index < count; ++index)
        values[index] = number<Element>(data[static_cast<qsizetype>(index)]);
    return NumericBuffer::copy(dtype, std::move(dimensions), std::as_bytes(std::span(values.get(), count)));
}

QJsonValue real(double value, const char *type, ValueJson::Purpose purpose)
{
    if (std::isfinite(value))
        return value;
    if (purpose == ValueJson::Purpose::Literal)
        throw ContractError("JSON 字面量不支持 NaN 或 Infinity");
    return QJsonObject{{"type", type}, {"non_finite", std::isnan(value) ? "NaN" : value < 0 ? "-Infinity" : "Infinity"}};
}

template<class Element>
QJsonArray elements(std::span<const Element> values, ValueJson::Purpose purpose = ValueJson::Purpose::Literal,
                    const char *type = "Float")
{
    QJsonArray result;
    for (const auto &value : values)
    {
        if constexpr (std::is_same_v<Element, bool>)
            result.append(value);
        else if constexpr (std::is_integral_v<Element>)
            result.append(static_cast<qint64>(value));
        else if constexpr (std::is_floating_point_v<Element>)
            result.append(real(value, type, purpose));
        else
            result.append(utf8(value));
    }
    return result;
}
}

TypeSpec ValueJson::decodeType(const QJsonObject &json)
{
    for (auto field = json.begin(); field != json.end(); ++field)
        if (field.key() != "type" && field.key() != "dtype" && field.key() != "rank" && field.key() != "layout")
            throw ContractError("TypeSpec 包含未知字段：" + field.key().toStdString());
    TypeSpec type;
    const auto name = string(json.value("type"));
    bool found = false;
    for (int index = 0; index <= static_cast<int>(Type::Rect); ++index)
        if (name == typeName(static_cast<Type>(index)))
        {
            type.type = static_cast<Type>(index);
            found = true;
            break;
        }
    if (!found)
        throw ContractError("未知类型：" + name);
    const bool isArray = type.type == Type::NumericArray || type.type == Type::StringArray;
    if (!isArray && (json.contains("dtype") || json.contains("rank") || json.contains("layout")))
        throw ContractError("只有数组类型可以声明 dtype、rank 和 layout");
    if (json.contains("dtype"))
    {
        const auto dtype = string(json.value("dtype"));
        found = false;
        for (int index = 0; index <= static_cast<int>(DType::Bool); ++index)
            if (dtype == dtypeName(static_cast<DType>(index)))
            {
                type.dtype = static_cast<DType>(index);
                found = true;
                break;
            }
        if (!found)
            throw ContractError("未知 dtype：" + dtype);
    }
    if (isArray)
    {
        const auto rank = integer(json.value("rank"));
        if (rank < 0 || rank > std::numeric_limits<int>::max())
            throw ContractError("数组 rank 超出整数范围");
        type.rank = static_cast<int>(rank);
        if (json.contains("layout"))
        {
            const auto layout = string(json.value("layout"));
            if (layout == "batch_ragged")
                type.layout = Layout::BatchRagged;
            else if (layout != "dense")
                throw ContractError("未知 layout：" + layout);
        }
    }
    validateTypeSpec(type);
    return type;
}

QJsonObject ValueJson::encodeType(const TypeSpec &type)
{
    validateTypeSpec(type);
    QJsonObject json{{"type", typeName(type.type)}};
    if (type.type == Type::NumericArray || type.type == Type::StringArray)
    {
        if (type.dtype)
            json.insert("dtype", dtypeName(*type.dtype));
        json.insert("rank", type.rank);
        json.insert("layout", type.layout == Layout::Dense ? "dense" : "batch_ragged");
    }
    return json;
}

PortValue ValueJson::decode(const QJsonValue &json, const TypeSpec &type)
{
    validateTypeSpec(type);
    if (json.isNull())
        return Missing{};
    if (json.isUndefined())
        throw ContractError("缺少 JSON 值；未定义字段不能代替 null");
    switch (type.type)
    {
    case Type::Int: return integer(json);
    case Type::Float: return number<double>(json);
    case Type::Bool: return number<bool>(json);
    case Type::String: return Utf8(string(json));
    case Type::ImageBatch:
        if (!array(json).isEmpty())
            throw ContractError("图像字面量只能是 null 或空数组");
        return ImageBatch{};
    case Type::NumericArray:
    {
        std::vector<NumericBuffer> buffers;
        const auto entries = type.layout == Layout::Dense ? QJsonArray{json} : array(json);
        const int rank = type.rank - (type.layout == Layout::BatchRagged ? 1 : 0);
        for (const auto entry : entries)
            switch (*type.dtype)
            {
            case DType::UInt8: buffers.push_back(numericBuffer<std::uint8_t>(entry, *type.dtype, rank)); break;
            case DType::Int32: buffers.push_back(numericBuffer<std::int32_t>(entry, *type.dtype, rank)); break;
            case DType::Int64: buffers.push_back(numericBuffer<std::int64_t>(entry, *type.dtype, rank)); break;
            case DType::Float32: buffers.push_back(numericBuffer<float>(entry, *type.dtype, rank)); break;
            case DType::Float64: buffers.push_back(numericBuffer<double>(entry, *type.dtype, rank)); break;
            case DType::Bool: buffers.push_back(numericBuffer<bool>(entry, *type.dtype, rank)); break;
            }
        return NumericArray(*type.dtype, type.rank, type.layout, std::move(buffers));
    }
    case Type::StringArray:
    {
        std::vector<StringBuffer> buffers;
        const auto entries = type.layout == Layout::Dense ? QJsonArray{json} : array(json);
        const int rank = type.rank - (type.layout == Layout::BatchRagged ? 1 : 0);
        for (const auto entry : entries)
        {
            const auto buffer = object(entry);
            auto dimensions = shape(buffer, rank);
            std::vector<std::string> values;
            for (const auto value : array(buffer.value("data")))
                values.push_back(string(value));
            buffers.emplace_back(std::move(dimensions), std::move(values));
        }
        return StringArray(type.rank, type.layout, std::move(buffers));
    }
    case Type::Rect:
    {
        std::vector<float> encoded;
        for (const auto value : array(json))
            encoded.push_back(number<float>(value));
        PortValue rectangle = Roi::decode(encoded);
        validateValue(type, rectangle);
        return rectangle;
    }
    case Type::RoiBatch:
    {
        std::vector<std::vector<Roi>> images;
        for (const auto image : array(json))
        {
            auto &rois = images.emplace_back();
            for (const auto roi : array(image))
            {
                std::vector<float> encoded;
                for (const auto value : array(roi))
                    encoded.push_back(number<float>(value));
                rois.push_back(Roi::decode(encoded));
            }
        }
        return RoiBatch(std::move(images));
    }
    }
    throw ContractError("不支持的值类型");
}

QJsonValue ValueJson::encode(const PortValue &value, const TypeSpec &type, Purpose purpose)
{
    validateValue(type, value);
    return std::visit([&](const auto &item) -> QJsonValue
    {
        using Item = std::decay_t<decltype(item)>;
        if constexpr (std::is_same_v<Item, Missing>)
            return QJsonValue(QJsonValue::Null);
        else if constexpr (std::is_same_v<Item, std::int64_t>)
            return static_cast<qint64>(item);
        else if constexpr (std::is_same_v<Item, double>)
            return real(item, "Float", purpose);
        else if constexpr (std::is_same_v<Item, bool>)
            return item;
        else if constexpr (std::is_same_v<Item, Utf8>)
            return utf8(item.str());
        else if constexpr (std::is_same_v<Item, Roi>)
            return elements(item.encoded());
        else if constexpr (std::is_same_v<Item, ImageBatch>)
        {
            if (purpose == Purpose::Literal)
            {
                if (!item.images().empty())
                    throw ContractError("不能将运行图像保存为 JSON 字面量");
                return QJsonArray{};
            }
            QJsonArray images;
            for (const auto &image : item.images())
                images.append(QJsonObject{{"rows", image.rows()}, {"cols", image.cols()}, {"channels", image.channels()}});
            return QJsonObject{{"type", "ImageBatch"}, {"count", static_cast<qint64>(images.size())}, {"images", images}};
        }
        else if constexpr (std::is_same_v<Item, RoiBatch>)
        {
            QJsonArray images;
            for (const auto &image : item.images())
            {
                QJsonArray rois;
                for (const auto &roi : image)
                    rois.append(elements(roi.encoded()));
                images.append(rois);
            }
            return images;
        }
        else
        {
            QJsonArray buffers;
            for (const auto &buffer : item.buffers())
            {
                QJsonArray data;
                if constexpr (std::is_same_v<Item, StringArray>)
                    data = elements(buffer.values());
                else
                    switch (buffer.dtype())
                    {
                    case DType::UInt8: data = elements(buffer.template values<std::uint8_t>()); break;
                    case DType::Int32: data = elements(buffer.template values<std::int32_t>()); break;
                    case DType::Int64: data = elements(buffer.template values<std::int64_t>()); break;
                    case DType::Float32: data = elements(buffer.template values<float>(), purpose, dtypeName(buffer.dtype())); break;
                    case DType::Float64: data = elements(buffer.template values<double>(), purpose, dtypeName(buffer.dtype())); break;
                    case DType::Bool: data = elements(buffer.template values<bool>()); break;
                    }
                buffers.append(QJsonObject{{"shape", elements(buffer.shape())}, {"data", data}});
            }
            return item.layout() == Layout::Dense ? QJsonValue(buffers[0]) : QJsonValue(buffers);
        }
    }, value);
}

PortValue ValueJson::parse(const QString &text, const TypeSpec &type)
{
    const Document document('[' + text.toUtf8() + ']');
    if (document.json().array().size() != 1)
        throw ContractError("需要单个标准 JSON 值");
    return document.decode({"0"}, type);
}

QString ValueJson::text(const PortValue &value, const TypeSpec &type, Purpose purpose)
{
    const auto json = encode(value, type, purpose);
    if (json.isArray())
        return QString::fromUtf8(QJsonDocument(json.toArray()).toJson(QJsonDocument::Indented)).trimmed();
    if (json.isObject())
        return QString::fromUtf8(QJsonDocument(json.toObject()).toJson(QJsonDocument::Indented)).trimmed();
    const auto wrapped = QJsonDocument(QJsonArray{json}).toJson(QJsonDocument::Compact);
    return QString::fromUtf8(wrapped.mid(1, wrapped.size() - 2));
}
}
