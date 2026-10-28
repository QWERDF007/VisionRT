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
    if (!index.isValid() || index.row() < 0 || index.row() >= rows_.size())
        return {};
    const auto &record = rows_.at(index.row());
    switch (role)
    {
    case LineRole: return record.line;
    case LevelRole: return record.level;
    default: return {};
    }
}

QHash<int, QByteArray> LogModel::roleNames() const
{
    return {{LineRole, "line"}, {LevelRole, "level"}};
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
    if (row < 0 || row >= rows_.size())
        return {};
    return rows_.at(row).line;
}

void LogModel::append(const QVector<LogRecord> &records)
{
    if (records.isEmpty())
        return;
    const auto limit = static_cast<qsizetype>(capacity_);
    // The incoming batch is trimmed first: a batch larger than the window cannot ask the view to
    // remove rows that never existed, and the model never grows past its capacity even briefly.
    const auto incoming = records.size() > limit ? records.sliced(records.size() - limit) : records;
    const auto overflow = std::max<qsizetype>(0, rows_.size() + incoming.size() - limit);
    if (overflow > 0)
    {
        beginRemoveRows({}, 0, static_cast<int>(overflow) - 1);
        rows_.remove(0, overflow);
        endRemoveRows();
    }
    const auto first = rows_.size();
    beginInsertRows({}, static_cast<int>(first), static_cast<int>(first + incoming.size()) - 1);
    for (const auto &record : incoming)
        rows_.push_back(record);
    endInsertRows();
}

void LogModel::setCapacity(std::size_t capacity)
{
    capacity_ = std::max<std::size_t>(1, capacity);
    const auto overflow = rows_.size() - static_cast<qsizetype>(capacity_);
    if (overflow > 0)
    {
        beginRemoveRows({}, 0, static_cast<int>(overflow) - 1);
        rows_.remove(0, overflow);
        endRemoveRows();
    }
}

} // namespace visionrt
