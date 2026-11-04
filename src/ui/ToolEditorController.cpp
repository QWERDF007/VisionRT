#include "ui/ToolEditorController.h"

#include "core/DefinitionJson.h"
#include "core/Project.h"
#include "ui/AppController.h"
#include "ui/DisplayTypes.h"

#include <QDesktopServices>
#include <QDir>
#include <QFileInfo>
#include <QStringList>
#include <QUrl>
#include <algorithm>
#include <set>

namespace visionrt {

namespace {

const char *toolKindString(ToolKind kind)
{
    switch (kind)
    {
    case ToolKind::Cpp:            return "cpp";
    case ToolKind::Host:           return "host";
    case ToolKind::PythonFunction: return "python_function";
    case ToolKind::PythonClass:    return "python_class";
    }
    return "unknown";
}

PortStage parseStage(const QString &stage)
{
    if (stage == QStringLiteral("outputs"))
        return PortStage::Outputs;
    if (stage == QStringLiteral("init"))
        return PortStage::Init;
    return PortStage::Inputs;
}

bool sameReference(const std::optional<PortRef> &left, const std::optional<PortRef> &right)
{
    if (left.has_value() != right.has_value())
        return false;
    return !left || (left->stage == right->stage && left->id == right->id);
}

bool sameOptionalValue(const std::optional<PortValue> &left, const std::optional<PortValue> &right)
{
    if (left.has_value() != right.has_value())
        return false;
    return !left || equalValues(*left, *right);
}

bool samePort(const PortDefinition &left, const PortDefinition &right)
{
    return left.id == right.id && left.nameZh == right.nameZh && left.nameEn == right.nameEn
           && left.type == right.type && left.tab == right.tab && left.group == right.group
           && left.editor == right.editor && left.optionsProvider == right.optionsProvider
           && left.colorOrder == right.colorOrder && left.decimals == right.decimals
           && equalValues(left.defaultValue, right.defaultValue)
           && sameOptionalValue(left.minimum, right.minimum)
           && sameOptionalValue(left.maximum, right.maximum)
           && sameOptionalValue(left.step, right.step)
           && sameReference(left.image, right.image) && sameReference(left.alignment, right.alignment)
           && sameReference(left.pathModeFrom, right.pathModeFrom)
           && sameReference(left.sameGeometryAs, right.sameGeometryAs)
           && left.typeFrom == right.typeFrom
           && left.options.size() == right.options.size()
           && std::equal(left.options.begin(), left.options.end(), right.options.begin(), equalValues);
}

bool samePorts(const std::vector<PortDefinition> &a, const std::vector<PortDefinition> &b)
{
    return std::equal(a.begin(), a.end(), b.begin(), b.end(), samePort);
}

QString portIdList(const std::set<int> &ids)
{
    QStringList parts;
    for (const int id : ids)
        parts.append(QString::number(id));
    return parts.join(QStringLiteral("/"));
}

} // namespace

ToolEditorController::ToolEditorController(QObject *parent)
    : QObject(parent)
    , ownedStore_(std::make_unique<CustomToolStore>())
    , store_(ownedStore_.get())
{
    connect(&portModel_, &ToolPortListModel::draftModified, this, [this] {
        updateDirty();
    });
    connect(&portModel_, &ToolPortListModel::rejected, this, [this](const QString &message) {
        setStatus(message, true);
    });
    connect(&portModel_, &ToolPortListModel::cleared, this, [this](const QString &message) {
        setStatus(message);
    });
}

ToolEditorController::~ToolEditorController() = default;

void ToolEditorController::setAppController(AppController *app)
{
    app_ = app;
    if (app_)
    {
        connect(app_, &AppController::catalogChanged, this, &ToolEditorController::toolsChanged);
        // Opening or switching a project changes what the pending draft would break.
        connect(app_, &AppController::projectChanged, this, &ToolEditorController::updateImpactSummary);
    }
}

void ToolEditorController::setCustomStore(CustomToolStore *store)
{
    store_ = store ? store : ownedStore_.get();
    emit toolsChanged();
}

QVariantList ToolEditorController::tools() const
{
    QVariantList                        list;
    std::map<std::string, ToolDefinition> allDefs;

    // Installed tools are listed too: the definition can be read and compared, but only a tool this
    // store owns may be edited or applied. The registry is the one catalog the runtime also uses.
    if (app_)
        for (const auto &def : app_->toolCatalog())
            allDefs.emplace(def.nameEn, def);
    if (store_)
        for (const auto &tool : store_->discoverCustomTools())
            allDefs.insert_or_assign(tool.definition.nameEn, tool.definition);

    for (const auto &[id, def] : allDefs)
    {
        QVariantMap item;
        item.insert("id", QString::fromStdString(def.nameEn));
        item.insert("name", QString::fromStdString(def.nameZh));
        item.insert("kind", QString::fromLatin1(toolKindString(def.kind)));
        item.insert("category", QString::fromStdString(categoryPath(def)));
        item.insert("isEditable", store_ ? store_->isUserTool(def) : false);
        item.insert("isSaved", true);
        list.append(item);
    }

    if (draft_ && (!saved_ || draft_->nameEn != saved_->nameEn))
    {
        bool found = false;
        for (const auto &item : list)
        {
            if (item.toMap().value("id").toString() == QString::fromStdString(draft_->nameEn))
            {
                found = true;
                break;
            }
        }
        if (!found)
        {
            QVariantMap item;
            item.insert("id", QString::fromStdString(draft_->nameEn));
            item.insert("name", QString::fromStdString(draft_->nameZh));
            item.insert("kind", QString::fromLatin1(toolKindString(draft_->kind)));
            item.insert("category", QString::fromStdString(categoryPath(*draft_)));
            item.insert("isEditable", true);
            item.insert("isSaved", false);
            list.append(item);
        }
    }

    return list;
}

QString ToolEditorController::currentToolId() const
{
    return draft_ ? QString::fromStdString(draft_->nameEn) : QString();
}

bool ToolEditorController::isToolIdEditable() const noexcept
{
    return draft_.has_value() && !saved_.has_value();
}

bool ToolEditorController::toolIdTaken(const std::string &toolId) const
{
    if (draft_ && draft_->nameEn == toolId)
        return true;
    if (store_ && store_->findCustomTool(toolId))
        return true;
    if (!app_)
        return false;
    const auto &definitions = app_->toolCatalog();
    return std::any_of(definitions.begin(), definitions.end(),
                       [&toolId](const ToolDefinition &def) { return def.nameEn == toolId; });
}

void ToolEditorController::setCurrentToolId(const QString &value)
{
    if (!draft_)
        return;
    const std::string id = value.trimmed().toStdString();
    if (!isToolIdEditable())
    {
        setStatus(QStringLiteral("工具标识在首次应用后不能修改"), true);
        emit currentToolChanged();
        return;
    }
    if (id == draft_->nameEn)
        return;
    try
    {
        validateIdentifier(id, false);
    }
    catch (const std::exception &error)
    {
        setStatus(QString::fromUtf8(error.what()), true);
        emit currentToolChanged();
        return;
    }
    if (toolIdTaken(id))
    {
        setStatus(QStringLiteral("工具标识已存在：%1").arg(QString::fromStdString(id)), true);
        emit currentToolChanged();
        return;
    }

    // The code package follows the id, because the id is what names the declaration file. Both are
    // only editable before the first commit, so no published path is ever rewritten here.
    draft_->nameEn = id;
    std::string package = id;
    std::replace(package.begin(), package.end(), '.', '/');
    draft_->entry.package = package;

    updateDirty();
    emit currentToolChanged();
    emit toolsChanged();
}

QString ToolEditorController::currentToolName() const
{
    return draft_ ? QString::fromStdString(draft_->nameZh) : QString();
}

void ToolEditorController::setCurrentToolName(const QString &value)
{
    if (draft_ && draft_->nameZh != value.trimmed().toStdString())
    {
        draft_->nameZh = value.trimmed().toStdString();
        updateDirty();
        emit currentToolChanged();
    }
}

QString ToolEditorController::currentToolKind() const
{
    return draft_ ? QString::fromLatin1(toolKindString(draft_->kind)) : QString();
}

QString ToolEditorController::currentToolCategory() const
{
    if (!draft_)
        return QStringLiteral("自定义");
    return QString::fromStdString(categoryPath(*draft_));
}

void ToolEditorController::setCurrentToolCategory(const QString &value)
{
    if (draft_)
    {
        const std::string cat = value.trimmed().toStdString();
        if (draft_->categories.empty() || draft_->categories.front() != cat)
        {
            draft_->categories = {cat};
            updateDirty();
            emit currentToolChanged();
        }
    }
}

QString ToolEditorController::currentToolVersion() const
{
    return draft_ ? QString::fromStdString(draft_->version) : QStringLiteral("1.0.0");
}

void ToolEditorController::setCurrentToolVersion(const QString &value)
{
    if (draft_ && draft_->version != value.trimmed().toStdString())
    {
        draft_->version = value.trimmed().toStdString();
        updateDirty();
        emit currentToolChanged();
    }
}

QString ToolEditorController::currentToolScript() const
{
    if (!draft_ || !store_)
        return QString();
    if (draft_->kind != ToolKind::PythonFunction && draft_->kind != ToolKind::PythonClass)
        return QString();

    // A published tool resolves its script through the roots it was discovered under; a draft that
    // has not been applied yet will live under this store, so that is the path it shows.
    if (!draft_->definitionPath.empty() && !draft_->source.roots.python.empty())
        return QString::fromStdWString(draft_->pythonScript().wstring());

    const auto file = draft_->entry.file.empty() ? std::filesystem::path{"tool.py"} : draft_->entry.file;
    return QString::fromStdWString((store_->pythonDir() / draft_->entry.package / file).wstring());
}

QString ToolEditorController::currentStage() const
{
    return stage_;
}

void ToolEditorController::setCurrentStage(const QString &stage)
{
    if (stage_ != stage)
    {
        stage_ = stage;
        if (draft_)
            bindDraftToPortModel();
        emit stageChanged();
    }
}

bool ToolEditorController::isEditable() const noexcept
{
    if (!draft_)
        return false;
    // A draft that was never applied is ours by definition; a loaded one is editable only when the
    // persisted declaration lives under this store, so an installed tool stays read-only.
    if (!saved_)
        return true;
    return store_ && store_->isUserTool(*draft_);
}

bool ToolEditorController::canClone() const
{
    return draft_ && (draft_->kind == ToolKind::PythonFunction || draft_->kind == ToolKind::PythonClass);
}

void ToolEditorController::bindDraftToPortModel()
{
    portModel_.setDraft(draft_ ? &*draft_ : nullptr, parseStage(stage_), isEditable());
}

bool ToolEditorController::isDirty() const noexcept
{
    return dirty_;
}

bool ToolEditorController::canApply() const noexcept
{
    return draft_.has_value() && !draft_->nameEn.empty() && !draft_->nameZh.empty();
}

QString ToolEditorController::statusMessage() const
{
    return statusMessage_;
}

bool ToolEditorController::isStatusError() const noexcept
{
    return statusError_;
}

void ToolEditorController::setStatus(const QString &message, bool isError)
{
    statusMessage_ = message;
    statusError_   = isError;
    emit statusChanged();
}

void ToolEditorController::updateDirty()
{
    bool dirty = false;
    if (!saved_)
        dirty = draft_.has_value();
    else if (draft_)
    {
        dirty = (draft_->nameEn != saved_->nameEn ||
                 draft_->nameZh != saved_->nameZh ||
                 draft_->version != saved_->version ||
                 draft_->categories != saved_->categories ||
                 !samePorts(draft_->inputs, saved_->inputs) ||
                 !samePorts(draft_->outputs, saved_->outputs) ||
                 !samePorts(draft_->initParams, saved_->initParams));
    }
    if (dirty_ != dirty)
    {
        dirty_ = dirty;
        emit dirtyChanged();
    }
    updateImpactSummary();
}

void ToolEditorController::updateImpactSummary()
{
    QString summary = QStringLiteral("当前工程无引用该工具的节点");
    if (app_ && draft_ && dirty_)
    {
        const auto &project = app_->project();
        const auto declared = [this](PortStage stage, PortId id) {
            const auto &ports = stage == PortStage::Outputs   ? draft_->outputs
                                : stage == PortStage::Inputs  ? draft_->inputs
                                                              : draft_->initParams;
            return std::any_of(ports.begin(), ports.end(), [id](const PortDefinition &port) { return port.id == id; });
        };

        std::set<int> unboundInputs;
        std::set<int> retypedInputs;
        std::set<int> droppedOutputs;
        std::set<NodeId> ownNodes;
        const int     nodes = referencingNodes(draft_->nameEn);
        for (const auto &flow : project.flows)
        {
            for (const auto &node : flow.nodes)
            {
                if (node.toolId != draft_->nameEn)
                    continue;
                ownNodes.insert(node.id);
                for (const auto &[id, binding] : node.inputs)
                {
                    (void)binding;
                    if (!isReservedPort(PortStage::Inputs, id) && !declared(PortStage::Inputs, id))
                        unboundInputs.insert(static_cast<int>(id));
                }
                for (const auto &[id, type] : node.signatures.inputs)
                {
                    const auto port = std::find_if(draft_->inputs.begin(), draft_->inputs.end(),
                                                   [id](const PortDefinition &item) { return item.id == id; });
                    if (port != draft_->inputs.end() && !(port->type == type))
                        retypedInputs.insert(static_cast<int>(id));
                }
            }
        }

        // A deleted output matters only where another node reads it; the binding lives in the
        // consumer, so it has to be found by following the sources of every node.
        if (!ownNodes.empty())
        {
            for (const auto &flow : project.flows)
                for (const auto &node : flow.nodes)
                    for (const auto &[id, binding] : node.inputs)
                        if (binding.source && ownNodes.count(binding.source->node)
                            && !isReservedPort(PortStage::Outputs, binding.source->port)
                            && !declared(PortStage::Outputs, binding.source->port))
                            droppedOutputs.insert(static_cast<int>(binding.source->port));
        }

        if (nodes > 0)
        {
            summary = QStringLiteral("%1 个节点引用该工具").arg(nodes);
            if (!unboundInputs.empty())
                summary += QStringLiteral("；删除的输入参数 %1 会解除绑定").arg(portIdList(unboundInputs));
            if (!droppedOutputs.empty())
                summary += QStringLiteral("；删除的输出参数 %1 会断开下游连接").arg(portIdList(droppedOutputs));
            if (!retypedInputs.empty())
                summary += QStringLiteral("；输入参数 %1 的类型已变更").arg(portIdList(retypedInputs));
            if (unboundInputs.empty() && droppedOutputs.empty() && retypedInputs.empty())
                summary += QStringLiteral("；本次修改不影响现有绑定");
        }
    }
    impactSummary_ = summary;
    emit impactSummaryChanged();
}

QString ToolEditorController::impactSummary() const
{
    return impactSummary_;
}

int ToolEditorController::toolUsage(const QString &toolId) const
{
    return referencingNodes(toolId.toStdString());
}

int ToolEditorController::referencingNodes(const std::string &toolId) const
{
    if (!app_ || toolId.empty())
        return 0;
    int count = 0;
    for (const auto &flow : app_->project().flows)
        for (const auto &node : flow.nodes)
            if (node.toolId == toolId)
                ++count;
    return count;
}

QStringList ToolEditorController::tabs() const
{
    QStringList list;
    for (const auto tab : toolTabs())
        list.append(QString::fromStdString(std::string(tab)));
    return list;
}

QVariantList ToolEditorController::supportedTypes() const
{
    QVariantList list;

    // 6 base types
    const std::array<Type, 6> baseTypes{Type::Int, Type::Float, Type::Bool, Type::String, Type::Image, Type::ROI};
    for (const auto t : baseTypes)
    {
        const TypeSpec spec{.type = t};
        QVariantMap item;
        item.insert("token", QString::fromStdString(typeToken(spec)));
        item.insert("label", displayTypeName(spec));
        item.insert("rank", 0);
        item.insert("dtype", QString());
        list.append(item);
    }

    // 28 vector types: 4 ranks x 7 dtypes
    const std::array<DType, 7> dtypes{
        DType::UInt8, DType::Int32, DType::Int64, DType::Float32, DType::Float64, DType::Bool, DType::String
    };
    for (int rank = 1; rank <= 4; ++rank)
    {
        for (const auto dt : dtypes)
        {
            const TypeSpec spec{.type = Type::Array, .dtype = dt, .rank = rank};
            QVariantMap item;
            item.insert("token", QString::fromStdString(typeToken(spec)));
            item.insert("label", displayTypeName(spec));
            item.insert("rank", rank);
            item.insert("dtype", QString::fromLatin1(dtypeName(dt)));
            list.append(item);
        }
    }

    return list;
}

void ToolEditorController::selectTool(const QString &toolId)
{
    if (toolId.isEmpty())
        return;
    if (dirty_)
    {
        setStatus(QStringLiteral("当前草稿有未保存的修改，请先应用或撤销"), true);
        return;
    }

    const std::string id = toolId.toStdString();
    // The lookup starts from nothing: a previous selection must not survive as the "not found"
    // fallback of the next one.
    draft_.reset();
    if (store_)
    {
        if (const auto tool = store_->findCustomTool(id))
            draft_ = tool->definition;
    }
    // An installed tool is listed as well; it opens read-only so its ports and types can be read
    // without becoming an editable second definition.
    if (!draft_ && app_)
    {
        const auto &definitions = app_->toolCatalog();
        const auto  found = std::find_if(definitions.begin(), definitions.end(),
                                        [&id](const ToolDefinition &def) { return def.nameEn == id; });
        if (found != definitions.end())
            draft_ = *found;
    }
    if (!draft_)
    {
        setStatus(QStringLiteral("未找到工具：%1").arg(toolId), true);
        return;
    }

    // The draft is a declaration: host-owned ports stay out of it, because the port model already
    // projects them as the fixed prefix.
    stripHostPorts(*draft_);
    saved_ = draft_;
    bindDraftToPortModel();
    updateDirty();
    emit currentToolChanged();
    setStatus(isEditable() ? QStringLiteral("已加载工具：%1").arg(toolId)
                           : QStringLiteral("已加载内置工具（只读）：%1").arg(toolId));
}

bool ToolEditorController::newTool(const QString &nameZh, const QString &nameEn, const QString &kind, const QString &category)
{
    if (nameZh.trimmed().isEmpty() || nameEn.trimmed().isEmpty())
    {
        setStatus(QStringLiteral("工具标识与名称不能为空"), true);
        return false;
    }
    // Starting another draft would drop the edits this one holds; the editor asks for an explicit
    // apply or revert first, exactly as switching tools does.
    if (dirty_)
    {
        setStatus(QStringLiteral("当前草稿有未保存的修改，请先应用或撤销"), true);
        return false;
    }
    const std::string id = nameEn.trimmed().toStdString();
    try
    {
        validateIdentifier(id, false);
    }
    catch (const std::exception &error)
    {
        setStatus(QString::fromUtf8(error.what()), true);
        return false;
    }
    // Publishing over an existing id would replace another tool's declaration and code; a new tool
    // has to take a number nothing else owns.
    if (toolIdTaken(id))
    {
        setStatus(QStringLiteral("工具标识已存在：%1").arg(QString::fromStdString(id)), true);
        return false;
    }

    ToolDefinition def;
    def.schemaVersion    = 6;
    def.nameZh           = nameZh.trimmed().toStdString();
    def.nameEn           = id;
    def.version          = "1.0.0";
    def.kind             = (kind == QStringLiteral("python_class")) ? ToolKind::PythonClass : ToolKind::PythonFunction;
    def.categories       = {category.trimmed().isEmpty() ? "自定义" : category.trimmed().toStdString()};
    def.nextInitPortId   = 0;
    def.nextInputPortId  = 1;
    def.nextOutputPortId = 2;

    std::string pkg = def.nameEn;
    std::replace(pkg.begin(), pkg.end(), '.', '/');
    def.entry.package = pkg;
    def.entry.file    = "tool.py";
    def.entry.symbol  = (def.kind == ToolKind::PythonFunction) ? "execute" : "Tool";

    draft_ = std::move(def);
    saved_ = std::nullopt;
    bindDraftToPortModel();
    updateDirty();
    emit currentToolChanged();
    emit toolsChanged();
    setStatus(QStringLiteral("已创建草稿：%1").arg(nameZh));
    return true;
}

bool ToolEditorController::cloneTool(const QString &toolId)
{
    if (!store_)
        return false;
    // Copying replaces the active draft; unsaved edits must not disappear as a side effect.
    if (dirty_)
    {
        setStatus(QStringLiteral("当前草稿有未保存的修改，请先应用或撤销"), true);
        return false;
    }

    const std::string id = toolId.toStdString();
    ToolDefinition source;
    bool found = false;

    if (draft_ && draft_->nameEn == id)
    {
        source = *draft_;
        found = true;
    }
    else
    {
        for (const auto &tool : store_->discoverCustomTools())
        {
            if (tool.definition.nameEn == id)
            {
                source = tool.definition;
                found = true;
                break;
            }
        }
    }

    // An installed Python example can be copied into the user root and edited there: its code lives
    // in the package the catalog already loaded, so there is something real to carry over.
    if (!found && app_)
    {
        const auto &definitions = app_->toolCatalog();
        const auto  match = std::find_if(definitions.begin(), definitions.end(),
                                        [&id](const ToolDefinition &def) { return def.nameEn == id; });
        if (match != definitions.end())
        {
            source = *match;
            found = true;
        }
    }

    if (!found)
    {
        setStatus(QStringLiteral("未找到要复制的工具：%1").arg(toolId), true);
        return false;
    }

    int suffix = 1;
    std::string newId;
    while (store_->findCustomTool(source.nameEn + "_copy" + std::to_string(suffix)).has_value())
        ++suffix;
    newId = source.nameEn + "_copy" + std::to_string(suffix);

    const std::string newName = source.nameZh + " 副本" + (suffix == 1 ? "" : " " + std::to_string(suffix));
    ToolDefinition    cloned;
    try
    {
        cloned = store_->cloneCustomTool(source, newId, newName);
    }
    catch (const std::exception &error)
    {
        setStatus(QString::fromUtf8(error.what()), true);
        return false;
    }
    draft_ = cloned;
    stripHostPorts(*draft_);
    saved_ = draft_;
    bindDraftToPortModel();
    updateDirty();
    emit currentToolChanged();
    emit toolsChanged();
    // The copy is already on disk; the catalog refreshes on the same idle-only path the apply uses.
    if (app_ && !app_->busy())
        app_->rescanTools();
    setStatus(QStringLiteral("已复制工具为：%1").arg(QString::fromStdString(draft_->nameZh)));
    return true;
}

bool ToolEditorController::deleteTool(const QString &toolId)
{
    if (!store_)
        return false;
    if (app_ && app_->busy())
    {
        setStatus(QStringLiteral("运行时正在工作，停止后才能删除工具"), true);
        return false;
    }

    const std::string id = toolId.toStdString();
    if (!store_->findCustomTool(id))
    {
        setStatus(QStringLiteral("只有自有工具可以删除：%1").arg(toolId), true);
        return false;
    }
    try
    {
        if (!store_->deleteCustomTool(id))
        {
            setStatus(QStringLiteral("未找到要删除的工具：%1").arg(toolId), true);
            return false;
        }
    }
    catch (const std::exception &error)
    {
        setStatus(QStringLiteral("删除失败：%1").arg(QString::fromUtf8(error.what())), true);
        return false;
    }

    if (draft_ && draft_->nameEn == id)
    {
        draft_.reset();
        saved_.reset();
        bindDraftToPortModel();
        updateDirty();
        emit currentToolChanged();
    }

    emit toolsChanged();
    if (app_)
        app_->rescanTools();

    setStatus(QStringLiteral("已删除工具：%1").arg(toolId));
    return true;
}

bool ToolEditorController::apply()
{
    if (!draft_ || !store_)
    {
        setStatus(QStringLiteral("没有可保存的草稿"), true);
        return false;
    }
    if (!isEditable())
    {
        setStatus(QStringLiteral("内置工具的定义不可覆盖，请复制为自有工具后再修改"), true);
        return false;
    }
    // Reloading a catalog replaces the factories a running pipeline holds; the management chain
    // only accepts that while the runtime is idle.
    if (app_ && app_->busy())
    {
        setStatus(QStringLiteral("运行时正在工作，停止后才能应用工具"), true);
        return false;
    }

    try
    {
        // The stored copy carries the path and roots the declaration was published under; the draft
        // keeps its declaration shape but must learn where it now lives, or it would look like an
        // unowned tool right after its first commit.
        const ToolDefinition stored = store_->saveCustomTool(*draft_);
        draft_->definitionPath      = stored.definitionPath;
        draft_->source              = stored.source;
        saved_                      = *draft_;
        updateDirty();
        emit toolsChanged();

        if (app_)
            app_->rescanTools();

        setStatus(QStringLiteral("工具已成功保存并应用：%1").arg(QString::fromStdString(draft_->nameZh)));
        return true;
    }
    catch (const std::exception &error)
    {
        setStatus(QStringLiteral("保存失败：%1").arg(QString::fromUtf8(error.what())), true);
        return false;
    }
}

void ToolEditorController::revert()
{
    if (saved_)
    {
        draft_ = saved_;
        bindDraftToPortModel();
        updateDirty();
        emit currentToolChanged();
        setStatus(QStringLiteral("已撤销修改"));
    }
    else if (draft_)
    {
        draft_.reset();
        bindDraftToPortModel();
        updateDirty();
        emit currentToolChanged();
        emit toolsChanged();
        setStatus(QStringLiteral("已丢弃新建草稿"));
    }
}

QString ToolEditorController::previewJson() const
{
    if (!draft_)
        return QString();
    try
    {
        ToolDefinition copy = *draft_;
        normalizeToolDefinition(copy);
        validateToolDefinition(copy);
        return QString::fromStdString(emitNormalizedToolJson(copy));
    }
    catch (const std::exception &error)
    {
        return QStringLiteral("/* 声明格式错误: %1 */").arg(error.what());
    }
}

void ToolEditorController::openCodeFile() const
{
    const QString path = currentToolScript();
    if (!path.isEmpty() && QFileInfo::exists(path))
        QDesktopServices::openUrl(QUrl::fromLocalFile(path));
}

void ToolEditorController::openCodeDirectory() const
{
    const QString path = currentToolScript();
    if (!path.isEmpty())
    {
        const QFileInfo info(path);
        if (info.dir().exists())
            QDesktopServices::openUrl(QUrl::fromLocalFile(info.dir().absolutePath()));
    }
}

} // namespace visionrt
