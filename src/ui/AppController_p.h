#pragma once
#include "core/ProjectEditor.h"
#include "log/LogModel.h"
#include "core/ProjectStorage.h"
#include "plugins/PluginLoader.h"
#include "python/PythonBackend.h"
#include "python/PythonProbe.h"
#include "runtime/RuntimeEngine.h"
#include "ui/AppController.h"
#include "ui/AssistPreview.h"
#include "ui/UiPreferences.h"
#include "ui/Diagnostics.h"
#include "ui/ImageViewport.h"
#include "ui/NodeListModel.h"
#include "ui/OutputModel.h"
#include "ui/ProjectOperations.h"
#include "ui/ResultPresentation.h"
#include "ui/RecentProjects.h"
#include "ui/RoiEditSession.h"
#include "ui/ToolTreeModel.h"
#include "ui/ToolEditorController.h"
#include "core/CustomToolStore.h"
#include "ui/WireRouter.h"
#include "common/Version.h"

#include <QPointer>
#include <QPointF>
#include <QTimer>
#include <atomic>
#include <deque>
#include <functional>
#include <future>
#include <map>
#include <set>
#include <string_view>
#include <utility>
#include <variant>

namespace visionrt {
namespace workbench {
QString               q(const std::string &text);
QString               pathText(const std::filesystem::path &path);
std::filesystem::path localPath(const QString &text);
NodeId                nodeId(const QString &text);
const PortDefinition *port(const std::vector<PortDefinition> &ports, PortId id);

// Controller-owned parameter rows carry a negative row id: they open an editor, they are not a port.
inline constexpr int kGuardRow     = -1;
inline constexpr int kExpressionRow = -2;
inline constexpr int kConditionRow = -3;
inline constexpr int kSelectTypeRow = -4;
QVariantMap           metadata(const ToolDefinition &definition);
} // namespace workbench

struct AppController::Impl : PresentationHost
{
    explicit Impl(AppController *owner, std::size_t workers, const QString &directory);
    ~Impl();

    // Records a project that reached a committed open or a successful save-as. A repeated record of
    // the list head is a no-op so a plain Ctrl+S never churns the menu.
    void recordRecent(const QString &path);
    // Newest-first history shown by the file menu; loaded once so a broken preferences file can
    // never delay the first project open.
    QVector<RecentProjectEntry> recentProjects;
    // The three platform settings this process started with. Settings are a snapshot: an applied
    // change replaces this value, and the interpreter is only replaced by the next start.
    Settings                    settings;
    // Interpreter a test process stated before the first controller existed. It is the only way a
    // test supplies one; a shipped build leaves it empty and reads the saved choice instead.
    static QString testPythonExecutable;
    // Reason the last settings commit was refused; shown by the settings window and cleared by the
    // next successful apply.
    QString                     settingsError;
    QString                     settingsErrorField;
    // A draft whose interpreter is being checked. Exactly one commit is in flight: a second apply is
    // refused until the check answers, so no two probes overlap.
    std::optional<Settings>     pendingSettings;
    void requireAvailableTool(std::string_view toolId) const;
    // 读取随包发布的宿主与 Contracts 构建元数据；工具包的元数据在 loadToolsPackage 中读取。
    void loadComponentBuildInfo();
    AppController                         *q;
    std::filesystem::path                  applicationDirectory;
    std::filesystem::path                  toolsPackageRoot;
    bool                                   toolsPackageInstalled{false};
    // Set before the members are torn down: a runtime answer that was already posted must not read
    // them, and the timer it was posted through is destroyed with this object.
    bool                                   tearingDown = false;
    // 三个组件的构建元数据分别来自各自的构建产物元数据文件，缺失即为未知，绝不互相顶替。
    std::optional<ComponentBuildInfo>      hostBuildInfo;
    std::optional<ComponentBuildInfo>      contractsBuildInfo;
    std::optional<ComponentBuildInfo>      toolsPackageBuildInfo;
    std::shared_ptr<PluginLoader>          pluginLoader;
    ToolRegistry                           registry;
    ProjectEditor                          editor;
    WireRouter                             wireRouter;
    std::unique_ptr<PythonBackend>         python;
    // Each environment check runs here, off the GUI thread. One object, one check at a time.
    std::unique_ptr<PythonProbe>           pythonProbe;
    // What the running check is for: the startup environment decision, or a draft about to be
    // committed. The startup answer starts the bridge; a draft answer only allows the write.
    enum class PythonCheck
    {
        None,
        Startup,
        Draft
    };
    PythonCheck                            pythonCheck = PythonCheck::None;
    // The environment the check accepted, kept for the evidence report. The status carries what the
    // window shows; this carries the prefix and search paths the bridge was started with, which the
    // status deliberately does not expose to every reader.
    std::optional<PythonEnvironment>       pythonEnvironment;
    // A probe answer arrived while the runtime was busy: the tool statuses are re-registered as soon
    // as it is idle again, instead of reporting "只能刷新" to the user.
    bool                                   pythonRefreshPending = false;
    std::unique_ptr<RuntimeEngine>         runtime;
    ToolTreeModel                          tools;
    OutputModel                            outputs;
    NodeListModel                          nodesModel{q};
    std::unique_ptr<LogModel>              logModel;
    QTimer                                 timer;
    RuntimeState                           state        = RuntimeState::Idle;
    std::uint64_t                          generation   = 1;
    std::uint64_t                          projectEpoch = 1;
    // Newest {session, round} accepted per node. Parallel workers post in their own order, so an
    // event older than one already applied is dropped instead of clearing a newer running state.
    struct ActivityEpoch
    {
        std::uint64_t session = 0;
        std::uint64_t round   = 0;
    };
    std::map<NodeId, ActivityEpoch>        activityEpoch;
    QString                                flow = "flow_1";
    QString                                selected;
    QString                                search;
    QString                                timing;
    std::map<std::string, double>          flowLatestMs;
    std::optional<double>                  latestRoundMs;
    double                                 latestElapsedMs = 0;
    // Session that owns the bottom complete-run statistics; single-node sessions never adopt it.
    std::uint64_t                          fullRunSession = 0;
    std::deque<ApplicationDiagnostic>                       diagnostics;
    std::optional<CopiedSelection>         clipboard;
    std::set<std::string>                  routingErrors;
    // File-operation state has one owner; the controller executes side effects around it.
    ProjectOperations operations;
    CustomToolStore   customStore;
    ToolEditorController toolEditor;
    // A user run owns the workspace; synchronize, refresh and reset are management tasks that keep
    // the previous editing rules.
    bool runActive() const noexcept
    {
        return state == RuntimeState::RunningOnce || state == RuntimeState::RunningLoop
            || state == RuntimeState::RunningNode || state == RuntimeState::Stopping;
    }

