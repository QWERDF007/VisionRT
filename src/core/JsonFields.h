#pragma once

#include <visionrt/contracts/Value.h>

#include <QFile>
#include <QJsonArray>
#include <QJsonObject>
#include <algorithm>
#include <filesystem>
#include <initializer_list>

namespace visionrt::json {
inline QString pathText(const std::filesystem::path &path)
{
    const auto text = path.generic_u8string();
    return QString::fromUtf8(reinterpret_cast<const char *>(text.data()), static_cast<qsizetype>(text.size()));
}

inline QJsonObject object(const QJsonValue &value, const QString &context)
{
    if (!value.isObject())
        throw ContractError((context + " 必须是 JSON 对象").toStdString());
    return value.toObject();
}

inline QJsonArray array(const QJsonValue &value, const QString &context)
{
    if (!value.isArray())
        throw ContractError((context + " 必须是 JSON 数组").toStdString());
    return value.toArray();
}

inline std::string text(const QJsonValue &value, const QString &context, bool nonempty = true)
{
    if (!value.isString() || (nonempty && value.toString().isEmpty()))
        throw ContractError((context + (nonempty ? " 必须是非空字符串" : " 必须是字符串")).toStdString());
    return value.toString().toUtf8().toStdString();
}

inline void fields(const QJsonObject &value, std::initializer_list<const char *> allowed, const QString &context)
{
    for (auto field = value.begin(); field != value.end(); ++field)
    {
        if (std::none_of(allowed.begin(), allowed.end(), [&](const auto name) { return field.key() == name; }))
            throw ContractError((context + "." + field.key() + " 是未知字段").toStdString());
    }
}

inline std::filesystem::path relativePath(const std::string &text, const QString &context)
{
    const auto path
        = std::filesystem::path(std::u8string_view(reinterpret_cast<const char8_t *>(text.data()), text.size()));
    if (path.empty() || path.is_absolute() || path.has_root_name() || path.has_root_directory())
        throw ContractError((context + " 必须是相对路径").toStdString());
    return path.lexically_normal();
}

inline QByteArray read(const std::filesystem::path &path)
{
    QFile file(pathText(path));
    if (!file.open(QIODevice::ReadOnly))
        throw ContractError(("无法读取 " + file.fileName() + "：" + file.errorString()).toStdString());
    const auto content = file.readAll();
    if (file.error() != QFileDevice::NoError)
        throw ContractError(("读取失败 " + file.fileName() + "：" + file.errorString()).toStdString());
    return content;
}
}
