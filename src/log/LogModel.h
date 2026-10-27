#pragma once

#include "log/LogService.h"

#include <QAbstractListModel>
#include <QVector>
#include <cstddef>

namespace visionrt
{

// Bounded log window model. Records arrive in batches from the service; the model only inserts and
// trims, so a burst never rebuilds the list and the visible scroll position survives.
class LogModel final : public QAbstractListModel
{
    Q_OBJECT

public:
    enum Role
    {
        TimeRole = Qt::UserRole + 1,
        LevelRole,
        SourceRole,
        MessageRole
    };

    explicit LogModel(QObject *parent = nullptr);

    int      rowCount(const QModelIndex &parent = {}) const override;
    QVariant data(const QModelIndex &index, int role = Qt::DisplayRole) const override;
    QHash<int, QByteArray> roleNames() const override;

    Q_INVOKABLE void clear();
    // Plain text of one row, used by the window's copy action.
    Q_INVOKABLE QString text(int row) const;

    void append(const QVector<LogRecord> &records);
    void setCapacity(std::size_t capacity);

private:
    QVector<LogRecord> rows_;
    std::size_t        capacity_ = 2000;
};

} // namespace visionrt
