#include "core/ValueJson.h"

#include <QJsonParseError>
#include <vector>

namespace visionrt {
ValueJson::Document::Document(const QByteArray &bytes)
    : typed_(std::string_view(bytes.constData(), static_cast<std::size_t>(bytes.size())))
{
    QJsonParseError error;
    values_ = QJsonDocument::fromJson(bytes, &error);
    if (error.error != QJsonParseError::NoError)
        throw ContractError("需要标准 JSON 文档：" + error.errorString().toStdString());
}

PortValue ValueJson::Document::decode(const QStringList &path, const TypeSpec &type) const
{
    std::vector<std::string> segments;
    segments.reserve(static_cast<std::size_t>(path.size()));
    for (const auto &segment : path) segments.push_back(segment.toStdString());
    return typed_.read(segments, type);
}
}
