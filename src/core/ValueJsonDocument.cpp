#include "core/ValueJson.h"

#include <QJsonArray>
#include <QJsonParseError>
#include <QRegularExpression>

namespace visionrt {
namespace {
template<class Vector>
void validateIntegerLiterals(const QJsonValue &json)
{
    using Element = typename Vector::value_type;
    if constexpr (std::is_integral_v<Element> && !std::is_same_v<Element, bool>)
    {
        for (const auto value : json.toArray()) (void)parseInt64(value.toString().toStdString());
    }
    else if constexpr (!std::is_arithmetic_v<Element> && !std::is_same_v<Element, std::string>)
    {
        for (const auto value : json.toArray()) validateIntegerLiterals<Element>(value);
    }
}
}
ValueJson::Document::Document(const QByteArray &bytes)
{
    QJsonParseError error;
    values_ = QJsonDocument::fromJson(bytes, &error);
    if (error.error != QJsonParseError::NoError)
        throw ContractError("需要标准 JSON 文档，offset=" + std::to_string(error.offset) + "：" + error.errorString().toStdString());

    const auto                      text = QString::fromUtf8(bytes);
    static const QRegularExpression tokens(
        QStringLiteral(R"json("(?:[^"\\]|\\.)*"|(-?(?:0|[1-9][0-9]*)(?:\.[0-9]+)?(?:[eE][+-]?[0-9]+)?))json"));
    auto    matches = tokens.globalMatch(text);
    QString numberText;
    numberText.reserve(text.size());
    qsizetype previous = 0;
    while (matches.hasNext())
    {
        const auto match = matches.next();
        if (!match.hasCaptured(1))
            continue;
        numberText += QStringView(text).mid(previous, match.capturedStart() - previous);
        numberText += '"' + match.captured(1) + '"';
        previous = match.capturedEnd();
    }
    numberText += QStringView(text).mid(previous);
    numberText_ = QJsonDocument::fromJson(numberText.toUtf8(), &error);
    if (error.error != QJsonParseError::NoError)
        throw ContractError("无法保留 JSON 数字原文：" + error.errorString().toStdString());
}

PortValue ValueJson::Document::decode(const QStringList &path, const TypeSpec &type) const
{
    const auto locate = [&](const QJsonDocument &document)
    {
        QJsonValue current = document.isArray() ? QJsonValue(document.array()) : QJsonValue(document.object());
        for (const auto &segment : path)
        {
            if (current.isArray())
            {
                bool       valid   = false;
                const auto index   = segment.toInt(&valid);
                const auto entries = current.toArray();
                if (!valid || index < 0 || index >= entries.size())
                    throw ContractError("JSON 数组路径不存在：" + path.join('/').toStdString());
                current = entries[index];
            }
            else if (current.isObject())
                current = current.toObject().value(segment);
            else
                current = QJsonValue::Undefined;
            if (current.isUndefined())
                throw ContractError("JSON 值路径不存在：" + path.join('/').toStdString());
        }
        return current;
    };
    auto result = ValueJson::decode(locate(values_), type);
    if (isMissing(result))
        return result;
    if (type.type == Type::Int)
        (void)parseInt64(locate(numberText_).toString().toStdString());
    else if (type.type == Type::Array)
    {
        const auto source = locate(numberText_);
        std::get<Array>(result).visit(
            [&](const auto &reference)
            {
                using Vector = std::remove_cvref_t<decltype(reference.get())>;
                validateIntegerLiterals<Vector>(source);
            });
    }
    return result;
}
}
