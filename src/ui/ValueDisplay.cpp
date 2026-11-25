#include "ui/ValueDisplay.h"

#include "core/ValueJson.h"

#include <QJsonArray>
#include <QJsonDocument>
#include <QStringList>

namespace visionrt {
namespace {
QString numberText(double number)
{
    auto text = QString::number(number, 'f', kValueDisplayDecimals);
    while (text.endsWith('0')) text.chop(1);
    if (text.endsWith('.')) text.chop(1);
    return text == QStringLiteral("-0") ? QStringLiteral("0") : text;
}

QString floatingJsonText(const QJsonValue &value)
{
    if (value.isDouble()) return numberText(value.toDouble());
    if (value.isArray())
    {
        QStringList elements;
        for (const auto &element : value.toArray()) elements.append(floatingJsonText(element));
        return '[' + elements.join(',') + ']';
    }
    const auto encoded = QJsonDocument(QJsonArray{value}).toJson(QJsonDocument::Compact);
    return QString::fromUtf8(encoded.mid(1, encoded.size() - 2));
}
} // namespace

QString displayValueText(const PortValue &value, const TypeSpec &type)
{
    if (isShapeType(type.type))
        return floatingJsonText(ValueJson::encode(value, type, ValueJson::Purpose::Display));
    const auto *array = std::get_if<Array>(&value);
    if (std::holds_alternative<Direction>(value) || std::holds_alternative<CoordinateFrame2D>(value))
        return floatingJsonText(ValueJson::encode(value, type, ValueJson::Purpose::Display));
    if (std::holds_alternative<double>(value)
        || (array && (array->dtype() == DType::Float32 || array->dtype() == DType::Float64)))
        return floatingJsonText(ValueJson::encode(value, type, ValueJson::Purpose::Display));
    return ValueJson::text(value, type, ValueJson::Purpose::Display);
}
} // namespace visionrt
