#pragma once

#include "core/CustomToolStore.h"
#include "core/ToolDefinition.h"
#include "ui/ToolPortListModel.h"

#include <QObject>
#include <QString>
#include <QVariantList>
#include <QVariantMap>
#include <map>
#include <memory>
#include <optional>
#include <set>
#include <string>
#include <vector>

namespace visionrt {

class AppController;

// The custom tool editor works on a window-scoped set of drafts: several tools and their folders can
// be edited before anything is written, and one Cancel discards them all. A draft is a declaration
// without host ports; the port models project the fixed prefix, so the declaration never holds a
// second copy of it.
class ToolEditorController : public QObject
{
    Q_OBJECT

    Q_PROPERTY(QVariantList tree READ tree NOTIFY treeChanged)
    Q_PROPERTY(QString currentKey READ currentKey WRITE selectKey NOTIFY currentToolChanged)
    Q_PROPERTY(bool hasTool READ hasTool NOTIFY currentToolChanged)
    Q_PROPERTY(QString currentToolId READ currentToolId WRITE setCurrentToolId NOTIFY currentToolChanged)
    Q_PROPERTY(bool isToolIdEditable READ isToolIdEditable NOTIFY currentToolChanged)
    Q_PROPERTY(QString currentToolName READ currentToolName WRITE setCurrentToolName NOTIFY currentToolChanged)
    Q_PROPERTY(QString currentToolKind READ currentToolKind NOTIFY currentToolChanged)
    Q_PROPERTY(QString currentToolCategory READ currentToolCategory WRITE setCurrentToolCategory NOTIFY currentToolChanged)
    Q_PROPERTY(QString currentToolVersion READ currentToolVersion WRITE setCurrentToolVersion NOTIFY currentToolChanged)
    Q_PROPERTY(QString currentToolFile READ currentToolFile WRITE setCurrentToolFile NOTIFY currentToolChanged)
    Q_PROPERTY(QString currentToolSymbol READ currentToolSymbol WRITE setCurrentToolSymbol NOTIFY currentToolChanged)
    // Absolute path of the draft's script, for the open actions only. The card edits the relative one.
    Q_PROPERTY(QString currentToolScript READ currentToolScript NOTIFY currentToolChanged)
    Q_PROPERTY(QString currentToolBinding READ currentToolBinding NOTIFY currentToolChanged)
    Q_PROPERTY(bool isPython READ isPython NOTIFY currentToolChanged)
    Q_PROPERTY(bool showsInit READ showsInit NOTIFY currentToolChanged)
    Q_PROPERTY(visionrt::ToolPortListModel *inputModel READ inputModel CONSTANT)
    Q_PROPERTY(visionrt::ToolPortListModel *outputModel READ outputModel CONSTANT)
    Q_PROPERTY(visionrt::ToolPortListModel *initModel READ initModel CONSTANT)
    Q_PROPERTY(bool isEditable READ isEditable NOTIFY currentToolChanged)
    // Off by default. When on, declarations shipped with the installation are listed and edited with
    // the same cards and the same Save; their ports still have to match the compiled implementation.
    Q_PROPERTY(bool editBuiltins READ editBuiltins WRITE setEditBuiltins NOTIFY editBuiltinsChanged)
    // Copying carries a script: a Python tool (own or installed) can be copied into the user root,
    // while a registered native factory has nothing to carry and stays read-only.
    Q_PROPERTY(bool canClone READ canClone NOTIFY currentToolChanged)
    Q_PROPERTY(bool isDirty READ isDirty NOTIFY dirtyChanged)
    Q_PROPERTY(bool canSave READ canSave NOTIFY dirtyChanged)
    Q_PROPERTY(QString statusMessage READ statusMessage NOTIFY statusChanged)
    Q_PROPERTY(bool isStatusError READ isStatusError NOTIFY statusChanged)
    Q_PROPERTY(QVariantList supportedTypes READ supportedTypes CONSTANT)
    Q_PROPERTY(QString impactSummary READ impactSummary NOTIFY impactSummaryChanged)

public:
    explicit ToolEditorController(QObject *parent = nullptr);
    ~ToolEditorController() override;

    void setAppController(AppController *app);
    void setCustomStore(CustomToolStore *store);

