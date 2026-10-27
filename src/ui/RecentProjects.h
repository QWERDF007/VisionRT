#pragma once

#include <QJsonArray>
#include <QString>
#include <QVariantList>
#include <QVector>

namespace visionrt {

// One "recently opened project" record. The identity is the canonical absolute path; the display
// text is rebuilt at load time so a renamed sibling folder never leaves a stale label behind.
struct RecentProjectEntry
{
    QString path;    // canonical absolute path used for identity and reopening
    QString text;    // file name, widened by the shortest parent suffix that separates duplicates
    QString tooltip; // full path for the menu tooltip

    bool operator==(const RecentProjectEntry &) const = default;
};

// The most recently opened projects, newest first. This is UI state: it never enters a project
// file, never dirties a document and never becomes a second way to open one.
class RecentProjects
{
public:
    static constexpr int kMaximum = 10;

    // Reads the persisted list. Corrupt records and illegal paths are dropped, valid ones survive
    // and the result is truncated, so one bad line cannot hide the rest.
    static QVector<RecentProjectEntry> load();

    // Moves a successfully opened or saved-as project to the front and persists the new order.
    // Returns the new list whether or not the write succeeded; the caller only reports the failure.
    static QVector<RecentProjectEntry> record(const QString &path);

    // Canonical absolute path, or an empty string when the input cannot be a project path.
    static QString identity(const QString &path);

    // Applies ordering, de-duplication, truncation and label widening without touching the disk.
    static QVector<RecentProjectEntry> normalise(const QVector<RecentProjectEntry> &entries);

    static QVector<RecentProjectEntry> fromJson(const QJsonArray &array);
    static QJsonArray                  toJson(const QVector<RecentProjectEntry> &entries);
    static QVariantList                toVariantList(const QVector<RecentProjectEntry> &entries);
};

} // namespace visionrt
