#include "ui/NodeListModel.h"

#include <algorithm>

namespace visionrt {
NodeListModel::NodeListModel(QObject *parent)
    : QAbstractListModel(parent)
{
}

int NodeListModel::rowCount(const QModelIndex &parent) const
{
    return parent.isValid() ? 0 : static_cast<int>(rows_.size());
}

QVariant NodeListModel::value(const Row &row, int role)
{
    switch (role)
    {
    case NodeIdRole: return row.id;
    case TitleRole: return row.title;
    case AliasRole: return row.alias;
    case ToolIdRole: return row.toolId;
    case VersionRole: return row.version;
    case CategoryRole: return row.category;
    case PosXRole: return row.x;
    case PosYRole: return row.y;
    case StateRole: return row.state;
    case DispositionRole: return row.disposition;
    case ExecutedRole: return row.executed;
    case StaleRole: return row.stale;
    case TimingTextRole: return row.timingText;
    case TimingPhasesRole: return row.timingPhases;
    case MissingRole: return row.missing;
    case DefMissingRole: return row.defMissing;
    case ImplUnavailableRole: return row.implUnavailable;
    case StatusCodeRole: return static_cast<qlonglong>(row.statusCode);
    case StatusMessageRole: return row.statusMessage;
    case StatusDetailRole: return row.statusDetail;
    case LifecycleMessageRole: return row.lifecycleMessage;
    case LifecycleDetailRole: return row.lifecycleDetail;
    case RowIndexRole: return 0;
    default: return {};
    }
}

bool NodeListModel::sameValue(const Row &left, const Row &right, int role)
{
    switch (role)
    {
    case NodeIdRole: return left.id == right.id;
    case TitleRole: return left.title == right.title;
    case AliasRole: return left.alias == right.alias;
    case ToolIdRole: return left.toolId == right.toolId;
    case VersionRole: return left.version == right.version;
    case CategoryRole: return left.category == right.category;
    case PosXRole: return left.x == right.x;
    case PosYRole: return left.y == right.y;
    case StateRole: return left.state == right.state;
    case DispositionRole: return left.disposition == right.disposition;
    case ExecutedRole: return left.executed == right.executed;
    case StaleRole: return left.stale == right.stale;
    case TimingTextRole: return left.timingText == right.timingText;
    case TimingPhasesRole: return left.timingPhases == right.timingPhases;
    case MissingRole: return left.missing == right.missing;
    case DefMissingRole: return left.defMissing == right.defMissing;
    case ImplUnavailableRole: return left.implUnavailable == right.implUnavailable;
    case StatusCodeRole: return left.statusCode == right.statusCode;
    case StatusMessageRole: return left.statusMessage == right.statusMessage;
    case StatusDetailRole: return left.statusDetail == right.statusDetail;
    case LifecycleMessageRole: return left.lifecycleMessage == right.lifecycleMessage;
    case LifecycleDetailRole: return left.lifecycleDetail == right.lifecycleDetail;
    default: return true;
    }
}

QVariant NodeListModel::data(const QModelIndex &index, int role) const
{
    if (!index.isValid() || index.row() < 0 || index.row() >= static_cast<int>(rows_.size()))
        return {};
    if (role == RowIndexRole)
        return index.row();
    return value(rows_[static_cast<std::size_t>(index.row())], role);
}

QHash<int, QByteArray> NodeListModel::roleNames() const
{
    return {
        {        NodeIdRole,         "nodeId"},
        {         TitleRole,      "nodeTitle"},
        {         AliasRole,      "nodeAlias"},
        {        ToolIdRole,     "nodeToolId"},
        {       VersionRole,    "nodeVersion"},
        {      CategoryRole,   "nodeCategory"},
        {          PosXRole,        "nodeX"},
        {          PosYRole,        "nodeY"},
        {         StateRole,     "nodeStatus"},
        {   DispositionRole,"nodeDisposition"},
        {      ExecutedRole,   "nodeExecuted"},
        {         StaleRole,      "nodeStale"},
        {    TimingTextRole, "nodeTimingText"},
        {  TimingPhasesRole,"nodeTimingPhases"},
        {       MissingRole,    "nodeMissing"},
        {    DefMissingRole, "nodeDefMissing"},
        {ImplUnavailableRole,"nodeImplUnavailable"},
        {      StatusCodeRole, "nodeStatusCode"},
        {   StatusMessageRole,"nodeStatusMessage"},
        {    StatusDetailRole, "nodeStatusDetail"},
        {LifecycleMessageRole,"nodeLifecycleMessage"},
        { LifecycleDetailRole,"nodeLifecycleDetail"},
    };
}

int NodeListModel::rowOf(const QString &id) const
{
    const auto found = index_.find(id);
    return found == index_.end() ? -1 : found->second;
}

const NodeListModel::Row *NodeListModel::rowAt(int row) const
{
    if (row < 0 || row >= static_cast<int>(rows_.size()))
        return nullptr;
    return &rows_[static_cast<std::size_t>(row)];
}

void NodeListModel::reset(std::vector<Row> rows)
{
    beginResetModel();
    rows_ = std::move(rows);
    index_.clear();
    for (int row = 0; row < static_cast<int>(rows_.size()); ++row)
        index_[rows_[static_cast<std::size_t>(row)].id] = row;
    endResetModel();
}

bool NodeListModel::sync(std::vector<Row> rows)
{
    // 1. Drop rows the flow no longer has. Removing from the tail keeps earlier indices valid.
    for (int row = static_cast<int>(rows_.size()) - 1; row >= 0; --row)
    {
        const auto &id   = rows_[static_cast<std::size_t>(row)].id;
        const bool  keep = std::any_of(rows.begin(), rows.end(), [&](const auto &item) { return item.id == id; });
        if (!keep)
        {
            beginRemoveRows({}, row, row);
            rows_.erase(rows_.begin() + row);
            endRemoveRows();
        }
    }
    // Every row after a removal shifted down, so the lookup table is rebuilt instead of patched.
    rebuildIndex();
    // 2. Insert new rows at the position the flow declares.
    for (int row = 0; row < static_cast<int>(rows.size()); ++row)
    {
        if (index_.contains(rows[static_cast<std::size_t>(row)].id))
            continue;
        beginInsertRows({}, row, row);
        rows_.insert(rows_.begin() + row, rows[static_cast<std::size_t>(row)]);
        endInsertRows();
        rebuildIndex();
    }
    // 3. Reorder survivors so the model order matches the flow order.
    bool structure = false;
    for (int row = 0; row < static_cast<int>(rows.size()); ++row)
    {
        const auto &wanted = rows[static_cast<std::size_t>(row)].id;
        if (row >= static_cast<int>(rows_.size()) || rows_[static_cast<std::size_t>(row)].id == wanted)
            continue;
        const auto found   = index_.find(wanted);
        if (found == index_.end())
            continue;
        const int current = found->second;
        if (current <= row)
            continue;
        if (!beginMoveRows({}, current, current, {}, row))
            continue;
        Row moved = std::move(rows_[static_cast<std::size_t>(current)]);
        rows_.erase(rows_.begin() + current);
        rows_.insert(rows_.begin() + row, std::move(moved));
        endMoveRows();
        rebuildIndex();
        structure = true;
    }
    // 4. Refresh the surviving row contents; only differing roles are reported.
    for (int row = 0; row < static_cast<int>(rows.size()); ++row)
        update(std::move(rows[static_cast<std::size_t>(row)]));
    return structure;
}

void NodeListModel::rebuildIndex()
{
    index_.clear();
    for (int row = 0; row < static_cast<int>(rows_.size()); ++row)
        index_[rows_[static_cast<std::size_t>(row)].id] = row;
}

bool NodeListModel::update(Row row)
{
    const auto found = index_.find(row.id);
    if (found == index_.end())
        return false;
    const int position = found->second;
    // A stale index would write past the row vector, so the row is checked before it is touched.
    if (position < 0 || position >= static_cast<int>(rows_.size())
        || rows_[static_cast<std::size_t>(position)].id != row.id)
    {
        rebuildIndex();
        return false;
    }
    Row      &current  = rows_[static_cast<std::size_t>(position)];
    QList<int> changed;
    for (int role = NodeIdRole; role <= LifecycleDetailRole; ++role)
        if (!sameValue(current, row, role))
            changed.push_back(role);
    if (changed.isEmpty())
        return false;
    for (int role : changed)
    {
        switch (role)
        {
        case TitleRole: current.title = row.title; break;
        case AliasRole: current.alias = row.alias; break;
        case VersionRole: current.version = row.version; break;
        case CategoryRole: current.category = row.category; break;
        case PosXRole: current.x = row.x; break;
        case PosYRole: current.y = row.y; break;
        case StateRole: current.state = row.state; break;
        case DispositionRole: current.disposition = row.disposition; break;
        case ExecutedRole: current.executed = row.executed; break;
        case StaleRole: current.stale = row.stale; break;
        case TimingTextRole: current.timingText = row.timingText; break;
        case TimingPhasesRole: current.timingPhases = row.timingPhases; break;
        case MissingRole: current.missing = row.missing; break;
        case DefMissingRole: current.defMissing = row.defMissing; break;
        case ImplUnavailableRole: current.implUnavailable = row.implUnavailable; break;
        case StatusCodeRole: current.statusCode = row.statusCode; break;
        case StatusMessageRole: current.statusMessage = row.statusMessage; break;
        case StatusDetailRole: current.statusDetail = row.statusDetail; break;
        case LifecycleMessageRole: current.lifecycleMessage = row.lifecycleMessage; break;
        case LifecycleDetailRole: current.lifecycleDetail = row.lifecycleDetail; break;
        default: break;
        }
    }
    const auto first = index(position);
    emit       dataChanged(first, first, changed);
    return true;
}

bool NodeListModel::remove(const QString &id)
{
    const auto found = index_.find(id);
    if (found == index_.end())
        return false;
    const int row = found->second;
    beginRemoveRows({}, row, row);
    rows_.erase(rows_.begin() + row);
    endRemoveRows();
    rebuildIndex();
    return true;
}

QVariantMap NodeListModel::rowMap(const QString &id) const
{
    const int row = rowOf(id);
    if (row < 0)
        return {};
    const auto &item = rows_[static_cast<std::size_t>(row)];
    return {
        {                   "id", item.id},
        {                "title", item.title},
        {                "alias", item.alias},
        {               "toolId", item.toolId},
        {              "version", item.version},
        {             "category", item.category},
        {                    "x", item.x},
        {                    "y", item.y},
        {                "state", item.state},
        {          "disposition", item.disposition},
        {             "executed", item.executed},
        {                "stale", item.stale},
        {           "timingText", item.timingText},
        {         "timingPhases", item.timingPhases},
        {              "missing", item.missing},
        {           "defMissing", item.defMissing},
        {      "implUnavailable", item.implUnavailable},
        {           "statusCode", static_cast<qlonglong>(item.statusCode)},
        {        "statusMessage", item.statusMessage},
        {         "statusDetail", item.statusDetail},
        {     "lifecycleMessage", item.lifecycleMessage},
        {      "lifecycleDetail", item.lifecycleDetail}
    };
}

QVariantList NodeListModel::snapshot() const
{
    QVariantList result;
    result.reserve(static_cast<qsizetype>(rows_.size()));
    for (const auto &row : rows_)
        result.push_back(rowMap(row.id));
    return result;
}

} // namespace visionrt
