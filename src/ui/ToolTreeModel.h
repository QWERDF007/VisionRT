#pragma once
#include "core/ToolRegistry.h"

#include <QAbstractItemModel>
#include <QString>
#include <memory>
#include <vector>

namespace visionrt {
class ToolTreeModel final : public QAbstractItemModel
{
    Q_OBJECT
public:
    enum Role
    {
        ToolIdRole = Qt::UserRole + 1,
        LeafRole,
        VersionRole,
        AvailableRole,
        StableKeyRole
    };

    explicit ToolTreeModel(QObject *parent = nullptr);
    void                   setRegistry(const ToolRegistry &registry);
    void                   setFilter(const QString &text);
    QModelIndex            index(int row, int column, const QModelIndex &parent = {}) const override;
    QModelIndex            parent(const QModelIndex &index) const override;
    int                    rowCount(const QModelIndex &parent = {}) const override;
    int                    columnCount(const QModelIndex &parent = {}) const override;
    QVariant               data(const QModelIndex &index, int role = Qt::DisplayRole) const override;
    QHash<int, QByteArray> roleNames() const override;
    Qt::ItemFlags          flags(const QModelIndex &index) const override;

private:
    struct Entry
    {
        QString                             name, id, version;
        bool                                available = false;
        Entry                              *parent    = nullptr;
        std::vector<std::unique_ptr<Entry>> children;
    };

    struct Definition
    {
        QString category, group, name, id, version;
        bool    available;
    };

    void                    rebuild();
    Entry                   root_;
    std::vector<Definition> definitions_;
    QString                 filter_;
};
} // namespace visionrt
