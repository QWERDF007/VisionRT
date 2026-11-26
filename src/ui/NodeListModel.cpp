#include "ui/NodeListModel.h"

#include <algorithm>

namespace visionrt {
NodeListModel::NodeListModel(QObject *parent)
    : QAbstractListModel(parent)
{
    holdTimer_.setSingleShot(true);
    connect(&holdTimer_, &QTimer::timeout, this, [this] { releaseExpiredHolds(); });
}

void NodeListModel::setRunningHoldMs(int milliseconds)
{
    runningHoldMs_ = std::max(0, milliseconds);
    if (runningHoldMs_ == 0)
        releaseExpiredHolds();
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
    case ToolNameRole: return row.toolName;
    case VersionRole: return row.version;
    case CategoryRole: return row.category;
    case PosXRole: return row.x;
    case PosYRole: return row.y;
    case StateRole: return static_cast<int>(row.state);
    case DisplayStateRole: return static_cast<int>(row.displayState);
    case DispositionRole: return static_cast<int>(row.disposition);
    case ExecutedRole: return row.executed;
    case StaleRole: return row.stale;
    case TimingTextRole: return row.timingText;
    case TimingPhasesRole: return row.timingPhases;
    case MissingRole: return row.missing;
    case DefMissingRole: return row.defMissing;
    case ImplUnavailableRole: return row.implUnavailable;
    case StatusCodeRole: return row.statusCode;
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
    case ToolNameRole: return left.toolName == right.toolName;
    case VersionRole: return left.version == right.version;
    case CategoryRole: return left.category == right.category;
    case PosXRole: return left.x == right.x;
    case PosYRole: return left.y == right.y;
    case StateRole: return left.state == right.state;
    case DisplayStateRole: return left.displayState == right.displayState;
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
        {      ToolNameRole,   "nodeToolName"},
        {       VersionRole,    "nodeVersion"},
        {      CategoryRole,   "nodeCategory"},
        {          PosXRole,        "nodeX"},
        {          PosYRole,        "nodeY"},
        {         StateRole,        "nodeStatus"},
        {  DisplayStateRole, "nodeDisplayStatus"},
        {   DispositionRole,   "nodeDisposition"},
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
    holdTimer_.stop();
    holds_.clear();
    const auto now = Clock::now();
    for (auto &item : rows)
    {
        item.displayState = item.state;
        if (!item.missing && item.state == NodeState::Running)
            holds_[item.id] = {now, {}, NodeState::Running, false};
    }
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
            holds_.erase(id);
            beginRemoveRows({}, row, row);
            rows_.erase(rows_.begin() + row);
            endRemoveRows();
        }
    }
    // Every row after a removal shifted down, so the lookup table is rebuilt instead of patched.
    rebuildIndex();
    // 2. Insert new rows at the position the flow declares.
    const auto now = Clock::now();
    for (int row = 0; row < static_cast<int>(rows.size()); ++row)
    {
        if (index_.contains(rows[static_cast<std::size_t>(row)].id))
            continue;
        auto inserted = rows[static_cast<std::size_t>(row)];
        inserted.displayState = inserted.state;
        if (!inserted.missing && inserted.state == NodeState::Running)
            holds_[inserted.id] = {now, {}, NodeState::Running, false};
        beginInsertRows({}, row, row);
        rows_.insert(rows_.begin() + row, std::move(inserted));
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
    scheduleHoldTimer();
    return structure;
}

void NodeListModel::rebuildIndex()
{
    index_.clear();
    for (int row = 0; row < static_cast<int>(rows_.size()); ++row)
        index_[rows_[static_cast<std::size_t>(row)].id] = row;
}

