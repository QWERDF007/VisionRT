#include "ui/OutputModel.h"

#include "core/ValueJson.h"
#include "ui/Diagnostics.h"
#include "ui/ValueDisplay.h"

#include <algorithm>
#include <limits>
#include <type_traits>

namespace visionrt {
namespace {
std::size_t estimatedStringBytes(const std::string &value);

QString q(const std::string &value)
{
    return QString::fromUtf8(value.data(), static_cast<qsizetype>(value.size()));
}

std::size_t estimatedArrayBytes(const Array &array)
{
    const auto maximum = std::numeric_limits<std::size_t>::max();
    if (array.dtype() != DType::String)
        return array.elementCount() > (maximum - 128) / 128 ? maximum : array.elementCount() * 128 + 128;
    std::size_t size = 128;
    for (const auto &value : array.elements<std::string>())
    {
        const auto bytes = estimatedStringBytes(value);
        if (bytes >= maximum - size)
            return maximum;
        size += bytes + 1;
    }
    return size;
}

QString displayValue(const PortValue &value, const TypeSpec &type)
{
    if (isMissing(value))
        return {};
    if (std::holds_alternative<DataRef<Image>>(value) || std::holds_alternative<DataRef<TrainingSet>>(value))
        return QStringLiteral("非显示型数据");
    if (const auto *text = std::get_if<std::string>(&value))
        return q(*text);
    return displayValueText(value, type);
}

std::size_t estimatedStringBytes(const std::string &value)
{
    const auto maximum = std::numeric_limits<std::size_t>::max();
    return value.size() > (maximum - 128) / 6 ? maximum : value.size() * 6 + 128;
}

template<class Vector>
std::size_t estimatedNestedBytes(const Vector &values)
{
    using Element      = typename Vector::value_type;
    const auto maximum = std::numeric_limits<std::size_t>::max();
    if constexpr (std::is_arithmetic_v<Element>)
        return values.size() > (maximum - 128) / 128 ? maximum : values.size() * 128 + 128;
    else
    {
        std::size_t size = 128;
        for (const auto &value : values)
        {
            const auto bytes = [&]
            {
                if constexpr (std::is_same_v<Element, std::string>)
                    return estimatedStringBytes(value);
                else
                    return estimatedNestedBytes(value);
            }();
            if (bytes >= maximum - size)
                return maximum;
            size += bytes + 1;
        }
        return size;
    }
}

QVariantList relationVariants(const std::vector<OutputRelation> &relations)
{
    QVariantList result;
    result.reserve(static_cast<qsizetype>(relations.size()));
    for (const auto &relation : relations)
        result.push_back(QVariantMap{
            { "node",                    relation.node},
            { "port", static_cast<uint>(relation.port)},
            {"label",                   relation.label}
        });
    return result;
}

bool equalRelations(const std::vector<OutputRelation> &a, const std::vector<OutputRelation> &b)
{
    if (a.size() != b.size())
        return false;
    for (std::size_t i = 0; i < a.size(); ++i)
        if (a[i].node != b[i].node || a[i].port != b[i].port || a[i].label != b[i].label)
            return false;
    return true;
}
} // namespace

QString typeText(const TypeSpec &type)
{
    QString text = QString::fromLatin1(typeName(type.type));
    if (type.type == Type::Array)
    {
        if (type.dtype)
            text += "<" + QString::fromLatin1(dtypeName(*type.dtype)) + ">";
        text += QStringLiteral(" rank=%1").arg(type.rank);
    }
    return text;
}

QString valueText(const PortValue &value, const TypeSpec &type)
{
    return ValueJson::text(value, type);
}

OutputModel::OutputModel(QObject *parent)
    : QAbstractTableModel(parent)
{
}

void OutputModel::clear()
{
    if (rows_.empty())
        return;
    beginResetModel();
    rows_.clear();
    endResetModel();
}

void OutputModel::setResult(const ToolDefinition *definition, ResultSlot::Snapshot snapshot,
                            const std::map<PortId, std::vector<OutputRelation>> &relations,
                            const std::map<PortId, bool> *connectable,
                            const TypeMap *nodeOutputs, std::shared_ptr<const NodeDiagnostic> lifecycle,
                            std::optional<NodeDiagnostic> status)
{
    if (!status)
    {
        if (snapshot)
            status = NodeDiagnostic{snapshot->node, snapshot->toolId, snapshot->phase, snapshot->status};
    }

    std::vector<Row> nextRows;
    auto add = [&](PortId id, std::string name, TypeSpec type, bool system)
    {
        PortValue value = Missing{};
        if (id == kStatusCodePort && status)
            value = status->status.code;
        else if (id == kStatusMessagePort && status)
            value = status->status.message;
        else if (snapshot)
            if (const auto found = snapshot->outputs.find(id); found != snapshot->outputs.end())
                value = found->second;
        const auto relIt = relations.find(id);
        Captured   captured{std::move(value), type, snapshot ? snapshot->publication : 0, id};
        if (system)
        {
            captured.diagnostic = status;
            captured.lifecycle = lifecycle;
        }
        bool       canConnect = false;
        if (connectable)
        {
            const auto found = connectable->find(id);
            canConnect       = found != connectable->end() && found->second;
        }
        Row        row{id,
                       q(name),
                       typeText(type),
                       {},
                       {},
                       relIt == relations.end() ? std::vector<OutputRelation>{} : relIt->second,
                       system,
                       canConnect,
                       std::move(captured)};
        row.preview = displayValue(row.captured.value, row.captured.type);
        row.tooltip = row.preview;
        if (id == kStatusMessagePort && row.captured.diagnostic)
        {
            const auto &diagnostic = *row.captured.diagnostic;
            const auto rawMsg = q(diagnostic.status.message);
            row.tooltip = !rawMsg.trimmed().isEmpty() ? rawMsg : statusSummary(diagnostic.status);
        }
        nextRows.push_back(std::move(row));
    };
    add(kStatusCodePort, "状态码", {Type::Int}, true);
    add(kStatusMessagePort, "状态消息", {Type::String}, true);
    if (definition)
        for (const auto &port : definition->outputs)
        {
            if (!isReservedPort(PortStage::Outputs, port.id))
            {
                if (!port.typeFrom)
                    add(port.id, port.nameZh, port.type, false);
                else if (nodeOutputs && nodeOutputs->contains(port.id))
                    add(port.id, port.nameZh, nodeOutputs->at(port.id), false);
                else
                {
                    Row row;
                    row.id = port.id;
                    row.name = q(port.nameZh);
                    row.type = QStringLiteral("未绑定");
                    row.tooltip = QStringLiteral("请选择源流程、源节点和输出端口");
                    row.resolved = false;
                    nextRows.push_back(std::move(row));
                }
            }
        }
    else if (nodeOutputs)
        for (const auto &[id, type] : *nodeOutputs)
        {
            if (!isReservedPort(PortStage::Outputs, id))
                add(id, portLabel(id), type, false);
        }

    const bool sameTopology = nextRows.size() == rows_.size()
        && std::equal(rows_.begin(), rows_.end(), nextRows.begin(),
                      [](const Row &a, const Row &b)
                      { return a.id == b.id && a.system == b.system && a.resolved == b.resolved; });

    if (sameTopology)
    {
        const int lastCol = columnCount() - 1;
        for (std::size_t i = 0; i < rows_.size(); ++i)
        {
            const auto &prev = rows_[i];
            auto       &next = nextRows[i];
            QList<int>  changed;
            bool        displayChanged = false;
            if (prev.name != next.name)
            {
                changed.push_back(NameRole);
                displayChanged = true;
            }
            if (prev.type != next.type)
            {
                changed.push_back(TypeRole);
                displayChanged = true;
            }
            if (prev.captured.type != next.captured.type)
                changed.push_back(TypeDisplayRole);
            if (prev.preview != next.preview)
            {
                changed.push_back(PreviewRole);
                displayChanged = true;
            }
            if (prev.tooltip != next.tooltip)
            {
                changed.push_back(TooltipRole);
                changed.push_back(Qt::ToolTipRole);
            }
            if (!equalRelations(prev.relations, next.relations))
            {
                changed.push_back(RelationsRole);
                changed.push_back(CountRole);
                displayChanged = true;
            }
            if (prev.connectable != next.connectable)
                changed.push_back(ConnectableRole);
            const bool prevDiag = static_cast<bool>(prev.captured.diagnostic || prev.captured.lifecycle);
            const bool nextDiag = static_cast<bool>(next.captured.diagnostic || next.captured.lifecycle);
            if (prevDiag != nextDiag)
                changed.push_back(DiagnosticRole);
            if (displayChanged)
                changed.push_back(Qt::DisplayRole);

            rows_[i] = std::move(next);
            if (!changed.isEmpty())
            {
                const int rowIdx = static_cast<int>(i);
                emit dataChanged(index(rowIdx, 0), index(rowIdx, lastCol), changed);
            }
        }
        return;
    }

    beginResetModel();
    rows_ = std::move(nextRows);
    endResetModel();
}

std::optional<OutputModel::Captured> OutputModel::capture(int row) const
{
    if (row < 0 || static_cast<std::size_t>(row) >= rows_.size() || !rows_[row].resolved)
        return {};
    return rows_[row].captured;
}

std::optional<OutputModel::Captured> OutputModel::capture(PortId port) const
{
    for (const auto &row : rows_)
        if (row.id == port && row.resolved)
            return row.captured;
    return {};
}

QString OutputModel::fullText(const Captured &captured)
{
    return ValueJson::text(captured.value, captured.type, ValueJson::Purpose::Display);
}

QString OutputModel::diagnosticText(const Captured &captured)
{
    if (captured.diagnostic)
        return diagnosticJson(*captured.diagnostic, captured.lifecycle.get());
    return captured.lifecycle ? diagnosticJson(*captured.lifecycle) : QString{};
}

std::size_t OutputModel::estimatedTextBytes(const Captured &captured)
{
    return std::visit(
        [](const auto &value) -> std::size_t
        {
            using Element = std::decay_t<decltype(value)>;
            if constexpr (std::is_same_v<Element, Array>)
                return estimatedArrayBytes(value);
            else if constexpr (std::is_same_v<Element, std::string>)
                return estimatedStringBytes(value);
            else if constexpr (isShapeValue<Element>)
                return estimatedNestedBytes(value.get());
            else
                return 128;
        },
        captured.value);
}

QVariantMap OutputModel::get(int row) const
{
    if (row < 0 || static_cast<std::size_t>(row) >= rows_.size())
        return {};
    const auto &item = rows_[row];
    return {
        {    "portId",                                                                item.id},
        {   "port_id",                                                                item.id},
        {      "name",                                                              item.name},
        {      "type",                                                              item.type},
        {"typeDisplay", !item.resolved ? QStringLiteral("未绑定") : displayTypeName(item.captured.type)},
        {   "preview",                                                           item.preview},
        {   "tooltip",                                                           item.tooltip},
        {     "count",                             static_cast<qulonglong>(item.relations.size())},
        { "relations",                                           relationVariants(item.relations)},
        {    "system",                                                            item.system},
        {"connectable",                                                        item.connectable},
        {"diagnostic", static_cast<bool>(item.captured.diagnostic || item.captured.lifecycle)},
        {  "rowIndex",                                                                    row}
    };
}

int OutputModel::rowCount(const QModelIndex &parent) const
{
    return parent.isValid() ? 0 : static_cast<int>(rows_.size());
}

int OutputModel::columnCount(const QModelIndex &parent) const
{
    return parent.isValid() ? 0 : 4;
}

QVariant OutputModel::data(const QModelIndex &index, int role) const
{
    if (!index.isValid() || index.row() < 0 || index.row() >= rowCount())
        return {};
    const auto &row = rows_[index.row()];
    switch (role)
    {
    case Qt::DisplayRole:
        switch (index.column())
        {
        case 0:
            return row.name;
        case 1:
            return row.type;
        case 2:
            return row.preview;
        case 3:
        {
            QStringList labels;
            labels.reserve(static_cast<qsizetype>(row.relations.size()));
            for (const auto &relation : row.relations) labels.push_back(relation.label);
            return labels.join(QStringLiteral(", "));
        }
        default:
            return {};
        }
    case Qt::ToolTipRole:
    case TooltipRole:
        return row.tooltip;
    case PortIdRole:
        return row.id;
    case NameRole:
        return row.name;
    case TypeRole:
        return row.type;
    case TypeDisplayRole:
        return displayTypeName(row.captured.type);
    case PreviewRole:
        return row.preview;
    case CountRole:
        return static_cast<qulonglong>(row.relations.size());
    case RelationsRole:
        return relationVariants(row.relations);
    case ConnectableRole:
        return rows_.at(static_cast<std::size_t>(index.row())).connectable;
    case SystemRole:
        return row.system;
    case DiagnosticRole:
        return static_cast<bool>(row.captured.diagnostic || row.captured.lifecycle);
    case RowIndexRole:
        return index.row();
    default:
        return {};
    }
}

QVariant OutputModel::headerData(int section, Qt::Orientation orientation, int role) const
{
    if (orientation != Qt::Horizontal || role != Qt::DisplayRole)
        return {};
    const QStringList headers{QStringLiteral("名称"), QStringLiteral("类型"), QStringLiteral("数据"),
                              QStringLiteral("关系")};
    return section >= 0 && section < headers.size() ? headers[section] : QVariant{};
}

QHash<int, QByteArray> OutputModel::roleNames() const
{
    return {
        {Qt::DisplayRole,    "display"},
        {     PortIdRole,     "portId"},
        {       NameRole,       "name"},
        {       TypeRole,        "type"},
        {TypeDisplayRole, "typeDisplay"},
        {    PreviewRole,     "preview"},
        {    TooltipRole,    "tooltip"},
        {      CountRole,      "count"},
        {  RelationsRole,  "relations"},
        {     SystemRole,     "system"},
        { ConnectableRole, "connectable"},
        { DiagnosticRole, "diagnostic"},
        {   RowIndexRole,   "rowIndex"}
    };
}
} // namespace visionrt
