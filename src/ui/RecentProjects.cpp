#include "ui/RecentProjects.h"

#include "ui/UiPreferences.h"

#include <QDir>
#include <QFileInfo>
#include <QHash>
#include <QJsonObject>
#include <QSet>
#include <QStringList>
#include <QVariantList>
#include <QVariantMap>

#include <algorithm>

namespace visionrt {
namespace {

// Path identity follows the platform's own rules: Windows and macOS compare case-insensitively,
// Linux does not, and nothing is lowercased blindly.
bool samePath(const QString &left, const QString &right)
{
#if defined(Q_OS_WIN) || defined(Q_OS_MACOS)
    return left.compare(right, Qt::CaseInsensitive) == 0;
#else
    return left == right;
#endif
}

QStringList parentComponents(const QString &path)
{
    const auto directory = QFileInfo(path).absolutePath();
    return QDir::fromNativeSeparators(directory).split(QLatin1Char('/'), Qt::SkipEmptyParts);
}

// A same-named file is labelled with the shortest trailing folders that make the group readable.
// Entries whose name is unique keep the plain file name.
void widenLabels(QVector<RecentProjectEntry> &entries)
{
    QHash<QString, QVector<int>> groups;
    for (int index = 0; index < entries.size(); ++index)
        groups[QFileInfo(entries[index].path).fileName()].push_back(index);
    for (auto it = groups.begin(); it != groups.end(); ++it)
    {
        const auto &indices = it.value();
        if (indices.size() < 2)
            continue;
        int widest = 1;
        for (const int index : indices)
            widest = std::max(widest, static_cast<int>(parentComponents(entries[index].path).size()));
        for (int depth = 1; depth <= widest; ++depth)
        {
            QStringList labels;
            labels.reserve(indices.size());
            for (const int index : indices)
            {
                const auto components = parentComponents(entries[index].path);
                const auto start      = std::max(0, static_cast<int>(components.size()) - depth);
                labels.push_back(components.mid(start).join(QLatin1Char('/')) + QLatin1Char('/') + it.key());
            }
            QSet<QString> seen;
            for (const auto &label : labels)
                seen.insert(label);
            if (seen.size() != labels.size())
                continue;
            for (int position = 0; position < indices.size(); ++position)
                entries[indices[position]].text = labels[position];
            break;
        }
    }
}

} // namespace

QString RecentProjects::identity(const QString &path)
{
    const auto trimmed = path.trimmed();
    if (trimmed.isEmpty() || !QDir::isAbsolutePath(trimmed))
        return {};
    const QFileInfo info(trimmed);
    // An existing file is resolved through its canonical form so a junction or a case variant is
    // the same project. A missing file keeps its absolute path: the entry stays usable for a later
    // recovery instead of disappearing.
    const auto canonical = info.canonicalFilePath();
    return canonical.isEmpty() ? QDir::cleanPath(info.absoluteFilePath()) : canonical;
}

QVector<RecentProjectEntry> RecentProjects::normalise(const QVector<RecentProjectEntry> &entries)
{
    QVector<RecentProjectEntry> result;
    result.reserve(std::min(entries.size(), static_cast<qsizetype>(kMaximum)));
    for (const auto &entry : entries)
    {
        const auto identity = RecentProjects::identity(entry.path);
        if (identity.isEmpty())
            continue;
        const auto duplicate = std::find_if(result.begin(), result.end(), [&](const auto &existing)
                                            { return samePath(existing.path, identity); });
        if (duplicate != result.end())
            continue;
        result.push_back({identity, QFileInfo(identity).fileName(), QDir::toNativeSeparators(identity)});
        if (result.size() == kMaximum)
            break;
    }
    widenLabels(result);
    return result;
}

QVector<RecentProjectEntry> RecentProjects::fromJson(const QJsonArray &array)
{
    QVector<RecentProjectEntry> entries;
    entries.reserve(array.size());
    for (const auto &value : array)
    {
        // Records are read leniently: the object form carries a path, a bare string is accepted as
        // one, and anything else is ignored rather than poisoning the list.
        const auto path = value.isObject() ? value.toObject().value(QStringLiteral("path")).toString() : value.toString();
        entries.push_back({path, {}, {}});
    }
    return normalise(entries);
}

QJsonArray RecentProjects::toJson(const QVector<RecentProjectEntry> &entries)
{
    QJsonArray array;
    for (const auto &entry : entries)
    {
        QJsonObject object;
        object.insert(QStringLiteral("path"), entry.path);
        array.push_back(object);
    }
    return array;
}

QVariantList RecentProjects::toVariantList(const QVector<RecentProjectEntry> &entries)
{
    QVariantList list;
    list.reserve(entries.size());
    for (const auto &entry : entries)
    {
        QVariantMap item;
        item.insert(QStringLiteral("path"), entry.path);
        item.insert(QStringLiteral("text"), entry.text);
        item.insert(QStringLiteral("tooltip"), entry.tooltip);
        list.push_back(item);
    }
    return list;
}

QVector<RecentProjectEntry> RecentProjects::load()
{
    return fromJson(UiPreferences::recentProjects());
}

QVector<RecentProjectEntry> RecentProjects::record(const QString &path)
{
    const auto identity = RecentProjects::identity(path);
    if (identity.isEmpty())
        return load();
    auto entries = load();
    entries.erase(std::remove_if(entries.begin(), entries.end(), [&](const auto &entry)
                                 { return samePath(entry.path, identity); }),
                  entries.end());
    entries.prepend({identity, {}, {}});
    auto normalised = normalise(entries);
    // A failed write keeps the previous file and the in-memory order: the project already opened,
    // and losing the order is a smaller harm than refusing the open.
    UiPreferences::setRecentProjects(toJson(normalised));
    return normalised;
}
} // namespace visionrt
