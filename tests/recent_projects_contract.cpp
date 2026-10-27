#ifdef NDEBUG
#    undef NDEBUG
#endif

#include "ui/RecentProjects.h"
#include "ui/UiPreferences.h"

#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonObject>
#include <QStandardPaths>
#include <QTemporaryDir>

#include <cassert>
#include <iostream>

using namespace visionrt;

namespace {
void require(bool condition, const std::string &message)
{
    if (!condition)
    {
        std::cerr << "recent projects contract failed: " << message << std::endl;
        std::abort();
    }
}

QString makeProject(const QDir &root, const QString &relative)
{
    const auto path = root.filePath(relative);
    QDir().mkpath(QFileInfo(path).absolutePath());
    QFile    file(path);
    require(file.open(QIODevice::WriteOnly), "cannot create fixture project " + relative.toStdString());
    file.write("{}");
    file.close();
    return path;
}

QStringList texts(const QVector<RecentProjectEntry> &entries)
{
    QStringList result;
    for (const auto &entry : entries)
        result.push_back(entry.text);
    return result;
}
} // namespace

int main()
{
    QCoreApplication::setOrganizationName(QStringLiteral("VisionRT"));
    QCoreApplication::setApplicationName(QStringLiteral("recent-projects-contract"));
    QStandardPaths::setTestModeEnabled(true);
    const auto configDirectory = QDir(QStandardPaths::writableLocation(QStandardPaths::AppConfigLocation));
    QDir(configDirectory.absolutePath()).removeRecursively();

    QTemporaryDir root;
    require(root.isValid(), "temporary directory is unavailable");

    // MRU-01: an empty history is a legal state and the menu is simply disabled.
    require(RecentProjects::load().isEmpty(), "a fresh installation must start with no recent projects");
    require(RecentProjects::identity(QStringLiteral("relative/path.vrproj")).isEmpty(),
            "a relative path is not a project identity");

    // MRU-02: the newest successful open is at the front, and a repeat moves the existing entry
    // instead of adding a second one.
    const auto first  = makeProject(root.path() + QStringLiteral("/a"), QStringLiteral("工程一.vrproj"));
    const auto second = makeProject(root.path() + QStringLiteral("/b"), QStringLiteral("第二个 工程.vrproj"));
    auto       entries = RecentProjects::record(first);
    require(entries.size() == 1 && QFileInfo(entries.front().path).fileName() == QStringLiteral("工程一.vrproj"),
            "the first recorded project is not at the front");
    entries = RecentProjects::record(second);
    require(entries.size() == 2 && QFileInfo(entries.front().path).fileName() == QStringLiteral("第二个 工程.vrproj"),
            "the newest project is not at the front");
    entries = RecentProjects::record(first);
    require(entries.size() == 2 && QFileInfo(entries.front().path).fileName() == QStringLiteral("工程一.vrproj"),
            "reopening an existing project must move it, not duplicate it");

    // MRU-03: the order survives a restart because it is stored in the same preferences document.
    const auto reloaded = RecentProjects::load();
    require(reloaded.size() == 2 && QFileInfo(reloaded.front().path).fileName() == QStringLiteral("工程一.vrproj"),
            "the recent order was not persisted");

    // MRU-04: ten entries, the eleventh drops the oldest one, and the menu labels stay readable.
    QVector<RecentProjectEntry> grown;
    QStringList                 created;
    for (int index = 0; index < 11; ++index)
    {
        const auto path = makeProject(root.path() + QStringLiteral("/many"), QStringLiteral("p%1.vrproj").arg(index));
        created.push_back(path);
        grown = RecentProjects::record(path);
    }
    require(grown.size() == RecentProjects::kMaximum, "the recent list grew past ten entries");
    require(QFileInfo(grown.front().path).fileName() == QStringLiteral("p10.vrproj"),
            "the last successful open is not first");
    for (const auto &entry : grown)
        require(QFileInfo(entry.path).fileName() != QStringLiteral("p0.vrproj"), "the oldest entry was not evicted");

    // MRU-05: same-named projects in different folders are told apart by the shortest parent suffix
    // they need, and the tooltip keeps the full path.
    QDir(configDirectory.absolutePath()).removeRecursively();
    const auto left  = makeProject(root.path() + QStringLiteral("/left"), QStringLiteral("same.vrproj"));
    const auto right = makeProject(root.path() + QStringLiteral("/right"), QStringLiteral("same.vrproj"));
    RecentProjects::record(left);
    const auto labelled = RecentProjects::record(right);
    require(labelled.size() == 2, "same-named projects collapsed into one entry");
    require(texts(labelled).contains(QStringLiteral("left/same.vrproj"))
                && texts(labelled).contains(QStringLiteral("right/same.vrproj")),
            "same-named projects are not distinguishable in the menu");
    for (const auto &entry : labelled)
        require(entry.tooltip == QDir::toNativeSeparators(entry.path), "the tooltip does not carry the full path");

    // MRU-06: the platform's own path rules decide identity, so a case variant is not a new project.
    const auto variant = QDir::fromNativeSeparators(QFileInfo(right).absolutePath()) + QStringLiteral("/SAME.vrproj");
    const auto deduped = RecentProjects::record(variant);
#if defined(Q_OS_WIN) || defined(Q_OS_MACOS)
    require(deduped.size() == 2, "a case variant created a second recent entry on a case-insensitive platform");
#else
    require(deduped.size() == 3, "a case variant was merged on a case-sensitive platform");
#endif

    // MRU-07: the recent list and the interaction settings share one file without erasing each other.
    UiPreferences::setEasyTouchEnabled(false);
    RecentProjects::record(first);
    require(!UiPreferences::easyTouchEnabled(), "recording a project reset the interaction preference");
    require(!RecentProjects::load().isEmpty(), "the recent list is empty after an interaction write");
    UiPreferences::setEasyTouchEnabled(true);
    require(RecentProjects::load().size() == 3, "changing an interaction preference cleared the recent list");

    // MRU-08: a corrupt preferences file reads as an empty list and never blocks a project open.
    {
        QFile file(UiPreferences::path());
        require(file.open(QIODevice::WriteOnly | QIODevice::Truncate), "cannot corrupt the preferences file");
        file.write("{ this is not json");
        file.close();
    }
    require(RecentProjects::load().isEmpty(), "a corrupt preferences file must read as an empty list");
    require(UiPreferences::easyTouchEnabled(), "a corrupt preferences file must keep the interaction default");

    // MRU-09: illegal records inside an otherwise valid list are dropped, valid ones survive.
    {
        UiPreferences::setRecentProjects(QJsonArray{QStringLiteral("relative.vrproj"), first, QJsonObject{}});
        const auto salvaged = RecentProjects::load();
        require(salvaged.size() == 1 && salvaged.front().path == RecentProjects::identity(first),
                "an invalid record discarded the valid entries around it");
    }

    // MRU-10: a missing file is still a legal entry, so a temporarily unavailable project can be
    // recovered from the menu instead of silently disappearing.
    const auto missing = root.filePath(QStringLiteral("moved/away.vrproj"));
    const auto kept    = RecentProjects::record(missing);
    require(!kept.isEmpty() && kept.front().path == QDir::cleanPath(missing),
            "a missing project was dropped from the recent list");

    std::cout << "Recent projects keep ten entries, de-duplicate by path identity and never erase the "
                 "interaction settings stored beside them\n";
    return 0;
}
