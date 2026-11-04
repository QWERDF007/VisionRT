#pragma once

#include "core/CustomToolStore.h"
#include "core/ToolDefinition.h"
#include "ui/ToolPortListModel.h"

#include <QObject>
#include <QString>
#include <QVariantList>
#include <QVariantMap>
#include <memory>
#include <optional>

namespace visionrt {

class AppController;

class ToolEditorController : public QObject
{
    Q_OBJECT

    Q_PROPERTY(QVariantList tools READ tools NOTIFY toolsChanged)
    Q_PROPERTY(QString currentToolId READ currentToolId WRITE setCurrentToolId NOTIFY currentToolChanged)
    Q_PROPERTY(bool isToolIdEditable READ isToolIdEditable NOTIFY currentToolChanged)
    Q_PROPERTY(QString currentToolName READ currentToolName WRITE setCurrentToolName NOTIFY currentToolChanged)
    Q_PROPERTY(QString currentToolKind READ currentToolKind NOTIFY currentToolChanged)
    Q_PROPERTY(QString currentToolCategory READ currentToolCategory WRITE setCurrentToolCategory NOTIFY currentToolChanged)
    Q_PROPERTY(QString currentToolVersion READ currentToolVersion WRITE setCurrentToolVersion NOTIFY currentToolChanged)
    Q_PROPERTY(QString currentToolScript READ currentToolScript NOTIFY currentToolChanged)
    Q_PROPERTY(QString currentStage READ currentStage WRITE setCurrentStage NOTIFY stageChanged)
    Q_PROPERTY(visionrt::ToolPortListModel *portModel READ portModel CONSTANT)
    Q_PROPERTY(bool isEditable READ isEditable NOTIFY currentToolChanged)
    // Copying carries a script: a Python tool (own or installed) can be copied into the user root,
    // while a registered native factory has nothing to carry and stays read-only.
    Q_PROPERTY(bool canClone READ canClone NOTIFY currentToolChanged)
    Q_PROPERTY(bool isDirty READ isDirty NOTIFY dirtyChanged)
    Q_PROPERTY(bool canApply READ canApply NOTIFY dirtyChanged)
    Q_PROPERTY(QString statusMessage READ statusMessage NOTIFY statusChanged)
    Q_PROPERTY(bool isStatusError READ isStatusError NOTIFY statusChanged)
    Q_PROPERTY(QVariantList supportedTypes READ supportedTypes CONSTANT)
    // The parameter pages a declaration may name. The editor offers exactly these, because a free
    // text tab would only fail validation on apply.
    Q_PROPERTY(QStringList tabs READ tabs CONSTANT)
    Q_PROPERTY(QString impactSummary READ impactSummary NOTIFY impactSummaryChanged)

public:
    explicit ToolEditorController(QObject *parent = nullptr);
    ~ToolEditorController() override;

    void setAppController(AppController *app);
    void setCustomStore(CustomToolStore *store);

    QVariantList tools() const;
    QString      currentToolId() const;
    // Only a draft that was never applied may be renamed: a published id names the declaration, the
    // code package and every project binding, so it locks at the first commit.
    void         setCurrentToolId(const QString &value);
    bool         isToolIdEditable() const noexcept;
    QString      currentToolName() const;
    void         setCurrentToolName(const QString &value);
    QString      currentToolKind() const;
    QString      currentToolCategory() const;
    void         setCurrentToolCategory(const QString &value);
    QString      currentToolVersion() const;
    void         setCurrentToolVersion(const QString &value);
    QString      currentToolScript() const;
    QString      currentStage() const;
    void         setCurrentStage(const QString &stage);

    ToolPortListModel *portModel() noexcept
    {
        return &portModel_;
    }

    bool isEditable() const noexcept;
    bool canClone() const;
    bool isDirty() const noexcept;
    bool canApply() const noexcept;

    QString statusMessage() const;
    bool    isStatusError() const noexcept;

    QVariantList supportedTypes() const;
    QStringList  tabs() const;
    QString      impactSummary() const;

    Q_INVOKABLE void selectTool(const QString &toolId);
    Q_INVOKABLE bool newTool(const QString &nameZh, const QString &nameEn, const QString &kind, const QString &category);
    Q_INVOKABLE bool cloneTool(const QString &toolId);
    Q_INVOKABLE bool deleteTool(const QString &toolId);
    Q_INVOKABLE bool apply();
    Q_INVOKABLE void revert();
    Q_INVOKABLE QString previewJson() const;
    Q_INVOKABLE void openCodeFile() const;
    Q_INVOKABLE void openCodeDirectory() const;
    // How many nodes of the open project reference this tool. The delete prompt and the impact
    // preview read the same count, so "referenced" has one meaning in the editor.
    Q_INVOKABLE int toolUsage(const QString &toolId) const;

signals:
    void toolsChanged();
    void currentToolChanged();
    void stageChanged();
    void dirtyChanged();
    void statusChanged();
    void impactSummaryChanged();

private:
    void setStatus(const QString &message, bool isError = false);
    void updateDirty();
    void updateImpactSummary();
    // A tool id names a declaration file and a code package, so a second tool may not take it.
    bool toolIdTaken(const std::string &toolId) const;
    int  referencingNodes(const std::string &toolId) const;
    void bindDraftToPortModel();

    AppController                  *app_ = nullptr;
    CustomToolStore                *store_ = nullptr;
    std::unique_ptr<CustomToolStore> ownedStore_;

    std::optional<ToolDefinition> draft_;
    std::optional<ToolDefinition> saved_;
    QString                       stage_ = QStringLiteral("inputs");
    ToolPortListModel             portModel_;

    bool    dirty_ = false;
    QString statusMessage_;
    bool    statusError_ = false;
    QString impactSummary_;
};

} // namespace visionrt
