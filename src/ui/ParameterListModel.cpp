#include "ui/ParameterListModel.h"

#include <algorithm>

namespace visionrt {

ParameterListModel::ParameterListModel(QObject *parent)
    : QAbstractListModel(parent)
{
}

QString ParameterListModel::makeKey(const QString &nodeId, int stage, bool recordScoped, const QVariant &datasetId,
                                    const QString &recordId, int id)
{
    return QStringLiteral("%1:%2:%3:%4:%5:%6")
        .arg(nodeId)
        .arg(stage)
        .arg(recordScoped ? 1 : 0)
        .arg(datasetId.isValid() ? datasetId.toString() : QString{})
        .arg(recordId)
        .arg(id);
}

void ParameterListModel::clear()
{
    if (rows_.empty())
        return;
    beginResetModel();
    rows_.clear();
    indexByKey_.clear();
    groups_.clear();
    endResetModel();
    emit groupsChanged();
}

void ParameterListModel::sync(std::vector<Row> nextRows)
{
    const bool sameTopology = nextRows.size() == rows_.size()
        && std::equal(rows_.begin(), rows_.end(), nextRows.begin(),
                      [](const Row &a, const Row &b)
                      { return a.key == b.key && a.nodeId == b.nodeId && a.group == b.group && a.tab == b.tab; });

    if (sameTopology)
    {
        for (std::size_t i = 0; i < rows_.size(); ++i)
        {
            if (rows_[i].map == nextRows[i].map)
                continue;
            rows_[i].map = std::move(nextRows[i].map);
            const auto idx = index(static_cast<int>(i));
            emit dataChanged(idx, idx, {ParameterRole});
        }
        return;
    }

    beginResetModel();
    rows_ = std::move(nextRows);
    rebuildIndexAndGroups();
    endResetModel();
    emit groupsChanged();
}

bool ParameterListModel::updateRowByKey(const QString &key, const QVariantMap &updatedMap)
{
    const auto found = indexByKey_.find(key);
    if (found == indexByKey_.end())
        return false;
    auto &row = rows_[static_cast<std::size_t>(found->second)];
    if (row.map == updatedMap)
        return false;
    row.map        = updatedMap;
    const auto idx = index(found->second);
    emit dataChanged(idx, idx, {ParameterRole});
    return true;
}

QVariantMap ParameterListModel::rowByKey(const QString &key) const
{
    const auto found = indexByKey_.find(key);
    if (found == indexByKey_.end())
        return {};
    return rows_[static_cast<std::size_t>(found->second)].map;
}

int ParameterListModel::rowIndexOf(const QString &key) const
{
    const auto found = indexByKey_.find(key);
    return found == indexByKey_.end() ? -1 : found->second;
}

QVariantList ParameterListModel::groupsForTab(int tab) const
{
    QVariantList result;
    for (const auto &entry : groups_)
    {
        const auto map = entry.toMap();
        if (map.value(QStringLiteral("tab")).toInt() == tab)
            result.push_back(map);
    }
    return result;
}

QVariantList ParameterListModel::snapshot() const
{
    QVariantList result;
    result.reserve(static_cast<qsizetype>(rows_.size()));
    for (const auto &row : rows_)
        result.push_back(row.map);
    return result;
}

QVariantList ParameterListModel::groupsSnapshot() const
{
    return groups_;
}

int ParameterListModel::rowCount(const QModelIndex &parent) const
{
    return parent.isValid() ? 0 : static_cast<int>(rows_.size());
}

QVariant ParameterListModel::data(const QModelIndex &index, int role) const
{
    if (!index.isValid() || index.row() < 0 || index.row() >= rowCount())
        return {};
    const auto &row = rows_[static_cast<std::size_t>(index.row())];
    switch (role)
    {
    case KeyRole:
        return row.key;
    case ParameterRole:
        return row.map;
    case GroupRole:
        return row.group;
    case TabRole:
        return row.tab;
    case NodeIdRole:
        return row.nodeId;
    default:
        return {};
    }
}

QHash<int, QByteArray> ParameterListModel::roleNames() const
{
    return {
        {      KeyRole,       "key"},
        {ParameterRole, "parameter"},
        {    GroupRole,     "group"},
        {      TabRole,       "tab"},
        {   NodeIdRole,    "nodeId"}
    };
}

void ParameterListModel::rebuildIndexAndGroups()
{
    indexByKey_.clear();
    indexByKey_.reserve(rows_.size());
    groups_.clear();

    for (std::size_t i = 0; i < rows_.size(); ++i)
    {
        const auto &row = rows_[i];
        indexByKey_.emplace(row.key, static_cast<int>(i));

        auto groupIt = std::find_if(groups_.begin(), groups_.end(),
                                    [&](const QVariant &entry)
                                    {
                                        const auto map = entry.toMap();
                                        return map.value(QStringLiteral("name")).toString() == row.group
                                            && map.value(QStringLiteral("tab")).toInt() == row.tab
                                            && map.value(QStringLiteral("nodeId")).toString() == row.nodeId;
                                    });
        if (groupIt == groups_.end())
        {
            groups_.append(QVariantMap{
                {    "nodeId",      row.nodeId},
                {      "name",       row.group},
                {       "tab",         row.tab},
                {"parameters", QVariantList{}}
            });
            groupIt = std::prev(groups_.end());
        }
        auto map  = groupIt->toMap();
        auto list = map.value(QStringLiteral("parameters")).toList();
        list.append(QVariantMap{
            {         "key",                                             row.key},
            {          "id",                   row.map.value(QStringLiteral("id"))},
            {       "stage",                row.map.value(QStringLiteral("stage"))},
            {    "recordId",             row.map.value(QStringLiteral("recordId"))},
            {"recordScoped",         row.map.value(QStringLiteral("recordScoped"))},
            {   "datasetId",            row.map.value(QStringLiteral("datasetId"))}
        });
        map.insert(QStringLiteral("parameters"), list);
        *groupIt = map;
    }
}

} // namespace visionrt
