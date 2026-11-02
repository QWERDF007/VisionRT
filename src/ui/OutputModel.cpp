#include "ui/OutputModel.h"

#include "core/ValueJson.h"
#include "ui/Diagnostics.h"

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

struct Preview
{
    QString   text;
    qsizetype limit;
    bool      truncated = false;

    bool append(QStringView value)
    {
        const auto room = limit - text.size();
        if (value.size() > room)
        {
            text += value.left(std::max<qsizetype>(0, room));
            truncated = true;
            return false;
        }
        text += value;
        return true;
    }

    bool append(const QString &value)
    {
        return append(QStringView(value));
    }

    bool full() const
    {
        return text.size() >= limit;
    }

    QString finish()
    {
        if (truncated || full())
        {
            text.truncate(std::max<qsizetype>(0, limit - 3));
            text += QStringLiteral("...");
        }
        return text;
    }
};

QString stringPreview(const std::string &value, qsizetype limit, bool quoted, bool &truncated)
{
    const auto bytes = std::min(value.size(), static_cast<std::size_t>(limit) * 4 + 4);
    truncated |= bytes < value.size();
    const auto prefix = QString::fromUtf8(value.data(), static_cast<qsizetype>(bytes));
    if (!quoted)
        return prefix;
    return ValueJson::text(prefix.toUtf8().toStdString(), {Type::String});
}

template<class Vector>
void appendNested(Preview &out, const Vector &values)
{
    using Element = typename Vector::value_type;
    out.append(QStringLiteral("["));
    for (std::size_t index = 0; index < values.size(); ++index)
    {
        if (out.full())
        {
            out.truncated = true;
            return;
        }
        if (index)
            out.append(QStringLiteral(", "));
        if constexpr (std::is_same_v<Element, std::string>)
            out.append(stringPreview(values[index], out.limit - out.text.size(), true, out.truncated));
        else if constexpr (std::is_same_v<Element, bool>)
            out.append(values[index] ? QStringLiteral("true") : QStringLiteral("false"));
        else if constexpr (std::is_integral_v<Element>)
            out.append(QString::number(static_cast<qlonglong>(values[index])));
        else if constexpr (std::is_floating_point_v<Element>)
            out.append(QString::number(values[index], 'g', std::numeric_limits<Element>::max_digits10));
        else
            appendNested(out, values[index]);
    }
    out.append(QStringLiteral("]"));
}


template<class T>
void appendArrayLeaf(Preview &out, const Array &array, std::size_t index)
{
    if constexpr (std::is_same_v<T, bool>)
        out.append(array.boolBytes()[index] != 0 ? QStringLiteral("true") : QStringLiteral("false"));
    else if constexpr (std::is_same_v<T, std::string>)
        out.append(stringPreview(array.elements<std::string>()[index], out.limit - out.text.size(), true,
                                 out.truncated));
    else if constexpr (std::is_integral_v<T>)
        out.append(QString::number(static_cast<qlonglong>(array.elements<T>()[index])));
    else
        out.append(QString::number(array.elements<T>()[index], 'g', std::numeric_limits<T>::max_digits10));
}

template<class T>
void appendArrayDense(Preview &out, const Array &array, int dimension, std::size_t &index)
{
    out.append(QStringLiteral("["));
    const auto &shape = array.shape();
    for (std::int64_t i = 0; i < shape[static_cast<std::size_t>(dimension)]; ++i)
    {
        if (out.full())
        {
            out.truncated = true;
            return;
        }
        if (i)
            out.append(QStringLiteral(", "));
        if (dimension + 1 == static_cast<int>(shape.size()))
            appendArrayLeaf<T>(out, array, index++);
        else
            appendArrayDense<T>(out, array, dimension + 1, index);
    }
    out.append(QStringLiteral("]"));
}

template<class T>
void appendArrayRagged(Preview &out, const Array &array, int level, std::size_t group, std::size_t &index)
{
    out.append(QStringLiteral("["));
    const auto &bounds = array.offsets()[static_cast<std::size_t>(level)];
    if (level + 1 == static_cast<int>(array.offsets().size()))
    {
        for (auto leaf = bounds[group]; leaf < bounds[group + 1]; ++leaf)
        {
            if (out.full())
            {
                out.truncated = true;
                return;
            }
            if (leaf != bounds[group])
                out.append(QStringLiteral(", "));
            appendArrayLeaf<T>(out, array, index++);
        }
    }
    else
    {
        for (auto child = bounds[group]; child < bounds[group + 1]; ++child)
        {
            if (out.full())
            {
                out.truncated = true;
                return;
            }
            if (child != bounds[group])
                out.append(QStringLiteral(", "));
            appendArrayRagged<T>(out, array, level + 1, static_cast<std::size_t>(child), index);
        }
    }
    out.append(QStringLiteral("]"));
}

template<class T>
void appendArrayTyped(Preview &out, const Array &array)
{
    if (array.layout() == ArrayLayout::Dense)
    {
        std::size_t index = 0;
        appendArrayDense<T>(out, array, 0, index);
        return;
    }
    out.append(QStringLiteral("["));
    std::size_t index  = 0;
    const auto  groups = array.offsets().empty() ? 0 : array.offsets()[0].size() - 1;
    for (std::size_t group = 0; group < groups; ++group)
    {
        if (out.full())
        {
            out.truncated = true;
            return;
        }
        if (group)
            out.append(QStringLiteral(", "));
        appendArrayRagged<T>(out, array, 0, group, index);
    }
    out.append(QStringLiteral("]"));
}

