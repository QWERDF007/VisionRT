#pragma once

#include "core/Project.h"

#include <QAbstractListModel>
#include <QVariantList>

#include <map>
#include <vector>

namespace visionrt {

// One row per node of the current flow. The model owns the presentation row, not the project: it
// never stores a second NodeDefinition and every value is produced by the controller on change.
class NodeListModel final : public QAbstractListModel
{
    Q_OBJECT
public:
    enum Role
    {
        NodeIdRole = Qt::UserRole + 1,
        TitleRole,
        AliasRole,
        ToolIdRole,
        VersionRole,
        CategoryRole,
        PosXRole,
        PosYRole,
        StateRole,
        DispositionRole,
        ExecutedRole,
        StaleRole,
        TimingTextRole,
        TimingPhasesRole,
        MissingRole,
        DefMissingRole,
        ImplUnavailableRole,
        StatusCodeRole,
        StatusMessageRole,
        StatusDetailRole,
        LifecycleMessageRole,
        LifecycleDetailRole,
        RowIndexRole
    };

    struct Row
    {
        QString      id;
        QString      title;
        QString      alias;
        QString      toolId;
        QString      version;
        QString      category;
        double       x = 0;
        double       y = 0;
        QString      state;
        QString      disposition;
        bool         executed        = false;
        bool         stale           = false;
        QString      timingText;
        QVariantList timingPhases;
        bool         missing          = false;
        bool         defMissing       = false;
        bool         implUnavailable  = false;
        std::int64_t statusCode       = 0;
        QString      statusMessage;
        QString      statusDetail;
        QString      lifecycleMessage;
        QString      lifecycleDetail;
    };

    explicit NodeListModel(QObject *parent = nullptr);

    int      rowCount(const QModelIndex &parent = {}) const override;
    QVariant data(const QModelIndex &index, int role = Qt::DisplayRole) const override;
    QHash<int, QByteArray> roleNames() const override;

    // Project replacement or flow switch: the whole view changes identity, so a reset is correct.
    void reset(std::vector<Row> rows);
    // Adds, removes and reorders rows with the matching model notifications, then updates every
    // surviving row in place. Returns true when the row set changed.
    bool sync(std::vector<Row> rows);
    // One node changed: only the roles that actually differ are reported.
    bool update(Row row);
    bool remove(const QString &id);

    int           rowOf(const QString &id) const;
    const Row    *rowAt(int row) const;
    QVariantList  snapshot() const;
    QVariantMap   rowMap(const QString &id) const;
    bool          empty() const noexcept { return rows_.empty(); }

private:
    static QVariant value(const Row &row, int role);
    static bool     sameValue(const Row &left, const Row &right, int role);
    // Structural changes shift every later row; the table is rebuilt instead of patched so a stale
    // index can never address a row that no longer exists.
    void            rebuildIndex();

    std::vector<Row>                       rows_;
    std::map<QString, int>                 index_;
};

} // namespace visionrt
