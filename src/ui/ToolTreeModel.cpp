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
        const auto availability = registry.availability(tool.nameEn);
        QStringList categories;
        for (const auto &name : tool.categories)
            categories.append(QString::fromStdString(name));
        definitions_.push_back({categories,
                                QString::fromStdString(tool.nameZh),
                                QString::fromStdString(tool.nameEn),
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

// The folder node with this name, created on first use so one group holds every tool under it.
ToolTreeModel::Entry &ToolTreeModel::groupEntry(Entry &parent, const QString &name)
{
    for (auto &child : parent.children)
        if (child->name == name)
            return *child;
    auto child    = std::make_unique<Entry>();
    child->name   = name;
    child->parent = &parent;
    parent.children.push_back(std::move(child));
    return *parent.children.back();
}

ToolTreeModel::Entry *ToolTreeModel::findById(Entry &entry, const QString &id)
{
    for (auto &child : entry.children)
    {
        if (child->id == id)
            return child.get();
        if (auto *found = findById(*child, id))
            return found;
    }
    return nullptr;
}

bool ToolTreeModel::updateToolStatus(const QString &toolId, bool available, const QString &status)
{
    auto *tool = findById(root_, toolId);
    if (!tool || (tool->available == available && tool->status == status))
        return false;
    tool->available = available;
    tool->status    = status;

    const auto indexOf = [this](const Entry *entry) -> QModelIndex
    {
        const auto *parent = entry->parent;
        if (!parent)
            return {};
        const auto &siblings = parent->children;
        for (std::size_t row = 0; row < siblings.size(); ++row)
            if (siblings[row].get() == entry)
                return createIndex(static_cast<int>(row), 0, const_cast<Entry *>(entry));
        return {};
    };
    const auto idx = indexOf(tool);
    emit       dataChanged(idx, idx, {AvailableRole, StatusRole});
    return true;
}

void ToolTreeModel::rebuild()
{
    beginResetModel();
    root_.children.clear();
    for (const auto &definition : definitions_)
    {
        if (!filter_.isEmpty() && !definition.name.contains(filter_, Qt::CaseInsensitive))
            continue;
        Entry *parent = &root_;
        if (definition.categories.isEmpty())
            parent = &groupEntry(root_, QStringLiteral("未分类"));
        for (const auto &category : definition.categories)
            parent = &groupEntry(*parent, category);
        auto child       = std::make_unique<Entry>();
        child->name      = definition.name;
        child->id        = definition.id;
        child->version   = definition.version;
        child->status    = definition.status;
        child->available = definition.available;
        child->parent    = parent;
        parent->children.push_back(std::move(child));
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
        {
            // The full folder path, so two roots that use the same leaf name stay distinct.
            QStringList path;
            for (const Entry *node = &entry; node && node != &root_; node = node->parent)
                path.prepend(node->name);
            return path.join(QLatin1Char('/'));
        }
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
