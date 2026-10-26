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
QString preferencesPath()
{
    const auto directory = QStandardPaths::writableLocation(QStandardPaths::AppConfigLocation);
    return directory.isEmpty() ? QString{} : QDir(directory).filePath(QStringLiteral("ui-preferences.json"));
}

QJsonObject readRoot()
{
    const auto path = preferencesPath();
    if (path.isEmpty())
        return {};
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly))
        return {};
    const auto document = QJsonDocument::fromJson(file.readAll());
    return document.isObject() ? document.object() : QJsonObject{};
}

void writeRoot(const QJsonObject &root)
{
    const auto path = preferencesPath();
    if (path.isEmpty())
        return;
    QDir().mkpath(QFileInfo(path).absolutePath());
    QSaveFile file(path);
    if (!file.open(QIODevice::WriteOnly))
        return;
    file.write(QJsonDocument(root).toJson(QJsonDocument::Indented));
    file.commit();
}
} // namespace

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
} // namespace visionrt