void appendArray(Preview &out, const Array &array)
{
    switch (array.dtype())
    {
    case DType::UInt8:
        appendArrayTyped<std::uint8_t>(out, array);
        break;
    case DType::Int32:
        appendArrayTyped<std::int32_t>(out, array);
        break;
    case DType::Int64:
        appendArrayTyped<std::int64_t>(out, array);
        break;
    case DType::Float32:
        appendArrayTyped<float>(out, array);
        break;
    case DType::Float64:
        appendArrayTyped<double>(out, array);
        break;
    case DType::Bool:
        appendArrayTyped<bool>(out, array);
        break;
    case DType::String:
        appendArrayTyped<std::string>(out, array);
        break;
    }
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

QString preview(const PortValue &value, qsizetype limit)
{
    Preview out{{}, limit};
    std::visit(
        [&](const auto &item)
        {
            using Element = std::decay_t<decltype(item)>;
            if constexpr (std::is_same_v<Element, Missing>)
                return;
            else if constexpr (std::is_same_v<Element, std::string>)
                out.append(stringPreview(item, limit, false, out.truncated));
            else if constexpr (std::is_same_v<Element, DataRef<Image>>)
                out.append(QStringLiteral("非显示型数据"));
            else if constexpr (std::is_same_v<Element, Array>)
                appendArray(out, item);
            else if constexpr (std::is_same_v<Element, DataRef<ROI>>)
                appendNested(out, item.get());
            else if constexpr (std::is_same_v<Element, bool>)
                out.append(item ? QStringLiteral("true") : QStringLiteral("false"));
            else if constexpr (std::is_same_v<Element, std::int64_t>)
                out.append(QString::number(static_cast<qlonglong>(item)));
            else
                out.append(QString::number(item, 'g', std::numeric_limits<Element>::max_digits10));
        },
        value);
    return out.finish();
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
                            const std::map<std::string, std::size_t, std::less<>> &references,
                            const std::map<std::string, bool, std::less<>> *connectable,
                            const TypeMap *nodeOutputs, std::shared_ptr<const NodeDiagnostic> lifecycle)
{
    beginResetModel();
    rows_.clear();
    auto add = [&](std::string id, std::string name, TypeSpec type, bool system)
    {
        PortValue value = Missing{};
        // A framework-stopped node has no tool answer: its diagnostic must never appear as port
        // data. Only a real ToolResult fills the two status rows.
        if (snapshot && (!system || snapshot->toolResultPublished))
        {
            if (id == "__status_code")
                value = snapshot->status.code;
            else if (id == "__status_message")
                value = snapshot->status.message;
            else if (const auto found = snapshot->outputs.find(id); found != snapshot->outputs.end())
                value = found->second;
        }
        const auto count = references.find(id);
        Captured   captured{std::move(value), type, snapshot ? snapshot->publication : 0, q(id)};
        if (system && snapshot)
        {
            captured.diagnostic = NodeDiagnostic{snapshot->node, snapshot->toolId, snapshot->phase, snapshot->status};
        }
        if (system)
            captured.lifecycle = lifecycle;
        bool       canConnect = false;
        if (connectable)
        {
            const auto found = connectable->find(id);
            canConnect       = found != connectable->end() && found->second;
        }
        Row        row{q(id),
                       q(name),
                       typeText(type),
                       {},
                       {},
                       count == references.end() ? 0 : count->second,
                       system,
                       canConnect,
                       std::move(captured)};
        row.preview = preview(row.captured.value, 256);
        row.tooltip = preview(row.captured.value, 4096);
        if (id == "__status_message" && row.captured.diagnostic)
        {
            const auto &diagnostic = *row.captured.diagnostic;
            const auto rawMsg = q(diagnostic.status.message);
            row.tooltip = !rawMsg.trimmed().isEmpty() ? rawMsg : statusSummary(diagnostic.status);
        }
        rows_.push_back(std::move(row));
    };
    add("__status_code", "状态码", {Type::Int}, true);
    add("__status_message", "状态消息", {Type::String}, true);
    if (definition)
        for (const auto &port : definition->outputs)
        {
            if (port.nameEn != "__status_code" && port.nameEn != "__status_message")
            {
                if (!port.typeFrom)
                    add(port.nameEn, port.nameZh, port.type, false);
                else if (nodeOutputs && nodeOutputs->contains(port.nameEn))
                    add(port.nameEn, port.nameZh, nodeOutputs->at(port.nameEn), false);
                else
                {
                    Row row;
                    row.id = q(port.nameEn);
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
            if (id != "__status_code" && id != "__status_message")
                add(id, id, type, false);
        }
    endResetModel();
}

std::optional<OutputModel::Captured> OutputModel::capture(int row) const
{
    if (row < 0 || static_cast<std::size_t>(row) >= rows_.size() || !rows_[row].resolved)
        return {};
    return rows_[row].captured;
}

std::optional<OutputModel::Captured> OutputModel::capture(const QString &port) const
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
            else if constexpr (std::is_same_v<Element, DataRef<ROI>>)
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
