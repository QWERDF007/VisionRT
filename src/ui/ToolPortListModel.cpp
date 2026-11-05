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

const char *relationLabel(const std::string &kind)
{
    if (kind == "image")
        return "图像关联";
    if (kind == "alignment")
        return "对齐关系";
    if (kind == "path_mode")
        return "路径模式";
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
        case TypeRole:        return QString::fromStdString(typeToken(port.type));
        case DisplayTypeRole: return displayTypeName(port.type);
        case DefaultValueRole:return isMissing(port.defaultValue) ? QString() : ValueJson::text(port.defaultValue, port.type);
        case IsFixedRole:     return true;
        case MinRole:         return QString();
        case MaxRole:         return QString();
        case StepRole:        return QString();
        case DecimalsRole:    return QString();
        case ImageRefRole:    return QString();
        case AlignmentRefRole:return QString();
        case PathModeRefRole: return QString();
        case SameGeometryRefRole: return QString();
        case OptionsTextRole: return QString();
        case TabRole:         return QString::fromStdString(port.tab);
        case GroupRole:       return QString();
        case EditorRole:      return QString();
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
        return QString::fromStdString(typeToken(port.type));
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
        return port.image ? QString::fromStdString(formatPortRef(*port.image)) : QString();
    case AlignmentRefRole:
        return port.alignment ? QString::fromStdString(formatPortRef(*port.alignment)) : QString();
    case PathModeRefRole:
        return port.pathModeFrom ? QString::fromStdString(formatPortRef(*port.pathModeFrom)) : QString();
    case SameGeometryRefRole:
        return port.sameGeometryAs ? QString::fromStdString(formatPortRef(*port.sameGeometryAs)) : QString();
    case OptionsTextRole:
    {
        QStringList lines;
        for (const auto &option : port.options)
            lines.append(ValueJson::text(option, port.type));
        return lines.join(QLatin1Char('\n'));
    }
    case TabRole:
        return QString::fromStdString(port.tab);
    case GroupRole:
        return QString::fromStdString(port.group);
    case EditorRole:
        return QString::fromStdString(port.editor);
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
        {TypeRole,         "typeToken"},
        {DisplayTypeRole,  "displayType"},
        {DefaultValueRole, "defaultValue"},
        {IsFixedRole,      "isFixed"},
        {MinRole,          "minValue"},
        {MaxRole,          "maxValue"},
        {StepRole,         "stepValue"},
        {DecimalsRole,     "decimalsValue"},
        {ImageRefRole,     "imageRef"},
        {AlignmentRefRole, "alignmentRef"},
        {PathModeRefRole,  "pathModeRef"},
        {SameGeometryRefRole, "sameGeometryRef"},
        {OptionsTextRole,  "optionsText"},
        {TabRole,          "tabName"},
        {GroupRole,        "groupName"},
        {EditorRole,       "editorType"},
        {ShowImageRole,    "showImage"}
    };
}

bool ToolPortListModel::addPort(const QString &nameZh, const QString &typeToken, const QString &defaultValue)
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
        port.type = parseTypeToken(typeToken.toStdString());
        port.defaultValue = defaultValue.isEmpty() ? portDefault(port) : parseEditorValue(defaultValue, port.type);
    }
    catch (const std::exception &error)
    {
        emit rejected(QString::fromUtf8(error.what()));
        return false;
    }
    port.id = allocatePortId(*draft_, stage_);

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
    if (properties.contains("typeToken"))
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
            throw ContractError(std::string(label) + " 不是合法的 " + typeToken(type) + " 值：" + error.what());
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
            candidate.tab = properties.value("tab").toString().trimmed().toStdString();
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
            const std::string editor = properties.value("editor").toString().trimmed().toStdString();
            if (!editor.empty())
            {
                const auto choices = editorChoices(candidate.type);
                if (std::find(choices.begin(), choices.end(), std::string_view{editor}) == choices.end())
                    throw ContractError("编辑器 " + editor + " 不能用于 " + typeToken(candidate.type) + " 参数");
            }
            // A path mode names another String parameter and only means anything with the path
            // editor; dropping the editor silently would leave the declaration lying.
            if (editor != "path" && candidate.pathModeFrom)
                throw ContractError("请先清除路径模式再修改编辑器");
            candidate.editor = editor;
        }
        const auto relation = [&candidate](const QVariant &value, const std::string &kind) -> std::optional<PortRef> {
            const auto text = value.toString().trimmed();
            if (text.isEmpty())
                return std::nullopt;
            PortRef reference;
            try
            {
                reference = parsePortRef(text.toStdString());
            }
            catch (const std::exception &error)
            {
                throw ContractError(std::string(relationLabel(kind)) + "：" + error.what());
            }
            return reference;
        };
        // Each relation is checked against the declaration being edited, not only against a type
        // table: a reference the declaration would reject never reaches the draft.
        if (properties.contains("image"))
        {
            auto reference = relation(properties.value("image"), "image");
            if (reference)
                requireRelationTarget(candidate, "image", *reference);
            candidate.image = reference;
        }
        if (properties.contains("alignment"))
        {
            auto reference = relation(properties.value("alignment"), "alignment");
            if (reference)
                requireRelationTarget(candidate, "alignment", *reference);
            candidate.alignment = reference;
        }
        if (properties.contains("path_mode"))
        {
            auto reference = relation(properties.value("path_mode"), "path_mode");
            if (reference)
                requireRelationTarget(candidate, "path_mode", *reference);
            candidate.pathModeFrom = reference;
        }
        if (properties.contains("same_geometry_as"))
        {
            auto reference = relation(properties.value("same_geometry_as"), "same_geometry_as");
            if (reference)
                requireRelationTarget(candidate, "same_geometry_as", *reference);
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

QStringList ToolPortListModel::typeChangeImpact(int row, const QString &typeToken) const
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

    TypeSpec type;
    try
    {
        type = parseTypeToken(typeToken.toStdString());
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
        for (const auto &field : retypePort(copy, stage_, (*ports)[businessIndex].id, type))
            impact.append(QString::fromStdString(field));
    }
    catch (const std::exception &)
    {
        // changeType() reports why the change is refused; there is no partial impact to preview.
        return {};
    }
    return impact;
}

