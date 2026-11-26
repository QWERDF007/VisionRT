#include "ui/TrainingRecordModel.h"

#include <QVariantMap>
#include <algorithm>
#include <unordered_set>

namespace visionrt {

TrainingRecordModel::TrainingRecordModel(QObject *parent)
    : QAbstractListModel(parent)
{
}

void TrainingRecordModel::clear()
{
    if (rows_.empty())
        return;
    beginRemoveRows({}, 0, static_cast<int>(rows_.size()) - 1);
    rows_.clear();
    endRemoveRows();
}

void TrainingRecordModel::applyRowDiff(int idx, Row &&next)
{
    auto      &current = rows_[static_cast<std::size_t>(idx)];
    QList<int> changed;
    if (current.imageId != next.imageId)
    {
        current.imageId = std::move(next.imageId);
        changed.push_back(ImageIdRole);
    }
    if (current.imageUrl != next.imageUrl)
    {
        current.imageUrl = std::move(next.imageUrl);
        changed.push_back(ImageUrlRole);
    }
    if (current.regions != next.regions)
    {
        current.regions = next.regions;
        changed.push_back(RegionsRole);
    }
    if (current.selected != next.selected)
    {
        current.selected = next.selected;
        changed.push_back(SelectedRole);
    }
    if (current.fields != next.fields)
    {
        current.fields = std::move(next.fields);
        changed.push_back(FieldsRole);
    }
    if (!changed.isEmpty())
    {
        const auto modelIdx = index(idx);
        emit dataChanged(modelIdx, modelIdx, changed);
    }
}

void TrainingRecordModel::sync(std::vector<Row> nextRows)
{
    if (nextRows.empty())
    {
        clear();
        return;
    }

    // Fast path: identical recordId order -> only emit dataChanged for modified rows/roles.
    if (nextRows.size() == rows_.size()
        && std::equal(rows_.begin(), rows_.end(), nextRows.begin(),
                      [](const Row &a, const Row &b) { return a.recordId == b.recordId; }))
    {
        for (std::size_t i = 0; i < rows_.size(); ++i)
            applyRowDiff(static_cast<int>(i), std::move(nextRows[i]));
        return;
    }

    std::unordered_set<QString> targetIds;
    targetIds.reserve(nextRows.size());
    for (const auto &row : nextRows)
        targetIds.insert(row.recordId);

    for (int i = static_cast<int>(rows_.size()) - 1; i >= 0; --i)
    {
        if (!targetIds.contains(rows_[static_cast<std::size_t>(i)].recordId))
        {
            beginRemoveRows({}, i, i);
            rows_.erase(rows_.begin() + i);
            endRemoveRows();
        }
    }

    for (std::size_t target = 0; target < nextRows.size(); ++target)
    {
        const int idx = static_cast<int>(target);
        if (target < rows_.size() && rows_[target].recordId == nextRows[target].recordId)
        {
            applyRowDiff(idx, std::move(nextRows[target]));
            continue;
        }
        auto existing = std::find_if(rows_.begin() + static_cast<std::ptrdiff_t>(target), rows_.end(),
                                     [&](const Row &row) { return row.recordId == nextRows[target].recordId; });
        if (existing != rows_.end())
        {
            const int from = static_cast<int>(existing - rows_.begin());
            beginMoveRows({}, from, from, {}, idx);
            auto moved = std::move(*existing);
            rows_.erase(existing);
            rows_.insert(rows_.begin() + idx, std::move(moved));
            endMoveRows();
            applyRowDiff(idx, std::move(nextRows[target]));
        }
        else
        {
            beginInsertRows({}, idx, idx);
            rows_.insert(rows_.begin() + idx, std::move(nextRows[target]));
            endInsertRows();
        }
    }
}

QVariantList TrainingRecordModel::snapshot() const
{
    QVariantList result;
    result.reserve(static_cast<qsizetype>(rows_.size()));
    for (const auto &row : rows_)
    {
        result.append(QVariantMap{
            {      "id", row.recordId},
            { "imageId",  row.imageId},
            {"imageUrl", row.imageUrl},
            { "regions",  row.regions},
            {"selected", row.selected},
            {  "fields",   row.fields}
        });
    }
    return result;
}

int TrainingRecordModel::rowCount(const QModelIndex &parent) const
{
    return parent.isValid() ? 0 : static_cast<int>(rows_.size());
}

QVariant TrainingRecordModel::data(const QModelIndex &index, int role) const
{
    if (!index.isValid() || index.row() < 0 || index.row() >= rowCount())
        return {};
    const auto &row = rows_[static_cast<std::size_t>(index.row())];
    switch (role)
    {
    case RecordIdRole:
        return row.recordId;
    case ImageIdRole:
        return row.imageId;
    case ImageUrlRole:
        return row.imageUrl;
    case RegionsRole:
        return row.regions;
    case SelectedRole:
        return row.selected;
    case FieldsRole:
        return row.fields;
    default:
        return {};
    }
}

QHash<int, QByteArray> TrainingRecordModel::roleNames() const
{
    return {
        {RecordIdRole, "recordId"},
        { ImageIdRole,  "imageId"},
        {ImageUrlRole, "imageUrl"},
        { RegionsRole,  "regions"},
        {SelectedRole, "selected"},
        {  FieldsRole,   "fields"}
    };
}

} // namespace visionrt