    struct PendingDeletion
    {
        std::string         flow;
        std::vector<NodeId> nodes;
    };

    std::optional<PendingDeletion> pendingDeletion;
    bool                          refreshReconcile = false;

    struct RefreshResult
    {
        std::vector<PythonImplementation>                implementations;
        std::vector<std::pair<std::string, ToolFactory>> factories;
        // The environment these answers were produced under. A refresh runs on a worker, so the
        // live status may already have moved on by the time the window reads the result back.
        PythonEnvironmentStatus                          environment;
    };

    std::shared_ptr<RefreshResult>       refreshResult;
    // The UI consumes the same materialised contract execution does: expression results, condition
    // variables and select ports must have one name/type/signature source. The cache is an identity
    // check on the node revision, never a second derivation.
    struct ContractCache
    {
        NodeId                node              = 0;
        std::uint64_t         executionRevision = 0;
        std::uint64_t         epoch             = 0;
        const ToolDefinition *source            = nullptr;
        std::shared_ptr<const ToolDefinition> contract;
    };
    mutable ContractCache contractCache;

    // Connectability depends on the project, flow, graph revision and node only, so one query serves
    // both the name highlight and the downstream candidate menu instead of rescanning per frame.
    struct ConnectableState
    {
        std::uint64_t                                                epoch = 0;
        QString                                                      flow;
        std::uint64_t                                                structureRevision = 0;
        NodeId                                                       node              = 0;
        bool                                                         valid             = false;
        std::map<PortId, bool>                                       connectable;
        std::map<PortId, std::vector<CandidateInput>>               candidates;
    };
    mutable ConnectableState connectable;

    // ROI editing state lives in one place; the controller only coordinates start/end with the
    // viewport and the project.
    RoiEditSession roiSession{editor};
    // Presentation state (snapshots, output rows, image batch, text/readback and its drain) has one
    // owner; the controller only connects QML and selection changes to it.
    ResultPresentation presentation{*this, outputs, roiSession, q};

    // Smart point selection runs on its own bounded channel; the controller only forwards the
    // cursor and owns the proposal transaction.
    std::unique_ptr<AssistPreview>        assist;
    std::string                           assistToolId;
    bool                                  assistEnabled = false;
    bool                                  easyTouchEnabled = UiPreferences::easyTouchEnabled();
    QString                               assistHint;
    QString                               assistDisabledReason;
    ROI                                   assistPreviewRois;
    std::optional<AssistContext>          assistPreviewContext;

    QVariantList                                       parameterRows;
    NodeId                                             parameterRowsNode = 0;