    QVariantList tree() const;
    QString      currentKey() const;
    QString      currentToolId() const;
    void         setCurrentToolId(const QString &value);
    bool         isToolIdEditable() const noexcept;
    QString      currentToolName() const;
    void         setCurrentToolName(const QString &value);
    QString      currentToolKind() const;
    QString      currentToolCategory() const;
    void         setCurrentToolCategory(const QString &value);
    QString      currentToolVersion() const;
    void         setCurrentToolVersion(const QString &value);
    QString      currentToolFile() const;
    void         setCurrentToolFile(const QString &value);
    QString      currentToolSymbol() const;
    void         setCurrentToolSymbol(const QString &value);
    QString      currentToolScript() const;
    QString      currentToolBinding() const;
    bool         hasTool() const noexcept;
    bool         isPython() const noexcept;
    bool         showsInit() const noexcept;

    ToolPortListModel *inputModel() noexcept
    {
        return &inputModel_;
    }
    ToolPortListModel *outputModel() noexcept
    {
        return &outputModel_;
    }
    ToolPortListModel *initModel() noexcept
    {
        return &initModel_;
    }

    bool isEditable() const noexcept;
    bool editBuiltins() const noexcept;
    void setEditBuiltins(bool enabled);
    bool canClone() const noexcept;
    bool isDirty() const noexcept;
    bool canSave() const noexcept;

    QString statusMessage() const;
    bool    isStatusError() const noexcept;

    QVariantList supportedTypes() const;
    QString      impactSummary() const;

    // Selecting a tool keeps every other draft in memory; nothing is written and no edit is lost.
    Q_INVOKABLE void selectKey(const QString &key);
    Q_INVOKABLE bool addTool(const QString &folderPath);
    Q_INVOKABLE bool addFolder(const QString &parentPath, const QString &name);
    Q_INVOKABLE bool renameFolder(const QString &path, const QString &name);
    Q_INVOKABLE bool removeFolder(const QString &path);
    Q_INVOKABLE bool cloneTool(const QString &key);
    Q_INVOKABLE bool deleteTool(const QString &key);
    // Commits every draft and every pending deletion, or reports the first declaration that cannot
    // be written and keeps the remaining drafts intact.
    Q_INVOKABLE bool saveAll();
    Q_INVOKABLE void cancelAll();
    Q_INVOKABLE QString previewJson() const;
    Q_INVOKABLE void openCodeFile() const;
    Q_INVOKABLE void openCodeDirectory() const;
    // How many nodes of the open project reference this tool. The delete prompt and the impact
    // preview read the same count, so "referenced" has one meaning in the editor.
    Q_INVOKABLE int toolUsage(const QString &toolId) const;
    Q_INVOKABLE QString folderName(const QString &path) const;
    Q_INVOKABLE QString parentFolder(const QString &path) const;

signals:
    void treeChanged();
    void currentToolChanged();
    void dirtyChanged();
    void statusChanged();
    void impactSummaryChanged();
    void editBuiltinsChanged();

private:
    struct Draft
    {
        // Stable within the editor window. A new tool's id may change before its first save, so the
        // key is never derived from the id after creation.
        std::string                    key;
        ToolDefinition                 def;
        std::optional<ToolDefinition>  saved;
        // A copy carries its source until Save: the script and resources are only written when the
        // whole window is committed, so Cancel leaves the tool directory untouched.
        std::optional<ToolDefinition>  cloneSource;
    };

    void        setStatus(const QString &message, bool isError = false);
    void        updateDirty();
    void        updateImpactSummary();
    void        bindModels();
    Draft      *current();
    const Draft *current() const;
    Draft      &ensureDraft(const std::string &toolId, const ToolDefinition &definition);
    bool        toolIdTaken(const std::string &toolId) const;
    int         referencingNodes(const std::string &toolId) const;
    std::vector<std::string> effectiveCategories(const std::string &toolId,
                                                 const ToolDefinition &definition) const;
    bool        folderHasTools(const QString &path) const;
    // Whether this declaration may be authored here: an own tool always, an installed one only with
    // the switch, and only when it has a file to write back to.
    bool        toolEditable(const ToolDefinition &definition, bool hasSaved) const;
    QVariantList buildTree() const;

    AppController                  *app_ = nullptr;
    CustomToolStore                *store_ = nullptr;
    std::unique_ptr<CustomToolStore> ownedStore_;

    std::map<std::string, Draft> drafts_;
    std::string                  current_;
    std::set<std::string>        deleted_;
    std::vector<std::string>     folders_;
    int                          newToolSeq_ = 0;

    ToolPortListModel inputModel_;
    ToolPortListModel outputModel_;
    ToolPortListModel initModel_;

    bool    editBuiltins_ = false;
    bool    dirty_ = false;
    QString statusMessage_;
    bool    statusError_ = false;
    QString impactSummary_;
};

} // namespace visionrt
