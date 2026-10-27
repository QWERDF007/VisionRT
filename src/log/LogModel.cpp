#include "log/LogModel.h"

#include <algorithm>

namespace visionrt
{
LogModel::LogModel(QObject *parent) : QAbstractListModel(parent)
{
    capacity_ = std::max<std::size_t>(1, LogService::instance().config().uiCapacity);
    connect(&LogService::instance(), &LogService::recordsAvailable, this, &LogModel::append);
}

int LogModel::rowCount(const QModelIndex &parent) const
{
    return parent.isValid() ? 0 : static_cast<int>(rows_.size());
}

QVariant LogModel::data(const QModelIndex &index, int role) const
{
    if (!index.isValid() || index.row() < 0 || static_cast<std::size_t>(index.row()) >= rows_.size())
        return {};
    const auto &record = rows_.at(index.row());
    switch (role)
    {
    case TimeRole: return record.time;
    case LevelRole: return record.level;
    case SourceRole: return record.source;
    case MessageRole: return record.message;
    default: return {};
    }
}

QHash<int, QByteArray> LogModel::roleNames() const
{
    return {{TimeRole, "time"}, {LevelRole, "level"}, {SourceRole, "source"}, {MessageRole, "message"}};
}

void LogModel::clear()
{
    if (rows_.isEmpty())
        return;
    beginResetModel();
    rows_.clear();
    endResetModel();
}

QString LogModel::text(int row) const
{
    if (row < 0 || static_cast<std::size_t>(row) >= rows_.size())
        return {};
    const auto &record = rows_.at(row);
    return record.time + QLatin1Char(' ') + record.level + QLatin1Char(' ') + record.source
           + QStringLiteral("：") + record.message;
}

void LogModel::append(const QVector<LogRecord> &records)
{
    if (records.isEmpty())
        return;
    std::size_t overflow = 0;
    if (rows_.size() + records.size() > capacity_)
        overflow = rows_.size() + records.size() - capacity_;
    if (overflow > 0)
    {
        beginRemoveRows({}, 0, static_cast<int>(overflow) - 1);
        rows_.remove(0, static_cast<int>(overflow));
        endRemoveRows();
    }
    const auto first = rows_.size();
    beginInsertRows({}, static_cast<int>(first), static_cast<int>(first + records.size()) - 1);
    for (const auto &record : records)
        rows_.push_back(record);
    endInsertRows();
}

void LogModel::setCapacity(std::size_t capacity)
{
    capacity_ = std::max<std::size_t>(1, capacity);
    if (rows_.size() > capacity_)
    {
        const auto overflow = rows_.size() - capacity_;
        beginRemoveRows({}, 0, static_cast<int>(overflow) - 1);
        rows_.remove(0, static_cast<int>(overflow));
        endRemoveRows();
    }
}

} // namespace visionrt
