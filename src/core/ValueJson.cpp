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

template<class Vector>
Vector decodeNested(const QJsonValue &json)
{
    using Element      = typename Vector::value_type;
    const auto entries = array(json);
    Vector     result;
    result.reserve(static_cast<std::size_t>(entries.size()));
    for (const auto entry : entries)
    {
        if constexpr (std::is_same_v<Element, std::string>)
            result.push_back(string(entry));
        else if constexpr (std::is_arithmetic_v<Element>)
            result.push_back(number<Element>(entry));
        else
            result.push_back(decodeNested<Element>(entry));
    }
    return result;
}

QJsonValue real(double value, const char *type, ValueJson::Purpose purpose)
{
    if (std::isfinite(value))
        return value;
    if (purpose == ValueJson::Purpose::Literal)
        throw ContractError("JSON 字面量不支持 NaN 或 Infinity");
    return QJsonObject{{"type", type}, {"non_finite", std::isnan(value) ? "NaN" : value < 0 ? "-Infinity" : "Infinity"}};
}

template<class Vector>
QJsonArray encodeNested(const Vector &values, ValueJson::Purpose purpose, const char *type)
{
    using Element = typename Vector::value_type;
    QJsonArray result;
    for (const auto &value : values)
    {
        if constexpr (std::is_same_v<Element, std::string>)
            result.append(utf8(value));
        else if constexpr (std::is_same_v<Element, bool>)
            result.append(static_cast<bool>(value));
        else if constexpr (std::is_integral_v<Element>)
            result.append(static_cast<qint64>(value));
        else if constexpr (std::is_floating_point_v<Element>)
            result.append(real(value, type, purpose));
        else
            result.append(encodeNested(value, purpose, type));
    }
    return result;
}
}

TypeSpec ValueJson::decodeType(const QJsonObject &json)
{
    for (auto field = json.begin(); field != json.end(); ++field)
        if (field.key() != "type" && field.key() != "dtype" && field.key() != "rank")
            throw ContractError("TypeSpec 包含未知字段：" + field.key().toStdString());
    TypeSpec type;
    const auto name = string(json.value("type"));
    bool found = false;
    for (int index = 0; index <= static_cast<int>(Type::ROI); ++index)
        if (name == typeName(static_cast<Type>(index)))
        {
            type.type = static_cast<Type>(index);
            found = true;
            break;
        }
    if (!found)
        throw ContractError("未知类型：" + name);
    if (json.contains("dtype"))
    {
        const auto dtype = string(json.value("dtype"));
        found = false;
        for (int index = 0; index <= static_cast<int>(DType::String); ++index)
            if (dtype == dtypeName(static_cast<DType>(index)))
            {
                type.dtype = static_cast<DType>(index);
                found = true;
                break;
            }
        if (!found)
            throw ContractError("未知 dtype：" + dtype);
    }
    if (json.contains("rank"))
    {
        const auto rank = integer(json.value("rank"));
        if (rank < 1 || rank > kMaxArrayRank)
            throw ContractError("数组 rank 必须在支持范围内");
        type.rank = static_cast<int>(rank);
    }
    validateTypeSpec(type);
    return type;
}

QJsonObject ValueJson::encodeType(const TypeSpec &type)
{
    validateTypeSpec(type);
    QJsonObject json{{"type", typeName(type.type)}};
    if (type.type == Type::Array)
    {
        json.insert("dtype", dtypeName(*type.dtype));
        json.insert("rank", type.rank);
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
    PortValue result;
    switch (type.type)
    {
    case Type::Int:
        result = integer(json);
        break;
    case Type::Float:
        result = number<double>(json);
        break;
    case Type::Bool:
        result = number<bool>(json);
        break;
    case Type::String:
        result = string(json);
        break;
    case Type::Image:
        if (!array(json).isEmpty())
            throw ContractError("图像字面量只能是 null 或空数组");
        result = DataRef<Image>(Image{});
        break;
    case Type::Array:
    {
        auto values = Array::empty(*type.dtype, type.rank);
        values.visit(
            [&](const auto &reference)
            {
                using Vector    = std::remove_cvref_t<decltype(reference.get())>;
                reference.get() = decodeNested<Vector>(json);
            });
        result = std::move(values);
        break;
    }
    case Type::ROI:
        result = DataRef<ROI>(decodeNested<ROI>(json));
        break;
    }
    validateValue(type, result);
    return result;
}

QJsonValue ValueJson::encode(const PortValue &value, const TypeSpec &type, Purpose purpose)
{
    validateValue(type, value);
    return std::visit(
        [&](const auto &item) -> QJsonValue
        {
            using Item = std::decay_t<decltype(item)>;
            if constexpr (std::is_same_v<Item, Missing>)
                return QJsonValue::Null;
            else if constexpr (std::is_same_v<Item, std::int64_t>)
                return static_cast<qint64>(item);
            else if constexpr (std::is_same_v<Item, double>)
                return real(item, "Float", purpose);
            else if constexpr (std::is_same_v<Item, bool>)
                return item;
            else if constexpr (std::is_same_v<Item, std::string>)
                return utf8(item);
            else if constexpr (std::is_same_v<Item, Array>)
                return item.visit([&](const auto &reference) -> QJsonValue
                                  { return encodeNested(reference.get(), purpose, dtypeName(*type.dtype)); });
            else if constexpr (std::is_same_v<Item, DataRef<ROI>>)
                return encodeNested(item.get(), purpose, "float64");
            else
            {
                if (purpose == Purpose::Literal)
                {
                    if (!item.get().empty())
                        throw ContractError("图像字面量只能是 null 或空数组");
                    return QJsonArray{};
                }
                QJsonArray images;
                for (const auto &image : item.get())
                    images.append(QJsonObject{
                        {    "rows",       image.rows},
                        {    "cols",       image.cols},
                        {"channels", image.channels()}
                    });
                return QJsonObject{
                    {  "type",                                "Image"},
                    { "count", static_cast<qint64>(item.get().size())},
                    {"images",                                 images}
                };
            }
        },
        value);
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
    {
        const auto array = json.toArray();
        if (array.isEmpty())
            return QStringLiteral("[]");
        return QString::fromUtf8(QJsonDocument(array).toJson(QJsonDocument::Compact)).trimmed();
    }
    if (json.isObject())
    {
        const auto object = json.toObject();
        if (object.isEmpty())
            return QStringLiteral("{}");
        return QString::fromUtf8(QJsonDocument(object).toJson(QJsonDocument::Indented)).trimmed();
    }
    const auto wrapped = QJsonDocument(QJsonArray{json}).toJson(QJsonDocument::Compact);
    return QString::fromUtf8(wrapped.mid(1, wrapped.size() - 2));
}
}
