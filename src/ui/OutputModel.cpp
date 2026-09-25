#include "ui/OutputModel.h"
#include "core/ValueYaml.h"
#include <algorithm>
#include <limits>

namespace visionrt {
namespace {
QString q(const std::string& value) { return QString::fromUtf8(value.data(), static_cast<qsizetype>(value.size())); }
struct Preview {
    QString text;
    qsizetype limit;
    bool truncated = false;
    bool append(QStringView value) {
        const auto room = limit - text.size();
        if (value.size() > room) { text += value.left(std::max<qsizetype>(0, room)); truncated = true; return false; }
        text += value; return true;
    }
    bool append(const QString& value) { return append(QStringView(value)); }
    bool full() const { return text.size() >= limit; }
    QString finish() {
        if (truncated || full()) { text.truncate(std::max<qsizetype>(0, limit - 4)); text += QStringLiteral("…[截断]").left(limit - text.size()); }
        return text;
    }
};
template<class T> void numbers(Preview& out, std::span<const T> values) {
    for (std::size_t index = 0; index < values.size() && !out.full(); ++index) {
        if (index) out.append(QStringLiteral(", "));
        if constexpr (std::is_same_v<T, bool>) out.append(values[index] ? QStringLiteral("true") : QStringLiteral("false"));
        else if constexpr (std::is_floating_point_v<T>) out.append(QString::number(values[index], 'g', std::numeric_limits<T>::max_digits10));
        else out.append(QString::number(static_cast<qlonglong>(values[index])));
        if (index + 1 < values.size() && out.full()) out.truncated = true;
    }
}
QString preview(const PortValue& value, const TypeSpec& type, qsizetype limit) {
    Preview out{{}, limit};
    std::visit([&](const auto& item) {
        using T = std::decay_t<decltype(item)>;
        if constexpr (std::is_same_v<T, Missing>) out.append(QStringLiteral("Missing"));
        else if constexpr (std::is_same_v<T, Utf8>) {
            const auto bytes = std::min<std::size_t>(item.str().size(), static_cast<std::size_t>(limit) * 4 + 4);
            out.append(QString::fromUtf8(item.str().data(), static_cast<qsizetype>(bytes)));
            out.truncated |= bytes < item.str().size();
        } else if constexpr (std::is_same_v<T, ImageBatch>) {
            out.append(QStringLiteral("ImageBatch B=%1 [").arg(item.images().size()));
            for (std::size_t i = 0; i < item.images().size() && !out.full(); ++i) {
                const auto& image = item.images()[i];
                out.append(QStringLiteral("%1%2×%3×%4").arg(i ? ", " : "").arg(image.cols()).arg(image.rows()).arg(image.channels()));
            }
            out.append(QStringLiteral("]"));
        } else if constexpr (std::is_same_v<T, NumericArray>) {
            out.append(typeText(type) + " [");
            for (const auto& buffer : item.buffers()) {
                if (out.full()) { out.truncated = true; break; }
                out.append(QStringLiteral("shape=(")); numbers(out, buffer.shape()); out.append(QStringLiteral(") ["));
                switch (buffer.dtype()) {
                case DType::UInt8: numbers(out, buffer.template values<std::uint8_t>()); break;
                case DType::Int32: numbers(out, buffer.template values<std::int32_t>()); break;
                case DType::Int64: numbers(out, buffer.template values<std::int64_t>()); break;
                case DType::Float32: numbers(out, buffer.template values<float>()); break;
                case DType::Float64: numbers(out, buffer.template values<double>()); break;
                case DType::Bool: numbers(out, buffer.template values<bool>()); break;
                }
                out.append(QStringLiteral("]; "));
            }
            out.append(QStringLiteral("]"));
        } else if constexpr (std::is_same_v<T, StringArray>) {
            out.append(typeText(type) + " [");
            for (const auto& buffer : item.buffers()) {
                if (out.full()) { out.truncated = true; break; }
                out.append(QStringLiteral("shape=(")); numbers(out, buffer.shape()); out.append(QStringLiteral(") ["));
                for (const auto& string : buffer.values()) {
                    if (out.full()) { out.truncated = true; break; }
                    const auto bytes = std::min<std::size_t>(string.size(), static_cast<std::size_t>(limit) * 4 + 4);
                    out.append(QString::fromUtf8(string.data(), static_cast<qsizetype>(bytes)));
                    out.truncated |= bytes < string.size(); out.append(QStringLiteral(", "));
                }
                out.append(QStringLiteral("]; "));
            }
            out.append(QStringLiteral("]"));
        } else if constexpr (std::is_same_v<T, RoiBatch>) {
            out.append(QStringLiteral("["));
            for (const auto& image : item.images()) {
                if (out.full()) { out.truncated = true; break; }
                out.append(QStringLiteral("["));
                for (const auto& roi : image) {
                    if (out.full()) { out.truncated = true; break; }
                    out.append(QStringLiteral("[")); numbers(out, roi.encoded()); out.append(QStringLiteral("], "));
                }
                out.append(QStringLiteral("], "));
            }
            out.append(QStringLiteral("]"));
        } else if constexpr (std::is_same_v<T, bool>) out.append(item ? QStringLiteral("true") : QStringLiteral("false"));
        else if constexpr (std::is_same_v<T, std::int64_t>) out.append(QString::number(static_cast<qlonglong>(item)));
        else out.append(QString::number(item, 'g', 17));
    }, value);
    return out.finish();
}
}
QString typeText(const TypeSpec& type) {
    static const char* names[] = {"Int", "Float", "Bool", "String", "ImageBatch", "NumericArray", "StringArray", "RoiBatch"};
    QString text = QString::fromLatin1(names[static_cast<int>(type.type)]);
    if (type.type == Type::NumericArray || type.type == Type::StringArray) {
        static const char* dtypes[] = {"uint8", "int32", "int64", "float32", "float64", "bool"};
        if (type.dtype) text += "<" + QString::fromLatin1(dtypes[static_cast<int>(*type.dtype)]) + ">";
        text += QStringLiteral(" rank=%1 %2").arg(type.rank).arg(type.layout == Layout::Dense ? "dense" : "batch_ragged");
    }
    return text;
}
QString valueText(const PortValue& value, const TypeSpec& type) {
    return q(YAML::Dump(encodeValue(value, type)));
}
OutputModel::OutputModel(QObject* parent) : QAbstractTableModel(parent) {}
void OutputModel::clear() { beginResetModel(); rows_.clear(); endResetModel(); }
void OutputModel::setResult(const ToolDefinition* definition, ResultSlot::Snapshot snapshot,
                           const std::map<std::string, std::size_t, std::less<>>& references,
                           const TypeMap* unresolvedOutputs) {
    beginResetModel(); rows_.clear();
    auto add = [&](std::string id, std::string name, TypeSpec type, bool system) {
        PortValue value = Missing{};
        if (snapshot) {
            if (id == "__status_code") value = snapshot->status.code;
            else if (id == "__status_message") value = Utf8(snapshot->status.message);
            else if (const auto found = snapshot->outputs.find(id); found != snapshot->outputs.end()) value = found->second;
        }
        const auto count = references.find(id);
        Captured captured{std::move(value), type, snapshot ? snapshot->publication : 0, q(id)};
        Row row{q(id), q(name), typeText(type), {}, {}, count == references.end() ? 0 : count->second, system, std::move(captured)};
        row.preview = preview(row.captured.value, type, 256);
        row.tooltip = preview(row.captured.value, type, 4096);
        rows_.push_back(std::move(row));
    };
    add("__status_code", "状态码", {Type::Int}, true);
    add("__status_message", "状态消息", {Type::String}, true);
    if (definition) for (const auto& port : definition->outputs) {
        if (port.id != "__status_code" && port.id != "__status_message") add(port.id, port.name, port.type, false);
    } else if (unresolvedOutputs) for (const auto& [id, type] : *unresolvedOutputs) {
        if (id != "__status_code" && id != "__status_message") add(id, id, type, false);
    }
    endResetModel();
}
std::optional<OutputModel::Captured> OutputModel::capture(int row) const {
    if (row < 0 || static_cast<std::size_t>(row) >= rows_.size()) return {};
    return rows_[row].captured;
}
QString OutputModel::fullText(const Captured& captured) {
    if (std::holds_alternative<ImageBatch>(captured.value)) return preview(captured.value, captured.type, std::numeric_limits<qsizetype>::max());
    if (const auto* string = std::get_if<Utf8>(&captured.value)) return q(string->str());
    return valueText(captured.value, captured.type);
}
std::size_t OutputModel::estimatedTextBytes(const Captured& captured) {
    return std::visit([](const auto& value) -> std::size_t {
        using T = std::decay_t<decltype(value)>;
        if constexpr (std::is_same_v<T, NumericArray>) {
            std::size_t size = 128;
            for (const auto& buffer : value.buffers()) size += (buffer.byteSize() / dtypeSize(buffer.dtype())) * 32 + 128;
            return size;
        } else if constexpr (std::is_same_v<T, Utf8>) return value.str().size();
        else if constexpr (std::is_same_v<T, StringArray>) {
            std::size_t size = 128;
            for (const auto& buffer : value.buffers()) for (const auto& string : buffer.values()) size += string.size() * 6 + 4;
            return size;
        } else if constexpr (std::is_same_v<T, RoiBatch>) {
            std::size_t size = 128;
            for (const auto& image : value.images()) for (const auto& roi : image) size += roi.encoded().size() * 24 + 4;
            return size;
        } else if constexpr (std::is_same_v<T, ImageBatch>) return value.images().size() * 128;
        else return 128;
    }, captured.value);
}
QVariantMap OutputModel::get(int row) const {
    if (row < 0 || static_cast<std::size_t>(row) >= rows_.size()) return {};
    const auto& item = rows_[row];
    return {{"portId", item.id}, {"port_id", item.id}, {"name", item.name}, {"type", item.type},
        {"preview", item.preview}, {"tooltip", item.tooltip}, {"count", static_cast<qulonglong>(item.count)},
        {"system", item.system}, {"rowIndex", row}};
}
int OutputModel::rowCount(const QModelIndex& parent) const { return parent.isValid() ? 0 : static_cast<int>(rows_.size()); }
int OutputModel::columnCount(const QModelIndex& parent) const { return parent.isValid() ? 0 : 4; }
QVariant OutputModel::data(const QModelIndex& index, int role) const {
    if (!index.isValid() || index.row() < 0 || index.row() >= rowCount()) return {};
    const auto& row = rows_[index.row()];
    switch (role) {
    case Qt::DisplayRole:
        switch (index.column()) {
        case 0: return row.name + QStringLiteral(" (%1)").arg(row.count);
        case 1: return row.type;
        case 2: return row.preview;
        case 3: return static_cast<qulonglong>(row.count);
        default: return {};
        }
    case Qt::ToolTipRole: case TooltipRole: return row.tooltip;
    case PortIdRole: return row.id;
    case NameRole: return row.name;
    case TypeRole: return row.type;
    case PreviewRole: return row.preview;
    case CountRole: return static_cast<qulonglong>(row.count);
    case SystemRole: return row.system;
    case RowIndexRole: return index.row();
    default: return {};
    }
}
QVariant OutputModel::headerData(int section, Qt::Orientation orientation, int role) const {
    if (orientation != Qt::Horizontal || role != Qt::DisplayRole) return {};
    const QStringList headers{QStringLiteral("名称"), QStringLiteral("类型"), QStringLiteral("数据"), QStringLiteral("关系")};
    return section >= 0 && section < headers.size() ? headers[section] : QVariant{};
}
QHash<int, QByteArray> OutputModel::roleNames() const {
    return {{Qt::DisplayRole, "display"}, {PortIdRole, "portId"}, {NameRole, "name"}, {TypeRole, "type"},
        {PreviewRole, "preview"}, {TooltipRole, "tooltip"}, {CountRole, "count"}, {SystemRole, "system"}, {RowIndexRole, "rowIndex"}};
}
}
