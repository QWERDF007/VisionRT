#include "ui/ToolPortListModel.h"

#include "core/ValueJson.h"
#include "ui/DisplayTypes.h"

namespace visionrt {

namespace {

// The editor renders a String value as its own text (see ValueJson::text), so it takes that text
// back literally; every other type is edited in the JSON form it is displayed in.
PortValue parseEditorValue(const QString &text, const TypeSpec &type)
{
    if (type == TypeSpec{Type::String})
        return PortValue{text.toStdString()};
    return ValueJson::parse(text, type);
}

QVariantMap referenceMap(const std::optional<PortRef> &reference)
{
    if (!reference) return {};
    return {{"stage", static_cast<int>(reference->stage)}, {"id", reference->id}};
}

const char *relationLabel(RelationKind kind)
{
    if (kind == RelationKind::Image)
        return "图像关联";
    if (kind == RelationKind::Alignment)
        return "对齐关系";
    return "同几何关系";
}

} // namespace

ToolPortListModel::ToolPortListModel(QObject *parent)
    : QAbstractListModel(parent)
{
}

void ToolPortListModel::setDraft(ToolDefinition *draft, PortStage stage, bool editable)
{
    beginResetModel();
    draft_    = draft;
    stage_    = stage;
    editable_ = editable;
    endResetModel();
}

bool ToolPortListModel::acceptsEdits() const noexcept
{
    return draft_ != nullptr && editable_;
}

int ToolPortListModel::fixedRowCount() const noexcept
{
    if (stage_ == PortStage::Inputs)
        return 1;
    if (stage_ == PortStage::Outputs)
        return 2;
    return 0;
}

bool ToolPortListModel::isFixedRow(int row) const noexcept
{
    return row >= 0 && row < fixedRowCount();
}

std::vector<PortDefinition> *ToolPortListModel::portVector() noexcept
{
    if (!draft_)
        return nullptr;
    switch (stage_)
    {
    case PortStage::Inputs:  return &draft_->inputs;
    case PortStage::Outputs: return &draft_->outputs;
    case PortStage::Init:    return &draft_->initParams;
    case PortStage::Train:   return &draft_->trainParams;
    }
    return nullptr;
}

const std::vector<PortDefinition> *ToolPortListModel::portVector() const noexcept
{
    if (!draft_)
        return nullptr;
    switch (stage_)
    {
    case PortStage::Inputs:  return &draft_->inputs;
    case PortStage::Outputs: return &draft_->outputs;
    case PortStage::Init:    return &draft_->initParams;
    case PortStage::Train:   return &draft_->trainParams;
    }
    return nullptr;
}

int ToolPortListModel::rowCount(const QModelIndex &parent) const
{
    if (parent.isValid() || !draft_)
        return 0;
    const auto *ports = portVector();
    const int businessCount = ports ? static_cast<int>(ports->size()) : 0;
    return fixedRowCount() + businessCount;
}

QVariant ToolPortListModel::data(const QModelIndex &index, int role) const
{
    if (!index.isValid() || !draft_)
        return {};

    const int row = index.row();
    const int fixedCount = fixedRowCount();

    // Fixed rows are a projection of the host-owned ports, never a second copy of their metadata.
    if (row < fixedCount)
    {
        const auto port = hostPort(stage_, static_cast<PortId>(row));
        switch (role)
        {
        case IdRole:          return static_cast<quint32>(port.id);
        case NameZhRole:      return QString::fromStdString(port.nameZh);
        case NameEnRole:      return QString();
        case TypeRole:        return ValueJson::encodeType(port.type).toVariantMap();
        case DisplayTypeRole: return displayTypeName(port.type);
        case DefaultValueRole:return isMissing(port.defaultValue) ? QString() : ValueJson::text(port.defaultValue, port.type);
        case IsFixedRole:     return true;
        case MinRole:         return QString();
        case MaxRole:         return QString();
        case StepRole:        return QString();
        case DecimalsRole:    return QString();
        case ImageRefRole:    return QVariantMap();
        case AlignmentRefRole:return QVariantMap();
        case SameGeometryRefRole: return QVariantMap();
        case OptionsTextRole: return QString();
        case TabRole:         return static_cast<int>(port.tab);
        case GroupRole:       return QString();
        case EditorRole:      return static_cast<int>(EditorKind::Default);
        }
        return {};
    }

    // Business rows
    const auto *ports = portVector();
    if (!ports)
        return {};

    const int businessIndex = row - fixedCount;
    if (businessIndex < 0 || businessIndex >= static_cast<int>(ports->size()))
        return {};

    const auto &port = (*ports)[businessIndex];
    switch (role)
    {
    case IdRole:
        return static_cast<quint32>(port.id);
    case NameZhRole:
        return QString::fromStdString(port.nameZh);
    case NameEnRole:
        return QString::fromStdString(port.nameEn);
    case TypeRole:
        return ValueJson::encodeType(port.type).toVariantMap();
    case DisplayTypeRole:
        return displayTypeName(port.type);
    case DefaultValueRole:
        return ValueJson::text(port.defaultValue, port.type);
    case IsFixedRole:
        return false;
    case MinRole:
        return port.minimum ? ValueJson::text(*port.minimum, port.type) : QString();
    case MaxRole:
        return port.maximum ? ValueJson::text(*port.maximum, port.type) : QString();
    case StepRole:
        return port.step ? ValueJson::text(*port.step, port.type) : QString();
    case DecimalsRole:
        return port.decimals ? QString::number(*port.decimals) : QString();
    case ImageRefRole:
        return referenceMap(port.image);
    case AlignmentRefRole:
        return referenceMap(port.alignment);
    case SameGeometryRefRole:
        return referenceMap(port.sameGeometryAs);
    case OptionsTextRole:
    {
        QStringList lines;
        for (const auto &option : port.options)
            lines.append(ValueJson::text(option, port.type));
        return lines.join(QLatin1Char('\n'));
    }
    case TabRole:
        return static_cast<int>(port.tab);
    case GroupRole:
        return QString::fromStdString(port.group);
    case EditorRole:
        return static_cast<int>(port.editor);
    case ShowImageRole:
        return port.imageVisible;
    }

    return {};
}

QHash<int, QByteArray> ToolPortListModel::roleNames() const
{
    return {
        {IdRole,           "portId"},
        {NameZhRole,       "nameZh"},
        {NameEnRole,       "nameEn"},
        {TypeRole,         "typeSpec"},
        {DisplayTypeRole,  "displayType"},
        {DefaultValueRole, "defaultValue"},
        {IsFixedRole,      "isFixed"},
        {MinRole,          "minValue"},
        {MaxRole,          "maxValue"},
        {StepRole,         "stepValue"},
        {DecimalsRole,     "decimalsValue"},
        {ImageRefRole,     "imageRef"},
        {AlignmentRefRole, "alignmentRef"},
        {SameGeometryRefRole, "sameGeometryRef"},
        {OptionsTextRole,  "optionsText"},
        {TabRole,          "tabName"},
        {GroupRole,        "groupName"},
        {EditorRole,       "editorType"},
        {ShowImageRole,    "showImage"}
    };
}

bool ToolPortListModel::addPort(const QString &nameZh, const QVariantMap &type, const QString &defaultValue)
{
    if (!acceptsEdits() || nameZh.trimmed().isEmpty())
        return false;

    // A function tool has no lifecycle of its own: the declaration validator refuses init parameters
    // for it, so the editor refuses the row here instead of failing at apply time.
    if (stage_ == PortStage::Init && draft_->kind == ToolKind::PythonFunction)
    {
        emit rejected(QStringLiteral("函数工具没有初始化参数，只有类工具才有"));
        return false;
    }

    auto *ports = portVector();
    if (!ports)
        return false;

    // The type and its default are settled before a number is handed out: a rejected add must not
    // burn a port id the user never sees.
    PortDefinition port;
    port.nameZh = nameZh.trimmed().toStdString();
    try
    {
        port.type = ValueJson::decodeType(QJsonObject::fromVariantMap(type));
        port.defaultValue = defaultValue.isEmpty() ? portDefault(port) : parseEditorValue(defaultValue, port.type);
    }
    catch (const std::exception &error)
    {
        emit rejected(QString::fromUtf8(error.what()));
        return false;
    }
    port.id = nextFreePortId(*draft_, stage_);

    const int newRow = fixedRowCount() + static_cast<int>(ports->size());
    beginInsertRows(QModelIndex(), newRow, newRow);
    ports->push_back(std::move(port));
    endInsertRows();

    emit draftModified();
    return true;
}

bool ToolPortListModel::removePort(int row)
{
    if (!acceptsEdits() || isFixedRow(row))
        return false;

    auto *ports = portVector();
    if (!ports)
        return false;

    const int businessIndex = row - fixedRowCount();
    if (businessIndex < 0 || businessIndex >= static_cast<int>(ports->size()))
        return false;

    const PortId id = (*ports)[businessIndex].id;

    // The declaration is reconciled first: a port other parameters still name must not simply
    // disappear, and every dropped reference has to reach the author. The row notification opens
    // before the data changes and closes again unchanged when the removal is refused.
    beginRemoveRows(QModelIndex(), row, row);
    std::vector<std::string> dropped;
    try
    {
        dropped = erasePort(*draft_, stage_, id);
    }
    catch (const std::exception &error)
    {
        endRemoveRows();
        emit rejected(QString::fromUtf8(error.what()));
        return false;
    }
    endRemoveRows();

    emit draftModified();
    if (!dropped.empty())
    {
        QStringList fields;
        for (const auto &field : dropped)
            fields.append(QString::fromStdString(field));
        emit cleared(QStringLiteral("已删除参数，并清除：%1").arg(fields.join(QStringLiteral("、"))));
    }
    return true;
}

bool ToolPortListModel::movePort(int fromRow, int toRow)
{
    if (!acceptsEdits() || isFixedRow(fromRow) || toRow < fixedRowCount())
        return false;

    auto *ports = portVector();
    if (!ports)
        return false;

    const int fixedCount = fixedRowCount();
    const int totalRows  = fixedCount + static_cast<int>(ports->size());
    if (fromRow < fixedCount || fromRow >= totalRows || toRow >= totalRows || fromRow == toRow)
        return false;

    const int destModelRow = toRow > fromRow ? toRow + 1 : toRow;
    if (!beginMoveRows(QModelIndex(), fromRow, fromRow, QModelIndex(), destModelRow))
        return false;

    const int fromBusiness = fromRow - fixedCount;
    const int toBusiness   = toRow - fixedCount;
    auto item = std::move((*ports)[fromBusiness]);
    ports->erase(ports->begin() + fromBusiness);
    ports->insert(ports->begin() + toBusiness, std::move(item));
    endMoveRows();

    emit draftModified();
    return true;
}

bool ToolPortListModel::updatePort(int row, const QVariantMap &properties)
{
    if (!acceptsEdits() || isFixedRow(row))
        return false;

    auto *ports = portVector();
    if (!ports)
        return false;

    const int businessIndex = row - fixedRowCount();
    if (businessIndex < 0 || businessIndex >= static_cast<int>(ports->size()))
        return false;

    // A type change is not a field edit: it reconciles the whole declaration, so it has its own
    // entry point instead of a map key that would silently ignore values and relations.
    if (properties.contains("typeSpec"))
    {
        emit rejected(QStringLiteral("改变参数类型请使用类型选择"));
        return false;
    }

    // The whole edit is validated before it touches the draft, so one rejected field cannot leave a
    // half-written port behind. A value the declaration cannot hold is reported, never swallowed.
    PortDefinition candidate = (*ports)[businessIndex];
    const auto parseValue = [](const QString &text, TypeSpec type, const char *label) -> std::optional<PortValue> {
        if (text.trimmed().isEmpty())
            return std::nullopt;
        try
        {
            return parseEditorValue(text.trimmed(), type);
        }
        catch (const std::exception &error)
        {
            throw ContractError(std::string(label) + " 不是合法的 " + typeName(type.type) + " 值：" + error.what());
        }
    };

    try
    {
        if (properties.contains("nameZh"))
        {
            const auto name = properties.value("nameZh").toString().trimmed();
            if (name.isEmpty())
                throw ContractError("参数名称不能为空");
            candidate.nameZh = name.toStdString();
        }
        if (properties.contains("nameEn"))
            candidate.nameEn = properties.value("nameEn").toString().trimmed().toStdString();
        if (properties.contains("tab"))
            candidate.tab = static_cast<ParameterTab>(properties.value("tab").toInt());
        if (properties.contains("group"))
            candidate.group = properties.value("group").toString().trimmed().toStdString();
        if (properties.contains("defaultValue"))
        {
            const auto value = parseValue(properties.value("defaultValue").toString(), candidate.type, "默认值");
            candidate.defaultValue = value ? *value : portDefault(candidate);
        }
        if (properties.contains("min"))
            candidate.minimum = parseValue(properties.value("min").toString(), candidate.type, "最小值");
        if (properties.contains("max"))
            candidate.maximum = parseValue(properties.value("max").toString(), candidate.type, "最大值");
        if (properties.contains("step"))
            candidate.step = parseValue(properties.value("step").toString(), candidate.type, "步长");
        if (properties.contains("decimals"))
        {
            const auto text = properties.value("decimals").toString().trimmed();
            if (text.isEmpty())
            {
                candidate.decimals.reset();
            }
            else
            {
                bool      ok    = false;
                const int value = text.toInt(&ok);
                if (!ok || value < 0)
                    throw ContractError("小数位必须是非负整数");
                candidate.decimals = value;
            }
        }
        if (properties.contains("editor"))
        {
            const auto editor = static_cast<EditorKind>(properties.value("editor").toInt());
            candidate.editor = editor;
        }
        const auto relation = [](const QVariant &value) -> std::optional<PortRef> {
            const auto fields = value.toMap();
            if (fields.isEmpty()) return std::nullopt;
            return PortRef{static_cast<PortStage>(fields.value("stage").toInt()), fields.value("id").toUInt()};
        };
        // Each relation is checked against the declaration being edited, not only against a type
        // table: a reference the declaration would reject never reaches the draft.
        if (properties.contains("image"))
        {
            auto reference = relation(properties.value("image"));
            if (reference)
                requireRelationTarget(candidate, RelationKind::Image, *reference);
            candidate.image = reference;
        }
        if (properties.contains("alignment"))
        {
            auto reference = relation(properties.value("alignment"));
            if (reference)
                requireRelationTarget(candidate, RelationKind::Alignment, *reference);
            candidate.alignment = reference;
        }
        if (properties.contains("same_geometry_as"))
        {
            auto reference = relation(properties.value("same_geometry_as"));
            if (reference)
                requireRelationTarget(candidate, RelationKind::SameGeometry, *reference);
            candidate.sameGeometryAs = reference;
        }
        if (properties.contains("options"))
        {
            candidate.options.clear();
            for (const auto &line : properties.value("options").toString().split(QLatin1Char('\n')))
                if (!line.trimmed().isEmpty())
                    candidate.options.push_back(parseEditorValue(line.trimmed(), candidate.type));
        }
        if (properties.contains("showImage"))
        {
            const bool visible = properties.value("showImage").toBool();
            if (visible && candidate.type.type != Type::Image)
                throw ContractError("只有图像参数可以加入图像显示列表");
            candidate.imageVisible = visible;
        }
    }
    catch (const std::exception &error)
    {
        emit rejected(QString::fromUtf8(error.what()));
        return false;
    }

    (*ports)[businessIndex] = std::move(candidate);
    emit dataChanged(index(row, 0), index(row, 0));
    emit draftModified();
    return true;
}

QStringList ToolPortListModel::typeChangeImpact(int row, const QVariantMap &type) const
{
    QStringList impact;
    if (!draft_ || isFixedRow(row))
        return impact;

    const auto *ports = portVector();
    if (!ports)
        return impact;
    const int businessIndex = row - fixedRowCount();
    if (businessIndex < 0 || businessIndex >= static_cast<int>(ports->size()))
        return impact;

    TypeSpec spec;
    try
    {
        spec = ValueJson::decodeType(QJsonObject::fromVariantMap(type));
    }
    catch (const std::exception &)
    {
        return impact;
    }

    // The impact is computed on a copy: previewing a change must never mutate the draft, and the
    // same reconciliation the commit runs decides what would be lost.
    ToolDefinition copy = *draft_;
    try
    {
        for (const auto &field : retypePort(copy, stage_, (*ports)[businessIndex].id, spec))
            impact.append(QString::fromStdString(field));
    }
    catch (const std::exception &)
    {
        // changeType() reports why the change is refused; there is no partial impact to preview.
        return {};
    }
    return impact;
}

bool ToolPortListModel::changeType(int row, const QVariantMap &type)
{
    if (!acceptsEdits() || isFixedRow(row))
        return false;

    auto *ports = portVector();
    if (!ports)
        return false;
    const int businessIndex = row - fixedRowCount();
    if (businessIndex < 0 || businessIndex >= static_cast<int>(ports->size()))
        return false;

    TypeSpec spec;
    try
    {
        spec = ValueJson::decodeType(QJsonObject::fromVariantMap(type));
    }
    catch (const std::exception &error)
    {
        emit rejected(QString::fromUtf8(error.what()));
        return false;
    }

    try
    {
        const auto clearedFields = retypePort(*draft_, stage_, (*ports)[businessIndex].id, spec);
        if (rowCount() > 0)
            emit dataChanged(index(0, 0), index(rowCount() - 1, 0));
        emit draftModified();
        if (!clearedFields.empty())
        {
            QStringList fields;
            for (const auto &field : clearedFields)
                fields.append(QString::fromStdString(field));
            emit cleared(QStringLiteral("已修改参数类型，清除：%1").arg(fields.join(QStringLiteral("、"))));
        }
        return true;
    }
    catch (const std::exception &error)
    {
        emit rejected(QString::fromUtf8(error.what()));
        return false;
    }
}

const PortDefinition *ToolPortListModel::findPort(PortStage stage, PortId id) const
{
    if (!draft_)
        return nullptr;
    const auto &ports = stage == PortStage::Train ? draft_->trainParams
                         : stage == PortStage::Init    ? draft_->initParams
                        : stage == PortStage::Inputs ? draft_->inputs
                                                     : draft_->outputs;
    const auto  found
        = std::find_if(ports.begin(), ports.end(), [id](const PortDefinition &port) { return port.id == id; });
    return found == ports.end() ? nullptr : &*found;
}

void ToolPortListModel::requireRelationTarget(const PortDefinition &port, RelationKind kind,
                                              const PortRef &reference) const
{
    const auto *target = findPort(reference.stage, reference.id);
    if (!target)
        throw ContractError(std::string(relationLabel(kind)) + " 引用了不存在的参数：" + formatPortRef(reference));

    const bool self = reference.stage == stage_ && reference.id == port.id;
    if (kind == RelationKind::Image || kind == RelationKind::SameGeometry)
    {
        if (self || target->type.type != Type::Image)
            throw ContractError(std::string(relationLabel(kind)) + " 必须指向图像参数：" + formatPortRef(reference));
        return;
    }
    if (kind == RelationKind::Alignment)
    {
        if (self || reference.stage != PortStage::Inputs
            || target->type != TypeSpec{Type::Array, DType::Float64, 3})
            throw ContractError(std::string(relationLabel(kind))
                                + " 必须指向另一个 Array(float64, 3) 输入：" + formatPortRef(reference));
        return;
    }
    if (self || reference.stage != stage_ || target->type != TypeSpec{Type::String} || port.editor != EditorKind::Path)
        throw ContractError(std::string(relationLabel(kind))
                            + " 必须指向同阶段另一个 String 参数，且当前参数使用 path 编辑器："
                            + formatPortRef(reference));
}

QVariantList ToolPortListModel::relationCandidates(int row, RelationKind kind) const
{
    QVariantList result;
    if (!draft_ || isFixedRow(row))
        return result;

    const auto *ports = portVector();
    if (!ports)
        return result;
    const int businessIndex = row - fixedRowCount();
    if (businessIndex < 0 || businessIndex >= static_cast<int>(ports->size()))
        return result;

    const auto &port = (*ports)[businessIndex];
    const auto  stageLabel = [](PortStage stage) {
        return stage == PortStage::Init ? "初始化" : stage == PortStage::Inputs ? "输入" : "输出";
    };

    // The candidate list is exactly what the relation rule accepts: the same check that guards an
    // edit decides what the editor may offer, so the two can never disagree.
    for (const auto stage : {PortStage::Init, PortStage::Inputs, PortStage::Outputs})
    {
        const auto &list = stage == PortStage::Init    ? draft_->initParams
                           : stage == PortStage::Inputs ? draft_->inputs
                                                        : draft_->outputs;
        for (const auto &other : list)
        {
            if (stage == stage_ && other.id == port.id)
                continue;
            const PortRef reference{stage, other.id};
            try
            {
                requireRelationTarget(port, kind, reference);
            }
            catch (const ContractError &)
            {
                continue;
            }
            QVariantMap item;
            item.insert("ref", referenceMap(reference));
            item.insert("label", QStringLiteral("%1：%2").arg(QString::fromUtf8(stageLabel(stage)),
                                                              QString::fromStdString(other.nameZh)));
            result.append(item);
        }
    }
    return result;
}

QVariantList ToolPortListModel::editorOptions(int row) const
{
    QVariantList result{QVariantMap{{"token", static_cast<int>(EditorKind::Default)}, {"label", "默认"}}};
    const auto *ports = portVector();
    const int business = row - fixedRowCount();
    if (!ports || business < 0 || business >= static_cast<int>(ports->size())) return result;
    for (const auto editor : editorChoices((*ports)[business].type))
        result.push_back(QVariantMap{{"token", static_cast<int>(editor)}, {"label", editorLabel(editor)}});
    return result;
}

QVariantMap ToolPortListModel::portAt(int row) const
{
    QVariantMap map;
    if (row < 0 || row >= rowCount())
        return map;

    const auto idx = index(row, 0);
    map.insert("portId", data(idx, IdRole));
    map.insert("nameZh", data(idx, NameZhRole));
    map.insert("nameEn", data(idx, NameEnRole));
    map.insert("typeSpec", data(idx, TypeRole));
    map.insert("displayType", data(idx, DisplayTypeRole));
    map.insert("defaultValue", data(idx, DefaultValueRole));
    map.insert("isFixed", data(idx, IsFixedRole));
    map.insert("minValue", data(idx, MinRole));
    map.insert("maxValue", data(idx, MaxRole));
    map.insert("stepValue", data(idx, StepRole));
    map.insert("decimalsValue", data(idx, DecimalsRole));
    map.insert("imageRef", data(idx, ImageRefRole));
    map.insert("alignmentRef", data(idx, AlignmentRefRole));
    map.insert("sameGeometryRef", data(idx, SameGeometryRefRole));
    map.insert("optionsText", data(idx, OptionsTextRole));
    map.insert("tabName", data(idx, TabRole));
    map.insert("groupName", data(idx, GroupRole));
    map.insert("editorType", data(idx, EditorRole));
    map.insert("showImage", data(idx, ShowImageRole));
    return map;
}

} // namespace visionrt
