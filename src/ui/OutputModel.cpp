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
    beginResetModel();
    rows_.clear();
    endResetModel();
}

void OutputModel::setResult(const ToolDefinition *definition, ResultSlot::Snapshot snapshot,
                            const std::map<PortId, std::size_t> &references,
                            const std::map<PortId, bool> *connectable,
                            const TypeMap *nodeOutputs, std::shared_ptr<const NodeDiagnostic> lifecycle,
                            std::optional<NodeDiagnostic> status)
{
    if (!status)
    {
        if (snapshot)
            status = NodeDiagnostic{snapshot->node, snapshot->toolId, snapshot->phase, snapshot->status};

    }
    beginResetModel();
    rows_.clear();
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
        const auto count = references.find(id);
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
                       count == references.end() ? 0 : count->second,
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
        rows_.push_back(std::move(row));
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
                    rows_.push_back(std::move(row));
                }
            }
        }
    else if (nodeOutputs)
        for (const auto &[id, type] : *nodeOutputs)
        {
            if (!isReservedPort(PortStage::Outputs, id))
                add(id, portLabel(id), type, false);
        }
    endResetModel();
}

void OutputModel::setStatus(std::optional<NodeDiagnostic> status)
{
    for (int index = 0; index < rowCount(); ++index)
    {
        auto &row = rows_[index];
        if (!row.system)
            continue;
        const PortValue value = !status ? PortValue{Missing{}} : row.id == kStatusCodePort ? PortValue{status->status.code}
                                                        : PortValue{status->status.message};
        if (equalValues(row.captured.value, value))
            continue;
        row.captured.value = value;
        row.captured.diagnostic = status;
        row.preview = displayValue(value, row.captured.type);
        row.tooltip = row.preview;
        emit dataChanged(this->index(index, 0), this->index(index, columnCount() - 1));
    }
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
        {     "count",                                    static_cast<qulonglong>(item.count)},
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
            return row.name + QStringLiteral(" (%1)").arg(row.count);
        case 1:
            return row.type;
        case 2:
            return row.preview;
        case 3:
            return static_cast<qulonglong>(row.count);
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
        return static_cast<qulonglong>(row.count);
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
        {     SystemRole,     "system"},
        { ConnectableRole, "connectable"},
        { DiagnosticRole, "diagnostic"},
        {   RowIndexRole,   "rowIndex"}
    };
}
} // namespace visionrt
