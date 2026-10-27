#include "ui/UiPreferences.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSaveFile>
#include <QStandardPaths>

namespace visionrt {
namespace {
QString resolvePreferencesPath()
{
    const auto directory = QStandardPaths::writableLocation(QStandardPaths::AppConfigLocation);
    return directory.isEmpty() ? QString{} : QDir(directory).filePath(QStringLiteral("ui-preferences.json"));
}

QJsonObject readRoot()
{
    const auto path = resolvePreferencesPath();
    if (path.isEmpty())
        return {};
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly))
        return {};
    const auto document = QJsonDocument::fromJson(file.readAll());
    return document.isObject() ? document.object() : QJsonObject{};
}

// One atomic writer for the whole document. QSaveFile keeps the previous file until commit, so a
// full disk or a killed process cannot truncate the settings the application is still using.
bool writeRoot(const QJsonObject &root)
{
    const auto path = resolvePreferencesPath();
    if (path.isEmpty())
        return false;
    QDir().mkpath(QFileInfo(path).absolutePath());
    QSaveFile file(path);
    if (!file.open(QIODevice::WriteOnly))
        return false;
    const auto payload = QJsonDocument(root).toJson(QJsonDocument::Indented);
    if (file.write(payload) != payload.size())
    {
        file.cancelWriting();
        return false;
    }
    return file.commit();
}
} // namespace

QString UiPreferences::path()
{
    return resolvePreferencesPath();
}

bool UiPreferences::easyTouchEnabled()
{
    const auto interaction = readRoot().value(QStringLiteral("interaction")).toObject();
    return interaction.value(QStringLiteral("easyTouchEnabled")).toBool(true);
}

void UiPreferences::setEasyTouchEnabled(bool enabled)
{
    auto       root        = readRoot();
    auto       interaction = root.value(QStringLiteral("interaction")).toObject();
    interaction.insert(QStringLiteral("easyTouchEnabled"), enabled);
    root.insert(QStringLiteral("interaction"), interaction);
    writeRoot(root);
}

QJsonArray UiPreferences::recentProjects()
{
    return readRoot().value(QStringLiteral("recentProjects")).toArray();
}

bool UiPreferences::setRecentProjects(const QJsonArray &entries)
{
    auto root = readRoot();
    root.insert(QStringLiteral("recentProjects"), entries);
    return writeRoot(root);
}
} // namespace visionrt
