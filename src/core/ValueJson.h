#pragma once

#include <visionrt/contracts/Value.h>

#include <QJsonObject>
#include <QJsonValue>
#include <QString>

namespace visionrt {
class ValueJson
{
public:
    enum class Purpose { Literal, Display };

    static TypeSpec decodeType(const QJsonObject &object);
    static QJsonObject encodeType(const TypeSpec &type);
    static PortValue decode(const QJsonValue &json, const TypeSpec &type);
    static QJsonValue encode(const PortValue &value, const TypeSpec &type, Purpose purpose = Purpose::Literal);
    static PortValue parse(const QString &text, const TypeSpec &type);
    static QString text(const PortValue &value, const TypeSpec &type, Purpose purpose = Purpose::Literal);
};
}
