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
#include <cctype>
#include <functional>
#include <set>
#include <unordered_map>

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
           && left.colorOrder == right.colorOrder && left.imageVisible == right.imageVisible
           && left.decimals == right.decimals && equalValues(left.defaultValue, right.defaultValue)
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

bool sameTool(const ToolDefinition &a, const ToolDefinition &b)
{
    return a.nameEn == b.nameEn && a.nameZh == b.nameZh && a.version == b.version && a.kind == b.kind
           && a.nodeModel == b.nodeModel && a.categories == b.categories && a.entry.package == b.entry.package
           && a.entry.file == b.entry.file && a.entry.symbol == b.entry.symbol
           && a.hostBinding == b.hostBinding && samePorts(a.inputs, b.inputs)
           && samePorts(a.outputs, b.outputs) && samePorts(a.initParams, b.initParams);
}

QString portIdList(const std::set<int> &ids)
{
    QStringList parts;
    for (const int id : ids)
        parts.append(QString::number(id));
    return parts.join(QStringLiteral("/"));
}

QStringList splitCategory(const std::string &path)
{
    QStringList parts;
    for (const auto &part : QString::fromStdString(path).split(QLatin1Char('/')))
        if (!part.trimmed().isEmpty())
            parts.append(part.trimmed());
    return parts;
}

std::string joinCategory(const QStringList &parts)
{
    return parts.join(QLatin1Char('/')).toStdString();
}

// A folder name is one path segment: the tree joins segments with '/', so a name may not contain it.
QString normalizedFolderName(const QString &name)
{
    return name.trimmed();
}

bool validFolderName(const QString &name, QString *reason)
{
    if (name.isEmpty())
    {
        *reason = QStringLiteral("目录名称不能为空");
        return false;
    }
    if (name.contains(QLatin1Char('/')) || name.contains(QLatin1Char('\\')))
    {
        *reason = QStringLiteral("目录名称不能包含斜杠");
        return false;
    }
    if (name == QStringLiteral(".") || name == QStringLiteral(".."))
    {
        *reason = QStringLiteral("目录名称不合法");
        return false;
    }
    return true;
}

QString folderOf(const std::string &categoryPath)
{
    QStringList parts = splitCategory(categoryPath);
    if (parts.isEmpty())
        return QStringLiteral("未分类");
    return parts.mid(0, 2).join(QLatin1Char('/'));
}

} // namespace

ToolEditorController::ToolEditorController(QObject *parent)
    : QObject(parent)
    , ownedStore_(std::make_unique<CustomToolStore>())
    , store_(ownedStore_.get())
{
    for (ToolPortListModel *model : {&inputModel_, &outputModel_, &initModel_})
    {
        connect(model, &ToolPortListModel::draftModified, this, [this] {
            updateDirty();
            emit treeChanged();
            emit currentToolChanged();
        });
        connect(model, &ToolPortListModel::rejected, this, [this](const QString &message) {
            setStatus(message, true);
        });
        connect(model, &ToolPortListModel::cleared, this, [this](const QString &message) {
            setStatus(message);
        });
    }
}

ToolEditorController::~ToolEditorController() = default;

void ToolEditorController::setAppController(AppController *app)
{
    app_ = app;
    if (app_)
    {
        connect(app_, &AppController::catalogChanged, this, &ToolEditorController::treeChanged);
        // Opening or switching a project changes what the pending draft would break.
        connect(app_, &AppController::projectChanged, this, &ToolEditorController::updateImpactSummary);
    }
}

void ToolEditorController::setCustomStore(CustomToolStore *store)
{
    store_ = store ? store : ownedStore_.get();
    emit treeChanged();
}

ToolEditorController::Draft *ToolEditorController::current()
{
    const auto found = drafts_.find(current_);
    return found == drafts_.end() ? nullptr : &found->second;
}

const ToolEditorController::Draft *ToolEditorController::current() const
{
    const auto found = drafts_.find(current_);
    return found == drafts_.end() ? nullptr : &found->second;
}

ToolEditorController::Draft &ToolEditorController::ensureDraft(const std::string &toolId,
                                                               const ToolDefinition &definition)
{
    const auto found = drafts_.find(toolId);
    if (found != drafts_.end())
        return found->second;

    ToolDefinition declaration = definition;
    stripHostPorts(declaration);
    Draft draft;
    draft.key   = toolId;
    draft.def   = declaration;
    draft.saved = declaration;
    return drafts_.emplace(toolId, std::move(draft)).first->second;
}

std::vector<std::string> ToolEditorController::effectiveCategories(const std::string &toolId,
                                                                   const ToolDefinition &definition) const
{
    const auto found = drafts_.find(toolId);
    if (found != drafts_.end())
        return found->second.def.categories;
    return definition.categories;
}

QVariantList ToolEditorController::tree() const
{
    return buildTree();
}

