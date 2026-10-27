#include "ui/ToolTreeModel.h"

#include <algorithm>

namespace visionrt {
ToolTreeModel::ToolTreeModel(QObject *parent)
    : QAbstractItemModel(parent)
{
}

void ToolTreeModel::setRegistry(const ToolRegistry &registry)
{
    definitions_.clear();
    for (const auto &tool : registry.definitions())
    {
        // Assist helpers are driven by the interaction mapping and never appear
        // in the ordinary tool menu.
        if (tool.visibility == ToolVisibility::Assist)
            continue;
        const auto availability = registry.availability(tool.id);
        definitions_.push_back({QString::fromStdString(tool.category), QString::fromStdString(tool.subcategory),
                                QString::fromStdString(tool.displayName), QString::fromStdString(tool.id),
                                QString::fromStdString(tool.version),
                                QString::fromStdString(availability.status.message),
                                availability.state == ToolAvailability::Available});
    }
    rebuild();
}

void ToolTreeModel::setFilter(const QString &text)
{
    if (filter_ == text.trimmed())
        return;
    filter_ = text.trimmed();
    rebuild();
}

bool ToolTreeModel::updateToolStatus(const QString &toolId, bool available, const QString &status)
{
    bool changed = false;
    for (auto &def : definitions_)
    {
        if (def.id == toolId)
        {
            if (def.available != available || def.status != status)
            {
                def.available = available;
                def.status    = status;
                changed       = true;
            }
            break;
        }
    }

    for (int catRow = 0; catRow < static_cast<int>(root_.children.size()); ++catRow)
    {
        auto &cat = *root_.children[catRow];
        for (int subRow = 0; subRow < static_cast<int>(cat.children.size()); ++subRow)
        {
            auto &sub = *cat.children[subRow];
            for (int toolRow = 0; toolRow < static_cast<int>(sub.children.size()); ++toolRow)
            {
                auto &tool = *sub.children[toolRow];
                if (tool.id == toolId)
                {
                    if (tool.available == available && tool.status == status)
                        return changed;
                    tool.available = available;
                    tool.status    = status;
                    const auto parentIdx = index(subRow, 0, index(catRow, 0, {}));
                    const auto toolIdx   = index(toolRow, 0, parentIdx);
                    emit dataChanged(toolIdx, toolIdx, {AvailableRole, StatusRole});
                    return true;
                }
            }
        }
    }
    return changed;
}

void ToolTreeModel::rebuild()
{
    beginResetModel();
    root_.children.clear();
    auto group = [](Entry &parent, const QString &name) -> Entry &
    {
        for (auto &child : parent.children)
            if (child->name == name)
                return *child;
        auto child    = std::make_unique<Entry>();
        child->name   = name;
        child->parent = &parent;
        parent.children.push_back(std::move(child));
        return *parent.children.back();
    };
    for (const auto &definition : definitions_)
    {
        if (!filter_.isEmpty() && !definition.name.contains(filter_, Qt::CaseInsensitive))
            continue;
        auto &category   = group(root_, definition.category.isEmpty() ? QStringLiteral("未分类") : definition.category);
        auto &subgroup   = group(category, definition.group.isEmpty() ? QStringLiteral("通用") : definition.group);
        auto  child      = std::make_unique<Entry>();
        child->name      = definition.name;
        child->id        = definition.id;
        child->version   = definition.version;
        child->status    = definition.status;
        child->available = definition.available;
        child->parent    = &subgroup;
        subgroup.children.push_back(std::move(child));
    }
    endResetModel();
}

QModelIndex ToolTreeModel::index(int row, int column, const QModelIndex &parent) const
{
    if (column != 0 || row < 0 || (parent.isValid() && parent.column() != 0))
        return {};
    const auto *entry = parent.isValid() ? static_cast<Entry *>(parent.internalPointer()) : &root_;
    if (static_cast<std::size_t>(row) >= entry->children.size())
        return {};
    return createIndex(row, column, entry->children[row].get());
}

QModelIndex ToolTreeModel::parent(const QModelIndex &index) const
{
    if (!index.isValid())
        return {};
    auto *parent = static_cast<Entry *>(index.internalPointer())->parent;
    if (!parent || parent == &root_)
        return {};
    const auto &siblings = parent->parent->children;
    for (std::size_t row = 0; row < siblings.size(); ++row)
        if (siblings[row].get() == parent)
            return createIndex(static_cast<int>(row), 0, parent);
    return {};
}

int ToolTreeModel::rowCount(const QModelIndex &parent) const
{
    if (parent.isValid() && parent.column() != 0)
        return 0;
    return static_cast<int>(
        (parent.isValid() ? static_cast<Entry *>(parent.internalPointer()) : &root_)->children.size());
}

int ToolTreeModel::columnCount(const QModelIndex &) const
{
    return 1;
}

QVariant ToolTreeModel::data(const QModelIndex &index, int role) const
{
    if (!index.isValid())
        return {};
    const auto &entry = *static_cast<Entry *>(index.internalPointer());
    switch (role)
    {
    case Qt::DisplayRole:
        return entry.name;
    case ToolIdRole:
        return entry.id;
    case LeafRole:
        return !entry.id.isEmpty();
    case VersionRole:
        return entry.version;
    case AvailableRole:
        return entry.available;
    case StatusRole:
        return entry.status;
    case StableKeyRole:
        if (!entry.id.isEmpty())
            return entry.id;
        return entry.parent == &root_ ? entry.name : entry.parent->name + "/" + entry.name;
    default:
        return {};
    }
}

QHash<int, QByteArray> ToolTreeModel::roleNames() const
{
    return {
        {Qt::DisplayRole,   "display"},
        {     ToolIdRole,    "toolId"},
        {       LeafRole,      "leaf"},
        {    VersionRole,   "version"},
        {  AvailableRole, "available"},
        {     StatusRole,    "status"},
        {  StableKeyRole, "stableKey"}
    };
}

Qt::ItemFlags ToolTreeModel::flags(const QModelIndex &index) const
{
    if (!index.isValid())
        return Qt::NoItemFlags;
    const auto &entry = *static_cast<Entry *>(index.internalPointer());
    auto        flags = Qt::ItemIsEnabled | Qt::ItemIsSelectable;
    if (!entry.id.isEmpty() && entry.available)
        flags |= Qt::ItemIsDragEnabled;
    return flags;
}
} // namespace visionrt
