#pragma once
#include "core/ToolRegistry.h"

#include <QAbstractItemModel>
#include <QString>
#include <QStringList>
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
        StatusRole,
        StableKeyRole
    };

    explicit ToolTreeModel(QObject *parent = nullptr);
    void                   setRegistry(const ToolRegistry &registry);
    void                   setFilter(const QString &text);
    bool                   updateToolStatus(const QString &toolId, bool available, const QString &status);
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
        QString                             name, id, version, status;
        bool                                available = false;
        Entry                              *parent    = nullptr;
        std::vector<std::unique_ptr<Entry>> children;
    };

    struct Definition
    {
        // Folder names below the declaration root; the tree nests one level per entry.
        QStringList categories;
        QString     name, id, version, status;
        bool        available;
    };

    static Entry *findById(Entry &entry, const QString &id);
    static Entry &groupEntry(Entry &parent, const QString &name);
    void          rebuild();
    Entry         root_;
    std::vector<Definition> definitions_;
    QString                 filter_;
};
} // namespace visionrt
