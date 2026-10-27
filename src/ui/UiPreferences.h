#pragma once

#include <QJsonArray>

#include <vector>

namespace visionrt {

// User-scoped interaction preferences. They never enter the project file, the
// tool declaration or the runtime inputs, and changing them cannot dirty a
// project or rerun a tool.
//
// Everything lives in one file, and every writer merges its own key into the
// document it reads, so writing the recent list can never erase an interaction
// setting and the other way round.
class UiPreferences
{
public:
    // Smart point selection is on by default so the first installation keeps the
    // automatic behaviour; an explicit user choice survives nodes and restarts.
    static bool easyTouchEnabled();
    static void setEasyTouchEnabled(bool enabled);

    // Stored recent-project records, exactly as persisted. Corrupt content is
    // reported as an empty list: a broken preferences file must never stop a
    // project from opening.
    static QJsonArray recentProjects();
    // Replaces the list atomically and reports whether the file was written. A
    // failed write leaves the previous file intact and the caller keeps the
    // in-memory list, because the project itself already opened successfully.
    static bool setRecentProjects(const QJsonArray &entries);

    // Absolute path of the preferences file, empty when the platform provides no
    // writable configuration location.
    static QString path();
};

} // namespace visionrt