QVariantList ToolEditorController::buildTree() const
{
    struct Entry
    {
        std::string id;
        QString     name;
        QString     folder;
        QString     key;
        bool        editable = false;
        bool        draft = false;
        std::string kind;
    };

    std::map<std::string, ToolDefinition> allDefs;
    if (app_)
        for (const auto &def : app_->toolCatalog())
            allDefs.emplace(def.nameEn, def);
    if (store_)
    {
        // The controller's catalog scan reports a bad declaration; the editor only lists what it can
        // read and does not repeat the same diagnostic on every tree refresh.
        std::vector<std::string> problems;
        for (const auto &tool : store_->discoverCustomTools(&problems))
            allDefs.insert_or_assign(tool.definition.nameEn, tool.definition);
    }

    // Drafts are what the tree shows: a new tool that has never been written, an edited declaration,
    // and the pending deletion that hides its tool until Save or Cancel decides.
    std::vector<Entry> tools;
    std::set<std::string> visibleDrafts;
    for (const auto &[key, draft] : drafts_)
    {
        if (deleted_.count(draft.def.nameEn))
            continue;
        visibleDrafts.insert(key);
        Entry entry;
        entry.id       = draft.def.nameEn;
        entry.name     = QString::fromStdString(draft.def.nameZh);
        entry.folder   = folderOf(joinCategory(splitCategory(categoryPath(draft.def))));
        entry.key      = QString::fromStdString(key);
        entry.editable = toolEditable(draft.def, draft.saved.has_value());
        entry.draft    = true;
        entry.kind     = toolKindString(draft.def.kind);
        tools.push_back(std::move(entry));
    }
    for (const auto &[id, def] : allDefs)
    {
        if (visibleDrafts.count(id) || deleted_.count(id))
            continue;
        // The editor authors the tools of this installation. A declaration that ships with it is only
        // listed when the authoring switch asks for it, so the catalogue is not offered as editable
        // by default.
        if (!toolEditable(def, true))
            continue;
        Entry entry;
        entry.id       = id;
        entry.name     = QString::fromStdString(def.nameZh);
        entry.folder   = folderOf(categoryPath(def));
        entry.key      = QString::fromStdString(id);
        entry.editable = toolEditable(def, true);
        entry.kind     = toolKindString(def.kind);
        tools.push_back(std::move(entry));
    }

    std::set<QString> folders;
    for (const auto &path : folders_)
    {
        QStringList parts = splitCategory(path);
        QString     prefix;
        for (const auto &part : parts)
        {
            prefix = prefix.isEmpty() ? part : prefix + QLatin1Char('/') + part;
            folders.insert(prefix);
        }
    }
    for (const auto &tool : tools)
    {
        QStringList parts = tool.folder.split(QLatin1Char('/'));
        QString     prefix;
        for (const auto &part : parts)
        {
            prefix = prefix.isEmpty() ? part : prefix + QLatin1Char('/') + part;
            folders.insert(prefix);
        }
    }

    // Preserve the exact canonical ordering of categories and tools from the main window catalog.
    std::unordered_map<std::string, int> toolOrder;
    std::unordered_map<QString, int>     folderOrder;
    int nextBuiltinFolderOrder = 0;
    int nextBuiltinToolOrder = 0;
    if (app_)
    {
        for (const auto &def : app_->toolCatalog())
        {
            if (toolOrder.find(def.nameEn) == toolOrder.end())
                toolOrder[def.nameEn] = nextBuiltinToolOrder++;

            QString prefix;
            for (const auto &cat : def.categories)
            {
                const QString qcat = QString::fromStdString(cat);
                prefix = prefix.isEmpty() ? qcat : prefix + QLatin1Char('/') + qcat;
                if (folderOrder.find(prefix) == folderOrder.end())
                    folderOrder[prefix] = nextBuiltinFolderOrder++;
            }
        }
    }

    // Custom tools and authored folders get top priority (negative order).
    int nextCustomFolderOrder = -1000;
    int nextCustomToolOrder = -1000;
    if (store_)
    {
        std::vector<std::string> problems;
        for (const auto &tool : store_->discoverCustomTools(&problems))
        {
            if (toolOrder.find(tool.definition.nameEn) == toolOrder.end())
                toolOrder[tool.definition.nameEn] = nextCustomToolOrder++;

            QString prefix;
            for (const auto &cat : tool.definition.categories)
            {
                const QString qcat = QString::fromStdString(cat);
                prefix = prefix.isEmpty() ? qcat : prefix + QLatin1Char('/') + qcat;
                if (folderOrder.find(prefix) == folderOrder.end())
                    folderOrder[prefix] = nextCustomFolderOrder++;
            }
        }
    }
    for (const auto &path : folders_)
    {
        QStringList parts = splitCategory(path);
        QString prefix;
        for (const auto &part : parts)
        {
            prefix = prefix.isEmpty() ? part : prefix + QLatin1Char('/') + part;
            if (folderOrder.find(prefix) == folderOrder.end())
                folderOrder[prefix] = nextCustomFolderOrder++;
        }
    }

    const auto toolNodes = [&tools, &toolOrder](const QString &folder) {
        std::vector<const Entry *> matching;
        for (const auto &tool : tools)
            if (tool.folder == folder)
                matching.push_back(&tool);
        std::sort(matching.begin(), matching.end(), [&toolOrder](const Entry *left, const Entry *right) {
            const auto itLeft = toolOrder.find(left->id);
            const auto itRight = toolOrder.find(right->id);
            const int orderLeft = (itLeft != toolOrder.end()) ? itLeft->second : 999999;
            const int orderRight = (itRight != toolOrder.end()) ? itRight->second : 999999;
            if (orderLeft != orderRight)
                return orderLeft < orderRight;
            const int byName = QString::localeAwareCompare(left->name, right->name);
            return byName != 0 ? byName < 0 : left->id < right->id;
        });
        QVariantList nodes;
        for (const auto *tool : matching)
        {
            QVariantMap node;
            node.insert("kind", QStringLiteral("tool"));
            node.insert("name", tool->name);
            node.insert("toolId", QString::fromStdString(tool->id));
            node.insert("key", tool->key);
            node.insert("folder", tool->folder);
            node.insert("toolKind", QString::fromStdString(tool->kind));
            node.insert("editable", tool->editable);
            node.insert("draft", tool->draft);
            node.insert("children", QVariantList{});
            nodes.append(node);
        }
        return nodes;
    };

    std::function<QVariantList(const QString &)> buildLevel = [&](const QString &parent) {
        QStringList children;
        for (const auto &path : folders)
        {
            const QString parentPath = path.contains(QLatin1Char('/'))
                                           ? path.left(path.lastIndexOf(QLatin1Char('/')))
                                           : QString();
            if (parentPath == parent)
                children.append(path);
        }
        std::sort(children.begin(), children.end(), [&folderOrder](const QString &left, const QString &right) {
            const auto itLeft = folderOrder.find(left);
            const auto itRight = folderOrder.find(right);
            const int orderLeft = (itLeft != folderOrder.end()) ? itLeft->second : 999999;
            const int orderRight = (itRight != folderOrder.end()) ? itRight->second : 999999;
            if (orderLeft != orderRight)
                return orderLeft < orderRight;
            return QString::localeAwareCompare(left, right) < 0;
        });

        QVariantList nodes;
        for (const auto &path : children)
        {
            QVariantMap node;
            node.insert("kind", QStringLiteral("folder"));
            node.insert("name", path.contains(QLatin1Char('/')) ? path.mid(path.lastIndexOf(QLatin1Char('/')) + 1)
                                                                : path);
            node.insert("path", path);
            node.insert("children", buildLevel(path));
            nodes.append(node);
        }
        for (const auto &tool : toolNodes(parent))
            nodes.append(tool);
        return nodes;
    };

    return buildLevel(QString());
}