NodeState NodeListModel::resolveDisplayState(const Row &current, const Row &incoming)
{
    const auto now = Clock::now();
    if (incoming.missing)
    {
        holds_.erase(incoming.id);
        return incoming.state;
    }
    if (incoming.state == NodeState::Running)
    {
        auto &hold = holds_[incoming.id];
        if (!hold.holding && current.displayState != NodeState::Running)
            hold.enteredRunningAt = now;
        else if (hold.enteredRunningAt == Clock::time_point{})
            hold.enteredRunningAt = now;
        hold.holding      = false;
        hold.pendingState = NodeState::Running;
        return NodeState::Running;
    }
    if (incoming.state == NodeState::Failed || incoming.state == NodeState::Canceled)
    {
        holds_.erase(incoming.id);
        return incoming.state;
    }
    if (current.displayState == NodeState::Running && runningHoldMs_ > 0)
    {
        auto &hold = holds_[incoming.id];
        if (hold.enteredRunningAt == Clock::time_point{})
            hold.enteredRunningAt = now;
        const auto deadline = hold.enteredRunningAt + std::chrono::milliseconds(runningHoldMs_);
        if (now < deadline)
        {
            hold.holding      = true;
            hold.holdUntil    = deadline;
            hold.pendingState = incoming.state;
            return NodeState::Running;
        }
    }
    holds_.erase(incoming.id);
    return incoming.state;
}

void NodeListModel::scheduleHoldTimer()
{
    const auto now = Clock::now();
    bool       any = false;
    Clock::time_point earliest{};
    for (const auto &[id, hold] : holds_)
    {
        if (!hold.holding)
            continue;
        if (!any || hold.holdUntil < earliest)
        {
            earliest = hold.holdUntil;
            any      = true;
        }
    }
    if (!any)
    {
        holdTimer_.stop();
        return;
    }
    const auto remaining = std::chrono::duration_cast<std::chrono::milliseconds>(earliest - now).count();
    holdTimer_.start(static_cast<int>(std::max<std::int64_t>(1, remaining)));
}

void NodeListModel::releaseExpiredHolds()
{
    const auto now = Clock::now();
    std::vector<QString> released;
    for (auto &[id, hold] : holds_)
    {
        if (!hold.holding)
            continue;
        if (runningHoldMs_ > 0 && now < hold.holdUntil)
            continue;
        const int position = rowOf(id);
        if (position >= 0 && position < static_cast<int>(rows_.size()))
        {
            auto &current = rows_[static_cast<std::size_t>(position)];
            const NodeState target = current.missing ? current.state : hold.pendingState;
            if (current.displayState != target)
            {
                current.displayState = target;
                const auto idx = index(position);
                emit dataChanged(idx, idx, {DisplayStateRole});
            }
        }
        released.push_back(id);
    }
    for (const auto &id : released)
        holds_.erase(id);
    scheduleHoldTimer();
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
    Row      &current = rows_[static_cast<std::size_t>(position)];
    row.displayState  = resolveDisplayState(current, row);
    scheduleHoldTimer();
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
        case ToolNameRole: current.toolName = row.toolName; break;
        case AliasRole: current.alias = row.alias; break;
        case VersionRole: current.version = row.version; break;
        case CategoryRole: current.category = row.category; break;
        case PosXRole: current.x = row.x; break;
        case PosYRole: current.y = row.y; break;
        case StateRole: current.state = row.state; break;
        case DisplayStateRole: current.displayState = row.displayState; break;
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
    holds_.erase(id);
    const int row = found->second;
    beginRemoveRows({}, row, row);
    rows_.erase(rows_.begin() + row);
    endRemoveRows();
    rebuildIndex();
    scheduleHoldTimer();
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
        {             "toolName", item.toolName},
        {              "version", item.version},
        {             "category", item.category},
        {                    "x", item.x},
        {                    "y", item.y},
        {                "state", static_cast<int>(item.state)},
        {          "disposition", static_cast<int>(item.disposition)},
        {             "executed", item.executed},
        {                "stale", item.stale},
        {           "timingText", item.timingText},
        {         "timingPhases", item.timingPhases},
        {              "missing", item.missing},
        {           "defMissing", item.defMissing},
        {      "implUnavailable", item.implUnavailable},
        {           "statusCode", item.statusCode},
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
