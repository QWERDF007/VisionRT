// Settings contract: the three platform settings live in the same document as the recent-project
// list and the EasyTouch preference. A first run may be written from defaults; a damaged or
// unreadable document keeps its bytes and is reported instead of being replaced.
#include "ui/UiPreferences.h"

#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QJsonDocument>
#include <QJsonObject>

#include <iostream>
#include <stdexcept>
#include <string>

using namespace visionrt;

namespace
{
void require(bool condition, const char *message)
{
    if (!condition)
        throw std::runtime_error(message);
}

QByteArray readAll(const QString &path)
{
    QFile file(path);
    require(file.open(QIODevice::ReadOnly), "cannot read the preferences file");
    return file.readAll();
}

void writeAll(const QString &path, const QByteArray &content)
{
    QFile file(path);
    require(file.open(QIODevice::WriteOnly | QIODevice::Truncate), "cannot write the preferences file");
    require(file.write(content) == content.size(), "short write on the preferences file");
}

QJsonObject document(const QString &path)
{
    const auto parsed = QJsonDocument::fromJson(readAll(path));
    require(parsed.isObject(), "the preferences file is not a JSON object");
    return parsed.object();
}

// Keeps whatever the build tree already had, so running the contract never destroys a developer's
// recent-project list.
class PathGuard
{
public:
    explicit PathGuard(QString path) : path_(std::move(path))
    {
        QFile file(path_);
        existed_ = file.exists() && file.open(QIODevice::ReadOnly);
        if (existed_)
            backup_ = file.readAll();
    }
    ~PathGuard()
    {
        QFile::remove(path_);
        if (existed_)
        {
            QFile file(path_);
            if (file.open(QIODevice::WriteOnly))
                file.write(backup_);
        }
    }
    PathGuard(const PathGuard &)            = delete;
    PathGuard &operator=(const PathGuard &) = delete;

private:
    QString    path_;
    QByteArray backup_;
    bool       existed_ = false;
};
} // namespace