QString ToolEditorController::currentKey() const
{
    return QString::fromStdString(current_);
}

bool ToolEditorController::hasTool() const noexcept
{
    return current() != nullptr;
}

void ToolEditorController::selectKey(const QString &key)
{
    const std::string id = key.toStdString();
    if (id.empty())
    {
        current_.clear();
        bindModels();
        emit currentToolChanged();
        updateImpactSummary();
        return;
    }

    // A draft is addressed by its stable key, which stops tracking the id when a new tool is
    // renamed. A caller that only knows the current id still finds it here.
    std::string target = id;
    if (!drafts_.count(target))
        for (const auto &[draftKey, draft] : drafts_)
            if (draft.def.nameEn == id)
            {
                target = draftKey;
                break;
            }

    if (!drafts_.count(target))
    {
        std::optional<ToolDefinition> found;
        if (store_)
            if (const auto tool = store_->findCustomTool(id))
                found = tool->definition;
        if (!found && app_)
        {
            const auto &definitions = app_->toolCatalog();
            const auto  match = std::find_if(definitions.begin(), definitions.end(),
                                            [&id](const ToolDefinition &def) { return def.nameEn == id; });
            if (match != definitions.end())
                found = *match;
        }
        if (!found)
        {
            setStatus(QStringLiteral("未找到工具：%1").arg(key), true);
            return;
        }
        ensureDraft(id, *found);
        target = id;
    }

    if (target == current_)
        return;
    current_ = target;
    bindModels();
    updateDirty();
    emit currentToolChanged();
    setStatus(isEditable() ? QStringLiteral("正在编辑：%1").arg(currentToolName())
                           : QStringLiteral("内置工具只读：%1").arg(currentToolName()));
}

void ToolEditorController::bindModels()
{
    Draft *draft = current();
    ToolDefinition *definition = draft ? &draft->def : nullptr;
    const bool editable = isEditable();
    inputModel_.setDraft(definition, PortStage::Inputs, editable);
    outputModel_.setDraft(definition, PortStage::Outputs, editable);
    initModel_.setDraft(definition, PortStage::Init, editable);
}

QString ToolEditorController::currentToolId() const
{
    const Draft *draft = current();
    return draft ? QString::fromStdString(draft->def.nameEn) : QString();
}

bool ToolEditorController::isToolIdEditable() const noexcept
{
    const Draft *draft = current();
    return draft && !draft->saved;
}

bool ToolEditorController::toolIdTaken(const std::string &toolId) const
{
    for (const auto &[key, draft] : drafts_)
        if (draft.def.nameEn == toolId && key != current_)
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
    Draft *draft = current();
    if (!draft)
        return;
    const std::string id = value.trimmed().toStdString();
    if (!isToolIdEditable())
    {
        setStatus(QStringLiteral("工具标识在首次保存后不能修改"), true);
        emit currentToolChanged();
        return;
    }
    if (id == draft->def.nameEn)
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
    draft->def.nameEn = id;
    std::string package = id;
    std::replace(package.begin(), package.end(), '.', '/');
    draft->def.entry.package = package;

    updateDirty();
    emit currentToolChanged();
    emit treeChanged();
}

