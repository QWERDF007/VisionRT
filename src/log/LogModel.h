#pragma once

#include "log/LogService.h"

#include <QAbstractListModel>
#include <QVector>
#include <cstddef>

namespace visionrt
{

// Bounded log window model. Every row is one finished line - the same text the file received - so
// the model neither re-composes nor re-columns a record. Records arrive in batches and the model
// only inserts and trims, so a burst never rebuilds the list and the scroll position survives.
class LogModel final : public QAbstractListModel
{
    Q_OBJECT
    // Read-only row count so the window can disable its actions on an empty history.
    Q_PROPERTY(int count READ count NOTIFY countChanged)

public:
    enum Role
    {
        LineRole = Qt::UserRole + 1,
        LevelRole
    };

    explicit LogModel(QObject *parent = nullptr);

    int      rowCount(const QModelIndex &parent = {}) const override;
    QVariant data(const QModelIndex &index, int role = Qt::DisplayRole) const override;
    QHash<int, QByteArray> roleNames() const override;

    Q_INVOKABLE void clear();
    // Plain text of one row, used by the window's copy action.
    Q_INVOKABLE QString text(int row) const;
    // Every retained line joined by '\n', in the order the window shows them.
    Q_INVOKABLE QString allText() const;
    int                 count() const;

    void append(const QVector<LogRecord> &records);
    void setCapacity(std::size_t capacity);

signals:
    void countChanged();

private:
    QVector<LogRecord> rows_;
    std::size_t        capacity_ = 2000;
};

} // namespace visionrt
