#pragma once

#include "core/ToolDefinition.h"
#include "python/PythonEnvironment.h"
#include "runtime/RuntimeEngine.h"
#include "ui/Export.h"
#include "ui/ToolEditorController.h"
#include "ui/UiState.h"

#include <QAbstractItemModel>
#include <QColor>
#include <QObject>
#include <QPointF>
#include <QString>
#include <QUrl>
#include <QVariantList>
#include <QVariantMap>
#include <memory>
#include <optional>

class QQmlEngine;
class QQuickTextDocument;

namespace visionrt {
class ImageViewport;
class PythonBackend;
class ToolEditorController;
struct ProjectDefinition;

class VISIONRT_UI_API AppController : public QObject
{
    Q_OBJECT
    Q_PROPERTY(QString title READ title NOTIFY projectChanged)
    Q_PROPERTY(bool busy READ busy NOTIFY runtimeChanged)
    Q_PROPERTY(QVariantList parameterTypes READ parameterTypes CONSTANT)
    Q_PROPERTY(QVariantList arrayElementTypes READ arrayElementTypes CONSTANT)
    Q_PROPERTY(bool runtimeBusy READ runtimeBusy NOTIFY runtimeChanged)
    // Single interaction predicate derived from the runtime state: everything except stop and close
    // is unavailable while a run owns the runtime. No second writable "UI locked" flag exists.
    Q_PROPERTY(bool workspaceEnabled READ workspaceEnabled NOTIFY runtimeChanged)
    // True exactly while a run that can still be stopped owns the runtime.
    Q_PROPERTY(bool stopEnabled READ stopEnabled NOTIFY runtimeChanged)
    Q_PROPERTY(visionrt::RuntimeState runState READ runState NOTIFY runtimeChanged)
    Q_PROPERTY(QString runStateText READ runStateText NOTIFY runtimeChanged)
    Q_PROPERTY(QString diagnostic READ diagnostic NOTIFY diagnosticChanged)
    Q_PROPERTY(QStringList diagnostics READ diagnostics NOTIFY diagnosticChanged)
    Q_PROPERTY(QVariantList flows READ flows NOTIFY projectChanged)
    Q_PROPERTY(QVariantList nodes READ nodes NOTIFY nodesChanged)
    Q_PROPERTY(QAbstractListModel *nodeListModel READ nodeListModel CONSTANT)
    Q_PROPERTY(QVariantList wires READ wires NOTIFY wiresChanged)
    Q_PROPERTY(QString currentFlow READ currentFlow WRITE selectFlow NOTIFY selectionChanged)
    Q_PROPERTY(QString selectedNode READ selectedNode NOTIFY selectionChanged)
    Q_PROPERTY(QVariantList parameters READ parameters NOTIFY parametersChanged)
    Q_PROPERTY(QVariantList parameterGroups READ parameterGroups NOTIFY parametersChanged)
    Q_PROPERTY(QAbstractListModel *parameterModel READ parameterModel CONSTANT)
    Q_PROPERTY(QString outputTitle READ outputTitle NOTIFY resultsChanged)
    Q_PROPERTY(QVariantList parameterTabs READ parameterTabs NOTIFY selectionChanged)
    Q_PROPERTY(QVariantList imageOutputs READ imageOutputs NOTIFY resultsChanged)
    // Training is an optional tool capability. When the selected tool declares none, every one of
    // these stays empty and the image panel keeps its ordinary processing layout.
    // Shape palette of the image toolbar: it only acts while regions are editable, either in the
    // project ROI editor or in a training record.
    Q_PROPERTY(bool shapeToolsAvailable READ shapeToolsAvailable NOTIFY resultsChanged)
    Q_PROPERTY(int drawShape READ drawShape NOTIFY resultsChanged)
    Q_PROPERTY(bool trainingAvailable READ trainingAvailable NOTIFY trainingChanged)
    Q_PROPERTY(bool trainingMode READ trainingMode WRITE setTrainingMode NOTIFY trainingChanged)
    Q_PROPERTY(QVariantList trainingRecords READ trainingRecords NOTIFY trainingChanged)
    Q_PROPERTY(QAbstractListModel *trainingRecordModel READ trainingRecordModel CONSTANT)
    Q_PROPERTY(QString selectedTrainingDraftRecord READ selectedTrainingDraftRecord NOTIFY trainingChanged)
    Q_PROPERTY(bool trainingSavable READ trainingSavable NOTIFY trainingChanged)
    Q_PROPERTY(bool canAddTrainingImage READ canAddTrainingImage NOTIFY resultsChanged)
    Q_PROPERTY(int activeRoiParameter READ activeRoiParameter NOTIFY resultsChanged)
    Q_PROPERTY(int activeRoiShape READ activeRoiShape NOTIFY resultsChanged)
    Q_PROPERTY(int imageIndex READ imageIndex WRITE setImageIndex NOTIFY resultsChanged)
    Q_PROPERTY(int imageCount READ imageCount NOTIFY resultsChanged)
    Q_PROPERTY(QString selectedImageOutput READ selectedImageOutput WRITE setImageOutput NOTIFY resultsChanged)
    Q_PROPERTY(quint64 resultThumbnailVersion READ resultThumbnailVersion NOTIFY resultThumbnailVersionChanged)
    Q_PROPERTY(QAbstractItemModel *toolModel READ toolModel CONSTANT)
    Q_PROPERTY(QAbstractItemModel *outputModel READ outputModel CONSTANT)
    // 日志窗口模型：服务未启动时为空，启动后按可见性批量追加
    Q_PROPERTY(QAbstractItemModel *logModel READ logModel CONSTANT)
    Q_PROPERTY(QVariantList searchResults READ searchResults NOTIFY searchChanged)
    // What the last open or tool refresh found changed under the project. Empty means there is
    // nothing to announce; the window shows this once and leaves it until the user closes it.
    Q_PROPERTY(QString toolUpdateSummary READ toolUpdateSummary NOTIFY toolUpdateSummaryChanged)
    Q_PROPERTY(QString timingText READ timingText NOTIFY timingChanged)
    Q_PROPERTY(bool dirty READ dirty NOTIFY projectChanged)
    Q_PROPERTY(bool canPaste READ canPaste NOTIFY pasteChanged)
    Q_PROPERTY(bool closing READ isClosing NOTIFY closingChanged)
    Q_PROPERTY(QString aboutText READ aboutText NOTIFY aboutTextChanged)
    Q_PROPERTY(visionrt::ToolEditorController *toolEditor READ toolEditor CONSTANT)
    Q_PROPERTY(visionrt::BindingMode conditionMode READ conditionMode NOTIFY parametersChanged)
    Q_PROPERTY(bool easyTouchEnabled READ easyTouchEnabled WRITE setEasyTouchEnabled NOTIFY assistChanged)
    // The tool editor lists the declarations shipped with the installation only when this is on.
    Q_PROPERTY(bool editBuiltins READ editBuiltins WRITE setEditBuiltins NOTIFY editBuiltinsChanged)
    // Why the last settings commit was refused. Empty after a successful apply; the settings window
    // shows it next to the page that owns the field.
    Q_PROPERTY(QString settingsError READ settingsError NOTIFY settingsChanged)
    // Which page owns the refusal: "builtins", "runtime", "logging" or empty for a file-level
    // failure. The window switches to that page instead of leaving the user on an unrelated one.
    Q_PROPERTY(visionrt::SettingsPage settingsErrorField READ settingsErrorField NOTIFY settingsChanged)
    // True while a chosen interpreter is being checked; the window disables its buttons so a second
    // commit cannot start a second check.
    Q_PROPERTY(bool settingsApplying READ settingsApplying NOTIFY settingsChanged)
    // The one host-side answer to "can Python tools run": the phase key and the sentence behind it.
    // Every surface reads this instead of deriving a second verdict of its own.
    Q_PROPERTY(visionrt::PythonEnvironmentPhase pythonEnvironmentPhase READ pythonEnvironmentPhase NOTIFY
                   pythonEnvironmentChanged)
    Q_PROPERTY(QString pythonEnvironmentText READ pythonEnvironmentText NOTIFY pythonEnvironmentChanged)
    Q_PROPERTY(bool assistAvailable READ assistAvailable NOTIFY assistChanged)
    Q_PROPERTY(bool assistActive READ assistActive NOTIFY assistChanged)
    Q_PROPERTY(QString assistDisabledReason READ assistDisabledReason NOTIFY assistChanged)
    Q_PROPERTY(QVariantList assistPreview READ assistPreview NOTIFY assistChanged)
    // Newest first, already labelled for a menu: text plus full-path tooltip. It is pure UI state,
    // so it never dirties the project and never becomes a second opening entry point.
    Q_PROPERTY(QVariantList recentProjects READ recentProjects NOTIFY recentProjectsChanged)
public:
    AppController(std::size_t workers, const QString &applicationDirectory, QObject *parent = nullptr);
    ~AppController() override;
    QString             title() const;
    bool                busy() const;
    bool                workspaceEnabled() const;
    bool                stopEnabled() const;
    // State-only view used by editors: drains caused by display conversion must not disable drawing.
    bool                runtimeBusy() const;
    RuntimeState        runState() const;
    QString             runStateText() const;
    QString             diagnostic() const;
    QStringList         diagnostics() const;
    Q_INVOKABLE QString diagnosticsJson() const;
    QVariantList        flows() const;
    QVariantList        nodes() const;
    QVariantList        wires() const;
    QString             currentFlow() const;
    QString             selectedNode() const;
    QVariantList        parameters() const;
    QAbstractListModel *parameterModel() const;
    QVariantList        parameterTabs() const;
    Q_INVOKABLE bool startTrainingRoi(int parameter, int shape);
    Q_INVOKABLE bool startParameterRoi(int parameter, int stage, int shape, bool recordScoped = false);
    Q_INVOKABLE bool loadParameterImage(int parameter, int stage, const QUrl &url, const QString &record = {});
    Q_INVOKABLE bool selectTrainingDataset(int parameter);
    Q_INVOKABLE bool addTrainingRecord(int parameter);
    Q_INVOKABLE bool setTrainingParameterPath(int parameter, const QUrl &url);
    QVariantList        parameterGroups() const;
    QString             outputTitle() const;
    QVariantList        imageOutputs() const;
    bool                trainingAvailable() const;
    bool                trainingMode() const;
    void                setTrainingMode(bool value);
    QVariantList        trainingRecords() const;
    QAbstractListModel *trainingRecordModel() const;
    QString             selectedTrainingDraftRecord() const;
    bool                trainingDirty() const;
    bool                trainingSavable() const;
    bool                canAddTrainingImage() const;
    // Copies the declared original frame into the in-memory draft and opens its training record.
    Q_INVOKABLE bool    addTrainingImage();
    Q_INVOKABLE void    selectTrainingDraftRecord(const QString &id);
    Q_INVOKABLE void    removeTrainingDraftRecord(const QString &id);
    // The typed value of one declared field. QML binds a control to the field number the
    // declaration publishes, so a renamed field keeps the same value.
    Q_INVOKABLE bool    setTrainingField(const QString &id, int field, const QVariant &value);
    // Tool-scoped training values belong to the draft until publication.
    Q_INVOKABLE bool    setTrainingParameter(int id, const QVariant &value);
    // Calls the business instance's train method and publishes a revision and model references.
    // Failure preserves the draft and previous model while replacing the displayed result.
    Q_INVOKABLE void    applyTraining();
    int                 activeRoiParameter() const;
    bool                shapeToolsAvailable() const;
    int                 drawShape() const;
    // Switches the active editor between selecting and drawing one shape; 0 selects.
    Q_INVOKABLE void    setDrawShape(int shape);
    int                 activeRoiShape() const;
    int                 imageIndex() const;
    int                 imageCount() const;
    QString             selectedImageOutput() const;
    quint64             resultThumbnailVersion() const;
    Q_INVOKABLE QString resultThumbnailUrl(int index) const;
    void                registerImageProviders(QQmlEngine *engine);
    Q_INVOKABLE bool    ensureImageProviders(QObject *contextObject);
    QAbstractItemModel *toolModel() const;
    QAbstractItemModel *outputModel() const;
    QAbstractItemModel *logModel();
    QString             toolUpdateSummary() const;
    // The window has shown the notice; a later refresh starts from an empty summary again.
    Q_INVOKABLE void    dismissToolUpdate();
    // Copies the whole window history as plain text; the selection and the scroll position stay.
    Q_INVOKABLE void    copyAllLogs();
    // Installs the window's level colouring on the text document the log view owns.
    Q_INVOKABLE void    attachLogHighlighter(QQuickTextDocument *document, const QColor &warning, const QColor &error);
    QVariantList        searchResults() const;
    QString             timingText() const;
    bool                dirty() const;
    bool                canPaste() const;
    QString             aboutText() const;
    Q_INVOKABLE void    copyAboutText() const;
    PythonBackend      *pythonBackend() const;
    // Test seam: a test process states the interpreter once — the one its build was configured with
    // — and every controller it constructs probes and starts it, exactly as a saved user choice
    // would. The application never calls this: a shipped build reads the user's choice and nothing
    // else, and an unset value leaves the backend unavailable.
    static void         usePythonForTesting(const QString &executable);
    ToolEditorController              *toolEditor() const noexcept;
    // The open project is the only source an impact preview reads; the tool editor never keeps a
    // second copy of it.
    const ProjectDefinition           &project() const noexcept;
    // Every definition the runtime catalog holds, in one place: the editor lists installed tools
    // from here instead of scanning package roots a second time. Distinct from the QML-facing
    // toolDefinitions() list, which is a presentation of the same catalog.
    const std::vector<ToolDefinition> &toolCatalog() const noexcept;