QString ToolEditorController::currentToolName() const
{
    const Draft *draft = current();
    return draft ? QString::fromStdString(draft->def.nameZh) : QString();
}

void ToolEditorController::setCurrentToolName(const QString &value)
{
    Draft *draft = current();
    if (draft && draft->def.nameZh != value.trimmed().toStdString())
    {
        draft->def.nameZh = value.trimmed().toStdString();
        updateDirty();
        emit currentToolChanged();
        emit treeChanged();
    }
}

QString ToolEditorController::currentToolKind() const
{
    const Draft *draft = current();
    return draft ? QString::fromLatin1(toolKindString(draft->def.kind)) : QString();
}

bool ToolEditorController::isPython() const noexcept
{
    const Draft *draft = current();
    return draft && (draft->def.kind == ToolKind::PythonFunction || draft->def.kind == ToolKind::PythonClass);
}

bool ToolEditorController::showsInit() const noexcept
{
    const Draft *draft = current();
    return draft && draft->def.kind == ToolKind::PythonClass;
}

QString ToolEditorController::currentToolCategory() const
{
    const Draft *draft = current();
    return draft ? QString::fromStdString(categoryPath(draft->def)) : QString();
}

void ToolEditorController::setCurrentToolCategory(const QString &value)
{
    Draft *draft = current();
    if (!draft)
        return;
    QStringList parts;
    for (const auto &part : value.split(QLatin1Char('/')))
    {
        const QString name = part.trimmed();
        if (name.isEmpty())
            continue;
        QString reason;
        if (!validFolderName(name, &reason))
        {
            setStatus(reason, true);
            return;
        }
        parts.append(name);
    }
    const std::string path = joinCategory(parts);
    if (categoryPath(draft->def) == path)
        return;
    draft->def.categories.clear();
    for (const auto &part : parts)
        draft->def.categories.push_back(part.toStdString());
    if (!parts.isEmpty())
        folders_.push_back(path);
    updateDirty();
    emit currentToolChanged();
    emit treeChanged();
}

QString ToolEditorController::currentToolVersion() const
{
    const Draft *draft = current();
    return draft ? QString::fromStdString(draft->def.version) : QString();
}

void ToolEditorController::setCurrentToolVersion(const QString &value)
{
    Draft *draft = current();
    if (draft && draft->def.version != value.trimmed().toStdString())
    {
        draft->def.version = value.trimmed().toStdString();
        updateDirty();
        emit currentToolChanged();
    }
}

QString ToolEditorController::currentToolFile() const
{
    const Draft *draft = current();
    return draft ? QString::fromStdString(draft->def.entry.file.generic_string()) : QString();
}

void ToolEditorController::setCurrentToolFile(const QString &value)
{
    Draft *draft = current();
    if (!draft)
        return;
    const QString text = value.trimmed();
    if (text.isEmpty())
    {
        setStatus(QStringLiteral("脚本路径不能为空"), true);
        emit currentToolChanged();
        return;
    }
    const std::filesystem::path relative = std::filesystem::u8path(text.toStdString());
    if (relative.is_absolute() || text.contains(QStringLiteral("..")))
    {
        setStatus(QStringLiteral("脚本路径必须是工具包内的相对路径"), true);
        emit currentToolChanged();
        return;
    }
    const std::string file = relative.generic_string();
    if (draft->def.entry.file.generic_string() != file)
    {
        draft->def.entry.file = file;
        updateDirty();
        emit currentToolChanged();
    }
}

QString ToolEditorController::currentToolSymbol() const
{
    const Draft *draft = current();
    return draft ? QString::fromStdString(draft->def.entry.symbol) : QString();
}

void ToolEditorController::setCurrentToolSymbol(const QString &value)
{
    Draft *draft = current();
    if (!draft)
        return;
    const std::string symbol = value.trimmed().toStdString();
    if (symbol.empty())
    {
        setStatus(QStringLiteral("入口名称不能为空"), true);
        emit currentToolChanged();
        return;
    }
    try
    {
        validateIdentifier(symbol, false);
    }
    catch (const std::exception &error)
    {
        setStatus(QString::fromUtf8(error.what()), true);
        emit currentToolChanged();
        return;
    }
    if (draft->def.entry.symbol != symbol)
    {
        draft->def.entry.symbol = symbol;
        updateDirty();
        emit currentToolChanged();
    }
}

QString ToolEditorController::currentToolScript() const
{
    const Draft *draft = current();
    if (!draft || !store_ || !isPython())
        return QString();
    if (!draft->def.definitionPath.empty() && !draft->def.source.roots.python.empty())
        return QString::fromStdWString(draft->def.pythonScript().wstring());
    const auto file = draft->def.entry.file.empty() ? std::filesystem::path{"tool.py"} : draft->def.entry.file;
    return QString::fromStdWString((store_->pythonDir() / draft->def.entry.package / file).wstring());
}

