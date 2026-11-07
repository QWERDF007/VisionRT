#pragma once

#include "log/LogLevel.h"

#include <QJsonArray>
#include <QString>

namespace visionrt
{

// The platform settings that share ui-preferences.json with the recent-project list and the
// EasyTouch preference. Each writer owns its own keys and merges them into the document it reads,
// so no writer can erase another one's state.
struct Settings
{
    // The tool editor offers the declarations shipped with the installation only when this is on.
    // Off is the default: editing an installed declaration is an authoring task, not daily work.
    bool     editBuiltins = false;
    // One production threshold for the file and the window channel.
    LogLevel loggingLevel = LogLevel::Info;
    // Absolute path of the external interpreter chosen by the user. Empty is a legal "not
    // configured" value; the next start runs without Python and the native flow stays available.
    QString  pythonExecutable;
};

// Why the document could not be used. FirstRun is the only state whose defaults may be written
// over: a damaged or unreadable file keeps its bytes and is reported to the caller instead of
// being replaced by a fresh document.
enum class SettingsState
{
    FirstRun,
    Loaded,
    Unreadable
};

// User-scoped interaction settings. They never enter the project file, a tool declaration or the
// runtime inputs, and changing them cannot dirty a project or rerun a tool.
class UiPreferences
{
public:
    static SettingsState loadSettings(Settings &settings, QString *error = nullptr);
    // Merges the three keys into the current document and commits it once. False means nothing was
    // written: the previous file and the running behaviour stay as they were.
    static bool saveSettings(const Settings &settings, QString *error = nullptr);

    // Smart point selection is on by default so the first installation keeps the automatic
    // behaviour; an explicit user choice survives nodes and restarts.
    static bool easyTouchEnabled();
    // Returns false when the preferences file could not be written; the caller reports it instead of
    // claiming the choice will survive a restart.
    static bool setEasyTouchEnabled(bool enabled);

    // Stored recent-project records, exactly as persisted. Corrupt content is reported as an empty
    // list: a broken preferences file must never stop a project from opening.
    static QJsonArray recentProjects();
    // Replaces the list atomically and reports whether the file was written. A failed write leaves
    // the previous file intact and the caller keeps the in-memory list, because the project itself
    // already opened successfully.
    static bool setRecentProjects(const QJsonArray &entries);

    // Absolute path of the preferences file, empty when the platform provides no writable
    // configuration location.
    static QString path();
};

} // namespace visionrt