    Q_INVOKABLE void         attachViewport(QObject *viewport);
    Q_INVOKABLE void         selectFlow(const QString &id);
    Q_INVOKABLE void         selectNode(const QString &id);
    Q_INVOKABLE QVariantList routeWires(const QVariantList &geometry);
    Q_INVOKABLE void         filterTools(const QString &text);
    Q_INVOKABLE void         searchNodes(const QString &text);
    Q_INVOKABLE void         locateNode(const QString &flow, const QString &node);
    Q_INVOKABLE void         addNode(const QString &toolId, int x, int y);
    Q_INVOKABLE bool         moveNodes(const QVariantList &ids, int dx, int dy);
    Q_INVOKABLE void         deleteNodes(const QVariantList &ids);
    Q_INVOKABLE void         copyNodes(const QVariantList &ids);
    Q_INVOKABLE void         pasteNodes(int x, int y);
    Q_INVOKABLE void         renameNode(const QString &id, const QString &alias);
    Q_INVOKABLE void         addFlow(const QString &name);
    Q_INVOKABLE void         renameFlow(const QString &id, const QString &name);
    Q_INVOKABLE void         deleteFlow(const QString &id);
    Q_INVOKABLE void         copyFlow(const QString &id);
    Q_INVOKABLE void         setFlowSelected(const QString &id, bool selected);
    Q_INVOKABLE bool         setParameter(int port, bool initialization, const QString &text, bool jsonInput = false);
    Q_INVOKABLE bool         setParameterPath(int port, bool initialization, const QString &path);
    Q_INVOKABLE bool         setParameterPath(int port, bool initialization, const QUrl &url);
    Q_INVOKABLE bool         stepIntegerParameter(int port, bool initialization, int direction, const QString &literal, int stage = -1, const QString &record = {});
    Q_INVOKABLE bool         setParameterMode(int port, bool upstream);
    Q_INVOKABLE QVariantMap  portDeclarationContext(PortId port, bool output = false) const;
    Q_INVOKABLE QVariantMap  portResultContext(PortId port) const;
    Q_INVOKABLE QVariantMap  portContext(PortId port, bool output = false) const;
    Q_INVOKABLE bool         portContextCurrent(const QVariantMap &context) const;
    Q_INVOKABLE QVariantList bindingCandidates(const QVariantMap &context) const;
    Q_INVOKABLE bool         bindInput(const QVariantMap &context, const QString &node, int output);
    Q_INVOKABLE bool         unbindInput(const QVariantMap &context);
    Q_INVOKABLE QVariantList cascadeOutputCandidates(const QVariantMap &context) const;
    Q_INVOKABLE bool         bindOutputTo(const QVariantMap &context, const QString &node, int input);
    Q_INVOKABLE bool         unbindOutputFrom(const QVariantMap &context, const QString &node, int input);
    Q_INVOKABLE void         runOnce();
    Q_INVOKABLE void         toggleLoop();
    Q_INVOKABLE void         runNode();
    // The one stop entry for single, loop and single-node runs; a repeated request is a no-op.
    Q_INVOKABLE void         stop();
    Q_INVOKABLE void         resetRuntime();
    Q_INVOKABLE void         refreshPython();
    Q_INVOKABLE void         rescanTools();
    Q_INVOKABLE void         requestNew();
    Q_INVOKABLE void         requestOpen(const QString &path);
    // The examples the product installed next to the executable. The menu opens its dialog there and
    // names the same path in its refusal when the installation carries none; the url is empty then.
    Q_INVOKABLE QString      examplesDirectory() const;
    Q_INVOKABLE QUrl         examplesDirectoryUrl() const;
    // Reuses requestOpen, so the recent list never bypasses the unsaved decision, drain or reset.
    Q_INVOKABLE void         openRecentProject(const QString &path);
    Q_INVOKABLE void         requestClose();
    Q_INVOKABLE bool         isClosing() const;
    Q_INVOKABLE void         resolveUnsaved(visionrt::UnsavedDecision decision, const QString &savePath = {});
    // The log window reports whether it can actually show records; hidden windows stay passive.
    Q_INVOKABLE void         setLogViewActive(bool active);
    // A field that rejects its own draft never reaches setParameter; the window still records it.
    Q_INVOKABLE void         logParameterDraftRejected(const QString &detail);
    Q_INVOKABLE bool         save(const QString &path = {});
    Q_INVOKABLE void         setImageIndex(int index);
    Q_INVOKABLE void         setImageOutput(const QString &output);
    Q_INVOKABLE bool         editRoi(int parameter, bool copyUpstream);
    Q_INVOKABLE bool         startRoiDraw(int parameter, int shape = 2);
    Q_INVOKABLE bool         clearRoi(int parameter);
    Q_INVOKABLE void         endRoiPreview();
    Q_INVOKABLE void         requestOutputText(const QVariantMap &context);
    Q_INVOKABLE QVariantMap  expressionConfig(const QString &node) const;
    Q_INVOKABLE bool         applyExpressionConfig(const QString &node, const QVariantList &variables,
                                                   const QVariantList &program);
    Q_INVOKABLE bool         applySingleExpression(const QString &node, const QString &text);
    // Mode switch is one transaction; binding a source is a separate one.
    Q_INVOKABLE bool         setConditionMode(const QString &node, bool upstream);
    Q_INVOKABLE QVariantMap  selectType(const QString &node) const;
    Q_INVOKABLE bool         applySelectType(const QString &node, int type, int dtype, int rank);
    // Chinese description of a type row, reusing the parameter panel's display mapping.
    Q_INVOKABLE QString      typeDescription(int type, int dtype, int rank) const;
    QVariantList parameterTypes() const;
    QVariantList arrayElementTypes() const;
    Q_INVOKABLE QString      dtypeDescription(int dtype) const;
    QAbstractListModel      *nodeListModel() const;
    // Single-row lookups by id: canvas helpers use these instead of scanning a list of maps.
    Q_INVOKABLE QVariantMap  nodeRecord(const QString &id) const;
    Q_INVOKABLE QPointF      nodePosition(const QString &id) const;
    Q_INVOKABLE QVariantList nodePositions() const;
    NodeModel                nodeModel() const;
    BindingMode              conditionMode() const;
    bool                     easyTouchEnabled() const;
    Q_INVOKABLE void         setEasyTouchEnabled(bool enabled);
    bool                     editBuiltins() const;
    Q_INVOKABLE void         setEditBuiltins(bool enabled);
    // The settings window owns one draft for the three pages and commits it once. The snapshot is
    // the applied state; `applySettings` validates the draft, writes the file and only then changes
    // the running controller. A changed interpreter is checked off the GUI thread, so the draft is
    // accepted (true) and the outcome arrives as settingsApplied once the check answers.
    Q_INVOKABLE QVariantMap  settingsSnapshot() const;
    Q_INVOKABLE bool         applySettings(const QVariantMap &values);
    QString                  settingsError() const;
    SettingsPage             settingsErrorField() const;
    bool                     settingsApplying() const;
    PythonEnvironmentPhase   pythonEnvironmentPhase() const;
    QString                  pythonEnvironmentText() const;
    // One machine-readable answer to "which interpreter runs the tools, which NumPy backs it, and
    // what is actually resident in this process". Release evidence archives it; the window keeps
    // reading the two properties above.
    Q_INVOKABLE QVariantMap  pythonEnvironmentReport() const;
    // Writes an installed declaration back to the file it came from. The compiled implementation
    // owns the ports, so a declaration that no longer matches it is refused instead of written.
    bool                     saveBuiltinTool(const ToolDefinition &definition, QString *error = nullptr);
    bool                     assistAvailable() const;
    bool                     assistActive() const;
    QString                  assistDisabledReason() const;
    QVariantList             assistPreview() const;
    QVariantList             recentProjects() const;
    Q_INVOKABLE QVariantList toolDefinitions() const;
    Q_INVOKABLE QVariantMap  toolMetadata(const QString &toolId) const;
    // Smart point-selection. Hover previews a candidate, click learns and commits a proposal,
    // Ctrl+click appends it to the current group. Esc cancels through assistCancel().
    Q_INVOKABLE bool         assistHover(double x, double y);
    Q_INVOKABLE bool         assistClick(double x, double y, bool append);
    Q_INVOKABLE void         assistCancel();
signals:
    void trainingChanged();
    void projectChanged();
    void recentProjectsChanged();
    void nodesChanged();
    // Only a change in the connection topology reaches the wires binding.
    void wiresChanged();
    void selectionChanged();
    void parametersChanged();
    void resultsChanged();
    void resultThumbnailVersionChanged();
    void runtimeChanged();
    void timingChanged();
    void diagnosticChanged();
    void searchChanged();
    void unsavedDecisionRequired();
    void savePathRequired();
    void closeReady();
    void closingChanged();
    void projectReplaced();
    void nodeLocated(const QString &node);
    void outputTextReady(const QString &text);
    void catalogChanged();
    void toolUpdateSummaryChanged();
    void aboutTextChanged();
    void pasteChanged();
    void assistChanged();
    void editBuiltinsChanged();
    void settingsChanged();
    // The outcome of one settings commit: true when it was written, false when the draft was
    // refused (settingsError then names the field and the reason).
    void settingsApplied(bool accepted);
    void pythonEnvironmentChanged();

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};
} // namespace visionrt
