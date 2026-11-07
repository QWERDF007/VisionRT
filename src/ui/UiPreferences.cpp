#include "ui/UiPreferences.h"

#include "core/ToolPaths.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonParseError>
#include <QSaveFile>

#include <utility>

namespace visionrt
{
namespace
{
QString resolvePreferencesPath()
{
    // Interaction settings are part of the installation, exactly like the user tools: the software
    // directory is the only writable root this application uses.
    return QString::fromStdWString((softwareDataDir() / "ui-preferences.json").wstring());
}

// One read for every writer, so "the file is missing" and "the file is damaged" stay different
// answers. Only the first may be replaced by a fresh document.
struct Document
{
    QJsonObject   root;
    SettingsState state  = SettingsState::Unreadable;
    QString       error;
};

Document readDocument()
{
    Document document;
    const auto path = resolvePreferencesPath();
    if (path.isEmpty())
    {
        document.error = QStringLiteral("无法确定软件目录，界面设置不可用");
        return document;
    }
    QFile file(path);
    if (!file.exists())
    {
        document.state = SettingsState::FirstRun;
        return document;
    }
    if (!file.open(QIODevice::ReadOnly))
    {
        document.error = QStringLiteral("无法读取设置文件（%1）：%2").arg(path, file.errorString());
        return document;
    }
    const auto bytes = file.readAll();
    if (file.error() != QFileDevice::NoError)
    {
        document.error = QStringLiteral("无法读取设置文件（%1）：%2").arg(path, file.errorString());
        return document;
    }
    QJsonParseError parse{};
    const auto      parsed = QJsonDocument::fromJson(bytes, &parse);
    if (!parsed.isObject())
    {
        document.error = QStringLiteral("设置文件已损坏（%1）：%2")
                             .arg(path, parse.errorString());
        return document;
    }
    document.root  = parsed.object();
    document.state = SettingsState::Loaded;
    return document;
}

// One atomic writer for the whole document. QSaveFile keeps the previous file until commit, so a
// full disk or a killed process cannot truncate the settings the application is still using.
bool writeRoot(const QJsonObject &root, QString *error)
{
    const auto path = resolvePreferencesPath();
    if (path.isEmpty())
    {
        if (error)
            *error = QStringLiteral("无法确定软件目录，界面设置不可用");
        return false;
    }
    QDir().mkpath(QFileInfo(path).absolutePath());
    QSaveFile file(path);
    if (!file.open(QIODevice::WriteOnly))
    {
        if (error)
            *error = QStringLiteral("无法写入设置文件（%1）：%2").arg(path, file.errorString());
        return false;
    }
    const auto payload = QJsonDocument(root).toJson(QJsonDocument::Indented);
    if (file.write(payload) != payload.size())
    {
        if (error)
            *error = QStringLiteral("无法写入设置文件（%1）：%2").arg(path, file.errorString());
        file.cancelWriting();
        return false;
    }
    if (!file.commit())
    {
        if (error)
            *error = QStringLiteral("无法写入设置文件（%1）：%2").arg(path, file.errorString());
        return false;
    }
    return true;
}

// Reads the three keys of an existing document. A key that is present but cannot be understood is
// damage, not a reason to fall back to a default and overwrite it on the next save.
bool applyStoredSettings(const QJsonObject &root, Settings &settings, QString *error)
{
    settings.editBuiltins = root.value(QStringLiteral("toolEditor"))
                                .toObject()
                                .value(QStringLiteral("editBuiltins"))
                                .toBool(false);

    const auto logging = root.value(QStringLiteral("logging")).toObject();
    if (logging.contains(QStringLiteral("level")))
    {
        const auto key = logging.value(QStringLiteral("level")).toString().toStdString();
        if (!parseLogLevel(key, settings.loggingLevel))
        {
            if (error)
                *error = QStringLiteral("设置文件中的日志等级无效：%1")
                             .arg(logging.value(QStringLiteral("level")).toVariant().toString());
            return false;
        }
    }

    settings.pythonExecutable =
        root.value(QStringLiteral("python")).toObject().value(QStringLiteral("executable")).toString();
    return true;
}

// Every writer starts from the current document, and only a first run or a fully valid document may
// be written back. A damaged file is never replaced by the merge of what could be salvaged.
bool readForWrite(QJsonObject &root, QString *error = nullptr)
{
    auto document = readDocument();
    if (document.state == SettingsState::Unreadable)
    {
        if (error)
            *error = document.error;
        return false;
    }
    Settings stored;
    if (document.state == SettingsState::Loaded && !applyStoredSettings(document.root, stored, error))
        return false;
    root = std::move(document.root);
    return true;
}
} // namespace

QString UiPreferences::path()
{
    return resolvePreferencesPath();
}

SettingsState UiPreferences::loadSettings(Settings &settings, QString *error)
{
    settings      = Settings{};
    auto document = readDocument();
    if (error)
        *error = document.error;
    if (document.state != SettingsState::Loaded)
        return document.state;
    if (!applyStoredSettings(document.root, settings, error))
        return SettingsState::Unreadable;
    return SettingsState::Loaded;
}

bool UiPreferences::saveSettings(const Settings &settings, QString *error)
{
    QJsonObject root;
    if (!readForWrite(root, error))
        return false;

    auto editor = root.value(QStringLiteral("toolEditor")).toObject();
    editor.insert(QStringLiteral("editBuiltins"), settings.editBuiltins);
    root.insert(QStringLiteral("toolEditor"), editor);

    auto logging = root.value(QStringLiteral("logging")).toObject();
    const auto levelKey = logLevelKey(settings.loggingLevel);
    logging.insert(QStringLiteral("level"), QString::fromLatin1(levelKey.data(), static_cast<qsizetype>(levelKey.size())));
    root.insert(QStringLiteral("logging"), logging);

    auto python = root.value(QStringLiteral("python")).toObject();
    python.insert(QStringLiteral("executable"), settings.pythonExecutable);
    root.insert(QStringLiteral("python"), python);

    return writeRoot(root, error);
}

bool UiPreferences::easyTouchEnabled()
{
    const auto root        = readDocument().root;
    const auto interaction = root.value(QStringLiteral("interaction")).toObject();
    return interaction.value(QStringLiteral("easyTouchEnabled")).toBool(true);
}

bool UiPreferences::setEasyTouchEnabled(bool enabled)
{
    QJsonObject root;
    if (!readForWrite(root))
        return false;
    auto interaction = root.value(QStringLiteral("interaction")).toObject();
    interaction.insert(QStringLiteral("easyTouchEnabled"), enabled);
    root.insert(QStringLiteral("interaction"), interaction);
    return writeRoot(root, nullptr);
}

QJsonArray UiPreferences::recentProjects()
{
    return readDocument().root.value(QStringLiteral("recentProjects")).toArray();
}

bool UiPreferences::setRecentProjects(const QJsonArray &entries)
{
    QJsonObject root;
    if (!readForWrite(root))
        return false;
    root.insert(QStringLiteral("recentProjects"), entries);
    return writeRoot(root, nullptr);
}
} // namespace visionrt
