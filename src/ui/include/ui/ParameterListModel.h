#pragma once

#include "ui/Export.h"

#include <QAbstractListModel>
#include <QString>
#include <QVariantList>
#include <QVariantMap>
#include <unordered_map>
#include <vector>

namespace visionrt {

class VISIONRT_UI_API ParameterListModel final : public QAbstractListModel
{
    Q_OBJECT
public:
    enum Role
    {
        KeyRole = Qt::UserRole + 1,
        ParameterRole,
        GroupRole,
        TabRole,
        NodeIdRole
    };

    struct Row
    {
        QString     key;
        QString     nodeId;
        QString     group;
        int         tab = 0;
        QVariantMap map;
    };

    explicit ParameterListModel(QObject *parent = nullptr);

    static QString makeKey(const QString &nodeId, int stage, bool recordScoped, const QVariant &datasetId,
                           const QString &recordId, int id);

    void clear();
    void sync(std::vector<Row> nextRows);
    bool updateRowByKey(const QString &key, const QVariantMap &updatedMap);

    Q_INVOKABLE QVariantMap  rowByKey(const QString &key) const;
    Q_INVOKABLE int          rowIndexOf(const QString &key) const;
    Q_INVOKABLE QVariantList groupsForTab(int tab) const;

    QVariantList snapshot() const;
    QVariantList groupsSnapshot() const;

    int                    rowCount(const QModelIndex &parent = {}) const override;
    QVariant               data(const QModelIndex &index, int role = Qt::DisplayRole) const override;
    QHash<int, QByteArray> roleNames() const override;

signals:
    void groupsChanged();

private:
    void rebuildIndexAndGroups();

    std::vector<Row>             rows_;
    std::unordered_map<QString, int> indexByKey_;
    QVariantList                 groups_;
};

} // namespace visionrt
