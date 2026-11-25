#pragma once

#include "core/Export.h"

#include <visionrt/contracts/Value.h>
#include <visionrt/contracts/ValueJson.h>

#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonValue>
#include <QString>
#include <QStringList>

namespace visionrt {
class VISIONRT_CORE_API ValueJson
{
public:
    enum class Purpose
    {
        Literal,
        Display,
        // Project persistence keeps the complete structure (shape or ragged offsets), so empty
        // trailing dimensions survive a round trip. Declaration defaults stay in the readable
        // nested form.
        Stored
    };

    class VISIONRT_CORE_API Document
    {
    public:
        explicit Document(const QByteArray &bytes);

        const QJsonDocument &json() const
        {
            return values_;
        }

        PortValue decode(const QStringList &path, const TypeSpec &type) const;

    private:
        QJsonDocument values_;
        ValueJsonDocument typed_;
    };

    static TypeSpec    decodeType(const QJsonObject &object);
    static QJsonObject encodeType(const TypeSpec &type);
    static PortValue   decode(const QJsonValue &json, const TypeSpec &type);
    static QJsonValue  encode(const PortValue &value, const TypeSpec &type, Purpose purpose = Purpose::Literal);
    static PortValue   parse(const QString &text, const TypeSpec &type);
    static QString     text(const PortValue &value, const TypeSpec &type, Purpose purpose = Purpose::Literal);
};
}