QString ToolEditorController::currentToolBinding() const
{
    const Draft *draft = current();
    return draft ? QString::fromStdString(draft->def.hostBinding) : QString();
}

bool ToolEditorController::toolEditable(const ToolDefinition &definition, bool hasSaved) const
{
    if (!hasSaved)
        return true;
    if (store_ && store_->isUserTool(definition))
        return true;
    // An installed declaration describes a compiled implementation. Authoring it is an explicit
    // choice, and even then it needs a file of its own to be written back to.
    return editBuiltins_ && !definition.definitionPath.empty();
}

bool ToolEditorController::isEditable() const noexcept
{
    const Draft *draft = current();
    if (!draft)
        return false;
    return toolEditable(draft->def, draft->saved.has_value());
}

bool ToolEditorController::editBuiltins() const noexcept
{
    return editBuiltins_;
}

void ToolEditorController::setEditBuiltins(bool enabled)
{
    if (editBuiltins_ == enabled)
        return;
    if (!enabled)
        for (const auto &[key, draft] : drafts_)
            // Read the ownership, not toolEditable: while the switch is still on it would answer for
            // the state being left, not the one being entered.
            if (draft.saved && !(store_ && store_->isUserTool(draft.def))
                && !sameTool(draft.def, *draft.saved))
            {
                // Turning the switch off must not discard an edit the user can no longer see.
                setStatus(QStringLiteral("内置工具还有未保存的修改，保存或取消后再关闭编辑"), true);
                return;
            }
    editBuiltins_ = enabled;
    emit editBuiltinsChanged();
    emit treeChanged();
}

bool ToolEditorController::canClone() const noexcept
{
    const Draft *draft = current();
    // A copy reads the script of a tool that already exists; a brand-new draft has none yet.
    return isPython() && draft && draft->saved;
}

bool ToolEditorController::isDirty() const noexcept
{
    return dirty_;
}