    const NodeDefinition                *currentNode() const;
    const NodeDefinition                *findNode(NodeId id) const;
    const ToolDefinition                *currentTool() const;
    bool                                 idle();
    bool                                 applyParameter(PortId id, bool initialization, PortValue value);
    void                                 report(const QString &text);
    void                                 report(ApplicationDiagnostic diagnostic);
    void                                 report(const ErrorInfo &error);
    void                                 report(const std::exception &error, const QString &summary);
    void                                 changed(bool structure = false);
    NodeListModel::Row                   nodeRow(const NodeDefinition &node) const;
    void                                 refreshNodeModel();
    void                                 publishWiresChanged();
    QString                              wireState;
    void                                 publishNodesChanged();

    template<class Function>
    bool command(Function &&operation, bool structure = false)
    {
        if (!idle())
            return false;
        try
        {
            if (operation())
                changed(structure);
            return true;
        }
        catch (const std::exception &error)
        {
            report(error, QStringLiteral("配置操作失败"));
            return false;
        }
    }

    void      setState(RuntimeState value);
    // Single acceptance predicate for hover, click, pending dispatch and result delivery.
    bool                 assistRequestsAllowed() const;
    bool                 readsAllowed() const noexcept;
    // Blocking drain, reserved for the destructor (last line of defence).
    void                 drainReaders();
    // Asynchronous drain used by every management flow: continues on the GUI thread once the
    // display worker and the text task released their references.
    void                 beginDrain(std::function<void()> continuation);
    void                 finishDrain();
    void                 updateReadback();
    void      poll();
    // Drains coalesced node activity events, applies only the transient running set, then polls for
    // the authoritative snapshot. Never derives status, timing or errors from the event itself.
    void      applyActivity();
    void      start(RunMode mode);
    void      beginRun(RunMode mode);
    void      request(ProjectOperations::Action action, QString path = {});
    void      advancePending();
    void      finishClose();
    void      clearResults();
    bool      nodeExists(NodeId id) const;
    // PresentationHost: the presentation reads these facts and reports these effects only.
    const NodeDefinition *selectedNode() const override { return currentNode(); }
    const FlowDefinition *currentFlow() const override;
    const ToolDefinition *effectiveTool() const override;
    const PortDefinition *parameter(PortId id, bool initialization) const override;
    const std::map<PortId, bool> *refreshConnectable(const FlowDefinition &flow,
                                                                      const NodeDefinition &node) const override;
    bool runtimeIdle() const override { return state == RuntimeState::Idle; }
    bool closing() const override { return operations.isClosing(); }
    bool contextCurrent(const QVariantMap &context) const override { return q->portContextCurrent(context); }
    QVariantMap portContext(PortId port) const override { return q->portContext(port, true); }
    void deliverText(const QString &text) override { emit q->outputTextReady(text); }
    void resultsChanged() override { emit q->resultsChanged(); }
    void runtimeChanged() override { emit q->runtimeChanged(); }
    void endRoiEdit() override { finishRoi(); }
    void reportDiagnostic(const ApplicationDiagnostic &diagnostic) override { report(diagnostic); }
    void reportFailure(const std::exception &error, const QString &summary) override { report(error, summary); }
    void      pruneDeletedNodes();
    void      synchronizeNodes();
    void      requestSynchronize();
    void      refresh(bool rescan);
    void      requestRefresh(bool rescan);
    // Starts the one check a selection gets. The phase becomes Probing for a startup check, so the
    // tool list says why Python tools are not usable yet. False means a check was already running
    // and the caller must not treat the running answer as its own.
    bool      beginPythonCheck(const QString &selection, PythonCheck purpose);
    void      finishPythonCheck();
    // Re-registers every tool status once the runtime is idle; a request during a run waits.
    void      schedulePythonRefresh();
    // Commits a validated draft: writes the file, then the running behaviour. Emits settingsApplied.
    bool      commitSettings(Settings next);
    // Test seam only: pumps the event loop until the startup check and the refresh it triggers have
    // both finished, so a test observes the same steady state a user reaches a moment later.
    void      waitForPythonCheck();
    void      loadToolsPackage();
    void      rescanTools();
    void      refreshFinished();
    void      updateResults();
    void      updateDisplay();
    ResultSlot::Snapshot displaySnapshot(const NodeDefinition &node) const;
    void      applyOverlays();
    const PortValue *resolvedInputRef(const NodeDefinition &node, PortId input) const;
    PortValue        resolvedInput(const NodeDefinition &node, PortId input) const;
    void                 commitRoi(const RoiEditTarget &target, const EncodedRegion *value);
    void      finishRoi();
    void      storageDiagnostics(const std::vector<ProjectStorageDiagnostic> &items);

    // Smart point selection.
    void                          refreshAssist();
    void                          setAssistHint(const QString &text);
    void                          clearAssistPreview();
    std::optional<AssistContext>  assistContext(QString *reason) const;
    bool                          assistContextCurrent(const AssistContext &context) const;
    std::optional<AssistRequest>  assistRequest(bool learn, bool append, const QPointF &cursor,
                                                QString *reason) const;
    void                          applyAssistOutcome(const AssistOutcome &outcome);
};
} // namespace visionrt