int main(int argc, char **argv)
try
{
    QCoreApplication application(argc, argv);
    const auto       path = UiPreferences::path();
    require(!path.isEmpty(), "the preferences path is empty");
    PathGuard guard(path);
    QFile::remove(path);

    // S06: a missing file is the first run, and only then do the documented defaults apply.
    Settings     settings;
    QString      error;
    require(UiPreferences::loadSettings(settings, &error) == SettingsState::FirstRun,
            "a missing file is not reported as a first run");
    require(!settings.editBuiltins && settings.loggingLevel == LogLevel::Info && settings.pythonExecutable.isEmpty(),
            "the first-run defaults are not the documented ones");

    // S06: one commit writes the three keys.
    Settings chosen;
    chosen.editBuiltins     = true;
    chosen.loggingLevel     = LogLevel::Warn;
    chosen.pythonExecutable = QStringLiteral("F:/envs/visionrt/python.exe");
    require(UiPreferences::saveSettings(chosen, &error), "the first save failed");
    auto stored = document(path);
    require(stored.value(QStringLiteral("toolEditor")).toObject().value(QStringLiteral("editBuiltins")).toBool(),
            "the tool editor switch was not written");
    require(stored.value(QStringLiteral("logging")).toObject().value(QStringLiteral("level")).toString()
                == QStringLiteral("warn"),
            "the log level was not written under its stable key");
    require(stored.value(QStringLiteral("python")).toObject().value(QStringLiteral("executable")).toString()
                == chosen.pythonExecutable,
            "the interpreter path was not written");

    require(UiPreferences::loadSettings(settings, &error) == SettingsState::Loaded, "the saved file is not readable");
    require(settings.editBuiltins && settings.loggingLevel == LogLevel::Warn
                && settings.pythonExecutable == chosen.pythonExecutable,
            "the saved settings did not round-trip");

    // S06: every other key survives a settings commit, because each writer merges its own keys.
    const QByteArray seeded = R"({
        "recentProjects": [{"path": "F:/work/a.vrt.json"}],
        "interaction": {"easyTouchEnabled": false},
        "future": {"kept": 7}
    })";
    writeAll(path, seeded);
    require(UiPreferences::saveSettings(chosen, &error), "saving into an existing document failed");
    stored = document(path);
    require(stored.value(QStringLiteral("recentProjects")).toArray().size() == 1,
            "a settings commit erased the recent-project list");
    require(!stored.value(QStringLiteral("interaction")).toObject().value(QStringLiteral("easyTouchEnabled")).toBool(true),
            "a settings commit reset the interaction preference");
    require(stored.value(QStringLiteral("future")).toObject().value(QStringLiteral("kept")).toInt() == 7,
            "a settings commit dropped an unknown key");

    // S06: a damaged document is reported and never overwritten, by any writer.
    const QByteArray damaged = "{ this is not json";
    writeAll(path, damaged);
    error.clear();
    require(UiPreferences::loadSettings(settings, &error) == SettingsState::Unreadable,
            "a damaged document is not reported as unreadable");
    require(!error.isEmpty(), "an unreadable document produced no reason");
    require(!UiPreferences::saveSettings(chosen, &error), "a damaged document was overwritten by a settings save");
    require(readAll(path) == damaged, "a refused settings save changed the file");
    require(!UiPreferences::setRecentProjects({}), "a damaged document was overwritten by a recent-project write");
    require(!UiPreferences::setEasyTouchEnabled(false), "a damaged document was overwritten by an interaction write");
    require(readAll(path) == damaged, "a refused write changed the file");

    // S06: an unknown level is a damaged setting, not a reason to silently reset to the default.
    writeAll(path, R"({"logging": {"level": "verbose"}})");
    require(UiPreferences::loadSettings(settings, &error) == SettingsState::Unreadable,
            "an unknown log level was accepted");
    require(!UiPreferences::saveSettings(chosen, &error), "an unknown log level was overwritten");

    // SET-TYPE-01: a key that exists with the wrong type is damage too. Reading it as a default
    // would quietly change a visible setting and then write that default back over the file.
    const QByteArray mistyped[] = {
        R"({"toolEditor": {"editBuiltins": "yes"}})",
        R"({"logging": {"level": 3}})",
        R"({"python": {"executable": false}})",
        R"({"python": "C:/python.exe"})",
    };
    for (const auto &broken : mistyped)
    {
        writeAll(path, broken);
        require(UiPreferences::loadSettings(settings, &error) == SettingsState::Unreadable,
                "a wrongly typed setting was accepted");
        require(!UiPreferences::saveSettings(chosen, &error), "a wrongly typed setting was overwritten");
        require(readAll(path) == broken, "a refused save changed a wrongly typed file");
    }

    // S06: an unreadable path (a directory in place of the file) is not a first run.
    QFile::remove(path);
    require(QDir().mkpath(path), "cannot create the directory that blocks the path");
    require(UiPreferences::loadSettings(settings, &error) == SettingsState::Unreadable,
            "an unreadable path was treated as a first run");
    require(!UiPreferences::saveSettings(chosen, &error), "an unreadable path was written");
    QDir().rmdir(path);

    // S04: the off threshold is a legal stored value.
    Settings quiet;
    quiet.loggingLevel = LogLevel::Off;
    require(UiPreferences::saveSettings(quiet, &error), "saving the off threshold failed");
    require(UiPreferences::loadSettings(settings, &error) == SettingsState::Loaded
                && settings.loggingLevel == LogLevel::Off,
            "the off threshold did not round-trip");

    std::cout << "Settings share one document, merge their own keys, and a damaged or unreadable file is "
                 "reported instead of replaced\n";
    return 0;
}
catch (const std::exception &failure)
{
    std::cerr << "settings contract failed: " << failure.what() << '\n';
    return 1;
}