bool ToolEditorController::canSave() const noexcept
{
    return dirty_;
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
    bool dirty = !deleted_.empty();
    if (!dirty)
        for (const auto &[key, draft] : drafts_)
        {
            if (!draft.saved || !sameTool(draft.def, *draft.saved))
            {
                dirty = true;
                break;
            }
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
    Draft *draft = current();
    QString summary = QStringLiteral("当前工程无引用该工具的节点");
    if (app_ && draft && dirty_)
    {
        const auto &project = app_->project();
        const auto declared = [draft](PortStage stage, PortId id) {
            const auto &ports = stage == PortStage::Outputs   ? draft->def.outputs
                                : stage == PortStage::Inputs  ? draft->def.inputs
                                                              : draft->def.initParams;
            return std::any_of(ports.begin(), ports.end(), [id](const PortDefinition &port) { return port.id == id; });
        };

        std::set<int> unboundInputs;
        std::set<int> retypedInputs;
        std::set<int> droppedOutputs;
        std::set<NodeId> ownNodes;
        const int     nodes = referencingNodes(draft->def.nameEn);
        for (const auto &flow : project.flows)
        {
            for (const auto &node : flow.nodes)
            {
                if (node.toolId != draft->def.nameEn)
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
                    const auto port = std::find_if(draft->def.inputs.begin(), draft->def.inputs.end(),
                                                   [id](const PortDefinition &item) { return item.id == id; });
                    if (port != draft->def.inputs.end() && !(port->type == type))
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

bool ToolEditorController::addTool(const QString &folderPath)
{
    if (app_ && app_->busy())
    {
        setStatus(QStringLiteral("运行时正在工作，停止后才能新增工具"), true);
        return false;
    }

    const int seq = ++newToolSeq_;
    std::string id;
    for (int candidate = seq;; ++candidate)
    {
        id = "custom.tool." + std::to_string(candidate);
        if (!toolIdTaken(id))
            break;
    }

    ToolDefinition def;
    def.schemaVersion    = 6;
    def.nameZh           = "新工具 " + std::to_string(seq);
    def.nameEn           = id;
    def.version          = "1.0.0";
    def.kind             = ToolKind::PythonFunction;
    def.nextInitPortId   = 0;
    def.nextInputPortId  = 1;
    def.nextOutputPortId = 2;

    const QString folder = folderPath.isEmpty() ? QStringLiteral("未分类") : folderPath;
    for (const auto &part : folder.split(QLatin1Char('/')))
        if (!part.trimmed().isEmpty())
            def.categories.push_back(part.trimmed().toStdString());

    std::string package = id;
    std::replace(package.begin(), package.end(), '.', '/');
    def.entry.package = package;
    def.entry.file    = "tool.py";
    def.entry.symbol  = "execute";

    const std::string key = "__new__" + std::to_string(seq);
    Draft draft;
    draft.key = key;
    draft.def = def;
    drafts_.emplace(key, std::move(draft));
    current_ = key;
    if (!folder.isEmpty())
        folders_.push_back(folder.toStdString());

    bindModels();
    updateDirty();
    emit currentToolChanged();
    emit treeChanged();
    setStatus(QStringLiteral("已添加工具草稿，保存后写入工具目录"));
    return true;
}

bool ToolEditorController::addFolder(const QString &parentPath, const QString &name)
{
    QString reason;
    const QString folder = normalizedFolderName(name);
    if (!validFolderName(folder, &reason))
    {
        setStatus(reason, true);
        return false;
    }
    const QString path = parentPath.isEmpty() ? folder : parentPath + QLatin1Char('/') + folder;

    // A folder exists when a declaration already lives under it or a draft folder was added; the
    // second case is what lets an empty folder exist before any tool does.
    const auto treeNodes = buildTree();
    std::function<bool(const QVariantList &, const QString &)> exists = [&](const QVariantList &nodes,
                                                                           const QString &wanted) {
        for (const auto &node : nodes)
        {
            const auto map = node.toMap();
            if (map.value("kind").toString() != QStringLiteral("folder"))
                continue;
            if (map.value("path").toString() == wanted)
                return true;
            if (exists(map.value("children").toList(), wanted))
                return true;
        }
        return false;
    };
    if (exists(treeNodes, path))
    {
        setStatus(QStringLiteral("目录已存在：%1").arg(path), true);
        return false;
    }

    folders_.push_back(path.toStdString());
    emit treeChanged();
    setStatus(QStringLiteral("已添加目录：%1").arg(path));
    return true;
}

bool ToolEditorController::renameFolder(const QString &path, const QString &name)
{
    const QString folder = normalizedFolderName(name);
    QString       reason;
    if (!validFolderName(folder, &reason))
    {
        setStatus(reason, true);
        return false;
    }
    const QString parent   = parentFolder(path);
    const QString newPath  = parent.isEmpty() ? folder : parent + QLatin1Char('/') + folder;
    if (newPath == path)
        return true;
    if (!folderHasTools(path) && std::find(folders_.begin(), folders_.end(), path.toStdString()) == folders_.end())
    {
        setStatus(QStringLiteral("目录不存在：%1").arg(path), true);
        return false;
    }

    // Renaming a folder is a declaration change for every tool under it: each one is moved into a
    // draft, and Save writes them together. Nothing is renamed on disk here.
    std::map<std::string, ToolDefinition> allDefs;
    if (app_)
        for (const auto &def : app_->toolCatalog())
            allDefs.emplace(def.nameEn, def);
    if (store_)
    {
        // The controller's catalog scan reports a bad declaration; the editor only lists what it can
        // read and does not repeat the same diagnostic on every tree refresh.
        std::vector<std::string> problems;
        for (const auto &tool : store_->discoverCustomTools(&problems))
            allDefs.insert_or_assign(tool.definition.nameEn, tool.definition);
    }

    const QStringList fromParts = splitCategory(path.toStdString());
    const QStringList toParts   = splitCategory(newPath.toStdString());
    int               renamed   = 0;

    const auto renameTool = [&](const std::string &key, const ToolDefinition &definition) {
        if (deleted_.count(definition.nameEn))
            return;
        QStringList parts = splitCategory(categoryPath(definition));
        if (parts.size() < fromParts.size())
            return;
        for (int i = 0; i < fromParts.size(); ++i)
            if (parts[i] != fromParts[i])
                return;
        QStringList renamedParts = toParts;
        for (int i = fromParts.size(); i < parts.size(); ++i)
            renamedParts.append(parts[i]);

        std::vector<std::string> categories;
        for (const auto &part : renamedParts)
            categories.push_back(part.toStdString());

        Draft &draft = ensureDraft(key, definition);
        if (draft.def.categories == categories)
            return;
        draft.def.categories = std::move(categories);
        ++renamed;
    };

    // Collect first: moving a tool into a draft inserts into the map the loop would walk.
    std::vector<std::pair<std::string, ToolDefinition>> renamedTools;
    for (const auto &[key, draft] : drafts_)
        renamedTools.emplace_back(key, draft.def);
    for (const auto &[id, def] : allDefs)
        if (!drafts_.count(id) && toolEditable(def, true))
            renamedTools.emplace_back(id, def);
    for (const auto &[key, definition] : renamedTools)
        renameTool(key, definition);

    for (auto &entry : folders_)
    {
        const QString value = QString::fromStdString(entry);
        if (value == path)
            entry = newPath.toStdString();
        else if (value.startsWith(path + QLatin1Char('/')))
            entry = (newPath + value.mid(path.size())).toStdString();
    }
    folders_.erase(std::remove_if(folders_.begin(), folders_.end(),
                                  [](const std::string &entry) { return entry.empty(); }),
                   folders_.end());

    updateDirty();
    emit treeChanged();
    emit currentToolChanged();
    setStatus(QStringLiteral("已重命名目录：%1，%2 个工具随之移动，保存后生效").arg(newPath).arg(renamed));
    return true;
}

bool ToolEditorController::removeFolder(const QString &path)
{
    if (folderHasTools(path))
    {
        setStatus(QStringLiteral("目录内还有工具，不能删除：%1").arg(path), true);
        return false;
    }
    const QString prefix = path + QLatin1Char('/');
    const bool    hasChild = std::any_of(folders_.begin(), folders_.end(), [&prefix](const std::string &entry) {
        return QString::fromStdString(entry).startsWith(prefix);
    });
    if (hasChild)
    {
        setStatus(QStringLiteral("目录内还有子目录，不能删除：%1").arg(path), true);
        return false;
    }
    const auto before = folders_.size();
    folders_.erase(std::remove(folders_.begin(), folders_.end(), path.toStdString()), folders_.end());
    if (folders_.size() == before)
    {
        setStatus(QStringLiteral("目录不存在：%1").arg(path), true);
        return false;
    }
    emit treeChanged();
    setStatus(QStringLiteral("已删除空目录：%1").arg(path));
    return true;
}

QString ToolEditorController::folderName(const QString &path) const
{
    const int index = path.lastIndexOf(QLatin1Char('/'));
    return index < 0 ? path : path.mid(index + 1);
}

QString ToolEditorController::parentFolder(const QString &path) const
{
    const int index = path.lastIndexOf(QLatin1Char('/'));
    return index < 0 ? QString() : path.left(index);
}

bool ToolEditorController::folderHasTools(const QString &path) const
{
    const QString prefix = path + QLatin1Char('/');
    std::map<std::string, ToolDefinition> allDefs;
    if (app_)
        for (const auto &def : app_->toolCatalog())
            allDefs.emplace(def.nameEn, def);
    if (store_)
    {
        // The controller's catalog scan reports a bad declaration; the editor only lists what it can
        // read and does not repeat the same diagnostic on every tree refresh.
        std::vector<std::string> problems;
        for (const auto &tool : store_->discoverCustomTools(&problems))
            allDefs.insert_or_assign(tool.definition.nameEn, tool.definition);
    }

    for (const auto &[key, draft] : drafts_)
    {
        if (deleted_.count(draft.def.nameEn))
            continue;
        const QString folder = folderOf(categoryPath(draft.def));
        if (folder == path || folder.startsWith(prefix))
            return true;
    }
    for (const auto &[id, def] : allDefs)
    {
        if (drafts_.count(id) || deleted_.count(id))
            continue;
        const QString folder = folderOf(categoryPath(def));
        if (folder == path || folder.startsWith(prefix))
            return true;
    }
    return false;
}

bool ToolEditorController::cloneTool(const QString &key)
{
    if (!store_)
        return false;
    if (app_ && app_->busy())
    {
        setStatus(QStringLiteral("运行时正在工作，停止后才能复制工具"), true);
        return false;
    }

    const Draft *source = nullptr;
    const auto found = drafts_.find(key.toStdString());
    if (found != drafts_.end())
        source = &found->second;
    if (!source)
    {
        setStatus(QStringLiteral("请先选择要复制的工具"), true);
        return false;
    }
    if (!source->saved)
    {
        setStatus(QStringLiteral("请先保存该工具，再复制"), true);
        return false;
    }
    if (source->def.kind != ToolKind::PythonFunction && source->def.kind != ToolKind::PythonClass)
    {
        setStatus(QStringLiteral("只有 Python 工具可以复制"), true);
        return false;
    }
    if (store_ && !store_->isUserTool(source->def))
    {
        setStatus(QStringLiteral("内置工具不能复制到自定义目录"), true);
        return false;
    }

    int         suffix = 1;
    std::string newId;
    while (toolIdTaken(source->def.nameEn + "_copy" + std::to_string(suffix)))
        ++suffix;
    newId = source->def.nameEn + "_copy" + std::to_string(suffix);

    ToolDefinition cloned  = source->def;
    cloned.nameEn          = newId;
    cloned.nameZh          = source->def.nameZh + " 副本" + (suffix == 1 ? "" : " " + std::to_string(suffix));
    cloned.definitionPath.clear();
    cloned.source = ToolSource{};
    std::string package = newId;
    std::replace(package.begin(), package.end(), '.', '/');
    cloned.entry.package = package;

    const std::string copyKey = "clone." + std::to_string(++newToolSeq_);
    Draft copy;
    copy.key         = copyKey;
    copy.def         = cloned;
    copy.cloneSource = source->def;
    drafts_.emplace(copyKey, std::move(copy));
    current_ = copyKey;

    bindModels();
    updateDirty();
    emit currentToolChanged();
    emit treeChanged();
    setStatus(QStringLiteral("已创建复制草稿：%1，保存后写入工具目录").arg(QString::fromStdString(cloned.nameZh)));
    return true;
}

bool ToolEditorController::deleteTool(const QString &key)
{
    if (!store_)
        return false;
    if (app_ && app_->busy())
    {
        setStatus(QStringLiteral("运行时正在工作，停止后才能删除工具"), true);
        return false;
    }

    const auto found = drafts_.find(key.toStdString());
    if (found != drafts_.end())
    {
        const Draft &draft = found->second;
        if (!draft.saved)
        {
            // A tool that was never written has nothing to delete; dropping the draft is the whole
            // operation, and Cancel would not resurrect it either.
            drafts_.erase(found);
            if (current_ == key.toStdString())
            {
                current_.clear();
                bindModels();
            }
            updateDirty();
            emit treeChanged();
            emit currentToolChanged();
            setStatus(QStringLiteral("已移除未保存的工具草稿"));
            return true;
        }
        if (!store_->isUserTool(draft.def))
        {
            setStatus(QStringLiteral("内置工具不能删除：%1").arg(QString::fromStdString(draft.def.nameZh)), true);
            return false;
        }
        deleted_.insert(draft.def.nameEn);
        drafts_.erase(found);
        if (current_ == key.toStdString())
        {
            current_.clear();
            bindModels();
        }
        updateDirty();
        emit treeChanged();
        emit currentToolChanged();
        setStatus(QStringLiteral("已标记删除，保存后从工具目录移除"));
        return true;
    }

    const std::string id = key.toStdString();
    if (!store_->findCustomTool(id))
    {
        setStatus(QStringLiteral("只有自有工具可以删除：%1").arg(key), true);
        return false;
    }
    deleted_.insert(id);
    updateDirty();
    emit treeChanged();
    setStatus(QStringLiteral("已标记删除，保存后从工具目录移除"));
    return true;
}

bool ToolEditorController::saveAll()
{
    if (!dirty_)
        return true;
    if (app_ && app_->busy())
    {
        setStatus(QStringLiteral("运行时正在工作，停止后才能保存工具"), true);
        return false;
    }
    if (!store_)
    {
        setStatus(QStringLiteral("工具存储不可用，无法保存"), true);
        return false;
    }

    // Every pending declaration is checked before anything is written: a rejected tool must not
    // leave the earlier tools of the same Save half committed.
    for (const auto &[key, draft] : drafts_)
    {
        if (draft.saved && sameTool(draft.def, *draft.saved))
            continue;
        try
        {
            ToolDefinition check = draft.def;
            normalizeToolDefinition(check);
            validateToolDefinition(check);
        }
        catch (const std::exception &error)
        {
            setStatus(QStringLiteral("%1 无法保存：%2")
                          .arg(QString::fromStdString(draft.def.nameZh), QString::fromUtf8(error.what())),
                      true);
            return false;
        }
    }

    // Deletions run first: if one cannot be done, no draft has been written yet and the window still
    // holds every edit.
    for (const auto &id : deleted_)
    {
        try
        {
            if (!store_->deleteCustomTool(id))
            {
                setStatus(QStringLiteral("删除失败：%1").arg(QString::fromStdString(id)), true);
                return false;
            }
        }
        catch (const std::exception &error)
        {
            setStatus(QStringLiteral("删除 %1 失败：%2")
                          .arg(QString::fromStdString(id), QString::fromUtf8(error.what())),
                      true);
            return false;
        }
    }
    deleted_.clear();

    int saved = 0;
    for (auto &[key, draft] : drafts_)
    {
        if (draft.saved && sameTool(draft.def, *draft.saved))
            continue;
        if (draft.def.nameEn.empty() || draft.def.nameZh.empty())
        {
            setStatus(QStringLiteral("工具名称与标识不能为空"), true);
            return false;
        }
        try
        {
            // A copied tool brings its script and resources; the copy is made here so Cancel can
            // still leave the tool directory untouched.
            if (draft.cloneSource)
            {
                (void)store_->cloneCustomTool(*draft.cloneSource, draft.def.nameEn, draft.def.nameZh);
                draft.cloneSource.reset();
            }
            if (draft.saved && !store_->isUserTool(draft.def))
            {
                // An installed declaration is written back to the file it was loaded from. The
                // application refuses the write when the ports no longer match the compiled
                // implementation, and the draft stays in the window for the user to correct.
                QString error;
                if (!app_ || !app_->saveBuiltinTool(draft.def, &error))
                    throw ContractError(error.isEmpty() ? std::string("内置工具声明无法写入") : error.toStdString());
                draft.saved = draft.def;
                ++saved;
                continue;
            }
            // The stored copy carries the path and roots the declaration was published under; the
            // draft keeps its declaration shape but must learn where it now lives.
            const ToolDefinition stored = store_->saveCustomTool(draft.def);
            draft.def.definitionPath    = stored.definitionPath;
            draft.def.source            = stored.source;
            // The draft is a declaration, not the normalised contract the store writes: the type's
            // materialised empty defaults must not make a just-saved tool look dirty again.
            draft.saved = draft.def;
            ++saved;
        }
        catch (const std::exception &error)
        {
            setStatus(QStringLiteral("保存 %1 失败：%2")
                          .arg(QString::fromStdString(draft.def.nameZh), QString::fromUtf8(error.what())),
                      true);
            return false;
        }
    }

    updateDirty();
    emit treeChanged();
    emit currentToolChanged();
    if (app_)
        app_->rescanTools();
    setStatus(saved > 0 ? QStringLiteral("已保存 %1 个工具的修改").arg(saved)
                        : QStringLiteral("已保存目录与删除修改"));
    return true;
}

void ToolEditorController::cancelAll()
{
    drafts_.clear();
    deleted_.clear();
    folders_.clear();
    current_.clear();
    bindModels();
    updateDirty();
    emit treeChanged();
    emit currentToolChanged();
    setStatus(QStringLiteral("已取消本次全部修改"));
}

QString ToolEditorController::previewJson() const
{
    const Draft *draft = current();
    if (!draft)
        return QString();
    try
    {
        ToolDefinition copy = draft->def;
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