bool ToolPortListModel::changeType(int row, const QString &typeToken)
{
    if (!acceptsEdits() || isFixedRow(row))
        return false;

    auto *ports = portVector();
    if (!ports)
        return false;
    const int businessIndex = row - fixedRowCount();
    if (businessIndex < 0 || businessIndex >= static_cast<int>(ports->size()))
        return false;

    TypeSpec type;
    try
    {
        type = parseTypeToken(typeToken.toStdString());
    }
    catch (const std::exception &error)
    {
        emit rejected(QString::fromUtf8(error.what()));
        return false;
    }

    try
    {
        const auto clearedFields = retypePort(*draft_, stage_, (*ports)[businessIndex].id, type);
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
    const auto &ports = stage == PortStage::Init    ? draft_->initParams
                        : stage == PortStage::Inputs ? draft_->inputs
                                                     : draft_->outputs;
    const auto  found
        = std::find_if(ports.begin(), ports.end(), [id](const PortDefinition &port) { return port.id == id; });
    return found == ports.end() ? nullptr : &*found;
}

void ToolPortListModel::requireRelationTarget(const PortDefinition &port, const std::string &kind,
                                              const PortRef &reference) const
{
    const auto *target = findPort(reference.stage, reference.id);
    if (!target)
        throw ContractError(std::string(relationLabel(kind)) + " 引用了不存在的参数：" + formatPortRef(reference));

    const bool self = reference.stage == stage_ && reference.id == port.id;
    if (kind == "image" || kind == "same_geometry_as")
    {
        if (self || target->type.type != Type::Image)
            throw ContractError(std::string(relationLabel(kind)) + " 必须指向图像参数：" + formatPortRef(reference));
        return;
    }
    if (kind == "alignment")
    {
        if (self || reference.stage != PortStage::Inputs
            || target->type != TypeSpec{Type::Array, DType::Float64, 3})
            throw ContractError(std::string(relationLabel(kind))
                                + " 必须指向另一个 Array(float64, 3) 输入：" + formatPortRef(reference));
        return;
    }
    if (self || reference.stage != stage_ || target->type != TypeSpec{Type::String} || port.editor != "path")
        throw ContractError(std::string(relationLabel(kind))
                            + " 必须指向同阶段另一个 String 参数，且当前参数使用 path 编辑器："
                            + formatPortRef(reference));
}

QVariantList ToolPortListModel::relationCandidates(int row, const QString &kind) const
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

    const std::string relation = kind.toStdString();
    if (relation != "image" && relation != "alignment" && relation != "path_mode"
        && relation != "same_geometry_as")
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
                requireRelationTarget(port, relation, reference);
            }
            catch (const ContractError &)
            {
                continue;
            }
            QVariantMap item;
            item.insert("ref", QString::fromStdString(formatPortRef(reference)));
            item.insert("label", QStringLiteral("%1：%2").arg(QString::fromUtf8(stageLabel(stage)),
                                                              QString::fromStdString(other.nameZh)));
            result.append(item);
        }
    }
    return result;
}

QVariantList ToolPortListModel::editorOptions(int row) const
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

    result.append(QVariantMap{{"token", QString()}, {"label", QStringLiteral("默认")}});
    for (const auto choice : editorChoices((*ports)[businessIndex].type))
    {
        const auto token = QString::fromStdString(std::string(choice));
        QString    label = token;
        if (token == "number")
            label = "数字";
        else if (token == "text")
            label = "文本";
        else if (token == "path")
            label = "路径";
        else if (token == "bool" || token == "switch")
            label = "开关";
        else if (token == "roi")
            label = "ROI 列表";
        else if (token == "rect")
            label = "矩形";
        else if (token == "array")
            label = "数组";
        else if (token == "recursive")
            label = "递归数组";
        result.append(QVariantMap{{"token", token}, {"label", label}});
    }
    return result;
}

int ToolPortListModel::rowForPortId(quint32 portId) const
{
    const int count = rowCount();
    for (int r = 0; r < count; ++r)
    {
        if (data(index(r, 0), IdRole).toUInt() == portId)
            return r;
    }
    return -1;
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
    map.insert("typeToken", data(idx, TypeRole));
    map.insert("displayType", data(idx, DisplayTypeRole));
    map.insert("defaultValue", data(idx, DefaultValueRole));
    map.insert("isFixed", data(idx, IsFixedRole));
    map.insert("minValue", data(idx, MinRole));
    map.insert("maxValue", data(idx, MaxRole));
    map.insert("stepValue", data(idx, StepRole));
    map.insert("decimalsValue", data(idx, DecimalsRole));
    map.insert("imageRef", data(idx, ImageRefRole));
    map.insert("alignmentRef", data(idx, AlignmentRefRole));
    map.insert("pathModeRef", data(idx, PathModeRefRole));
    map.insert("sameGeometryRef", data(idx, SameGeometryRefRole));
    map.insert("optionsText", data(idx, OptionsTextRole));
    map.insert("tabName", data(idx, TabRole));
    map.insert("groupName", data(idx, GroupRole));
    map.insert("editorType", data(idx, EditorRole));
    map.insert("showImage", data(idx, ShowImageRole));
    return map;
}

} // namespace visionrt
