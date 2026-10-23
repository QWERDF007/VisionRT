#pragma once
#include "core/ProjectEditor.h"
#include "core/ProjectStorage.h"
#include "plugins/PluginLoader.h"
#include "python/PythonRuntime.h"
#include "runtime/RuntimeEngine.h"
#include "ui/AppController.h"
#include "ui/Diagnostics.h"
#include "ui/ImageViewport.h"
#include "ui/OutputModel.h"
#include "ui/ToolTreeModel.h"
#include "ui/WireRouter.h"

#include <QPointer>
#include <QTimer>
#include <deque>
#include <future>
#include <set>
#include <variant>

namespace visionrt {
namespace workbench {
QString               q(const std::string &text);
QString               pathText(const std::filesystem::path &path);
std::filesystem::path localPath(const QString &text);
NodeId                nodeId(const QString &text);
const PortDefinition *port(const std::vector<PortDefinition> &ports, std::string_view id);
QVariantMap           metadata(const ToolDefinition &definition);
} // namespace workbench

struct AppController::Impl
{
    explicit Impl(AppController *owner, std::size_t workers, const QString &directory);
    ~Impl();
    AppController                         *q;
    std::filesystem::path                  applicationDirectory;
    std::filesystem::path                  toolsPackageRoot;
    std::shared_ptr<PluginLoader>          pluginLoader;
    ToolRegistry                           registry;
    ProjectEditor                          editor;
    WireRouter                             wireRouter;
    std::optional<QVariantList>            nodeRows;
    std::unique_ptr<PythonRuntime>         python;
    std::unique_ptr<RuntimeEngine>         runtime;
    ToolTreeModel                          tools;
    OutputModel                            outputs;
    DisplayConverter                       converter;
    QTimer                                 timer;
    RuntimeState                           state        = RuntimeState::Idle;
    std::uint64_t                          generation   = 1;
    std::uint64_t                          projectEpoch = 1;
    std::map<NodeId, ResultSlot::Snapshot> snapshots;
    std::set<NodeId>                       active;
    std::map<NodeId, std::shared_ptr<const NodeDiagnostic>> lifecycleErrors;
    std::map<NodeId, std::shared_ptr<const NodeDiagnostic>> instanceErrors;
    QString                                flow = "flow_1";
    QString                                selected;
    QString                                search;
    QString                                timing;
    std::deque<ApplicationDiagnostic>                       diagnostics;
    std::optional<CopiedSelection>         clipboard;
    std::set<std::string>                  routingErrors;
    enum class Pending
    {
        None,
        New,
        Open,
        Close,
        Reset,
        DeleteNodes
    };
    Pending                          pending = Pending::None;
    QString                          pendingPath;
    bool                             pendingApproved  = false;
    bool                             awaitingDecision = false;
    bool                             pendingReset     = false;
    std::optional<ProjectDefinition> replacement;

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
    };

    std::shared_ptr<RefreshResult>       refreshResult;
    QPointer<ImageViewport>              viewport;
    std::vector<QPointer<ImageViewport>> thumbnails;
    std::vector<QPointer<ImageViewport>> displayTargets;
    ROI::value_type                      displayInputs, displayOutputs;
    std::optional<RoiEditTarget>         displayRoiTarget;
    quint64                              displayRequest   = 0;
    QString                              imageOutput;
    int                                  index = 0;
    int                                  count = 0;

    struct RoiPreview
    {
        std::uint64_t            id;
        NodeId                   node;
        std::string              input;
        QString                  flow;
        TypeSpec                 type;
        bool                     singleRectangle;
        InputBinding             original;
        PortValue                background;
        PortValue                draft;
        int                      index       = 0;
        int                      resultIndex = 0;
        ImageViewport::ViewState view;
        bool                     fieldsValid = true;
    };

    std::optional<RoiPreview> roi;
    std::uint64_t            roiSerial = 0;

    struct TextRequest
    {
        QVariantMap context;
        bool        copy;
        bool        diagnostic;
    };

    struct SaveRequest
    {
        QString       path;
        std::uint64_t project;
    };

    std::deque<std::variant<TextRequest, SaveRequest>> readRequests;
    std::optional<RuntimeReadback>                     readback;
    QVariantList                                       parameterRows;
    NodeId                                             parameterRowsNode = 0;

    struct TextResult
    {
        QString       text;
        bool          copy;
        std::uint64_t generation;
        QVariantMap   context;
    };

    std::future<TextResult>              textTask;
    QVariantMap                         approvedTextContext;
    bool                                 approvedCopy = false;
    bool                                 approvedDiagnostic = false;
    const FlowDefinition                *currentFlow() const;
    const NodeDefinition                *currentNode() const;
    const NodeDefinition                *findNode(NodeId id) const;
    const ToolDefinition                *currentTool() const;
    const PortDefinition                *parameter(const std::string &id, bool initialization) const;
    bool                                 idle();
    bool                                 applyParameter(const QString &id, bool initialization, PortValue value);
    void                                 report(const QString &text);
    void                                 report(ApplicationDiagnostic diagnostic);
    void                                 report(const std::exception &error, const QString &summary);
    void                                 changed(bool structure = false);
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
    bool                 readsAllowed() const noexcept;
    void                 drainReaders();
    void                 updateReadback();
    void      poll();
    void      start(RunMode mode);
    void      request(Pending action, QString path = {});
    void      advancePending();
    void      clearResults();
    bool      nodeExists(NodeId id) const;
    void      pruneDeletedNodes();
    void      synchronizeNodes();
    void      refresh(bool rescan);
    void      loadToolsPackage();
    void      rescanTools();
    void      refreshFinished();
    void      updateResults();
    void      updateDisplay();
    void      applyOverlays();
    const PortValue *resolvedInputRef(const NodeDefinition &node, const std::string &input) const;
    PortValue resolvedInput(const NodeDefinition &node, const std::string &input) const;
    void                 commitRoi(const RoiEditTarget &target, const ROI::value_type::value_type *value);
    ResultSlot::Snapshot displaySnapshot(const NodeDefinition &node) const;
    void      finishRoi();
    void      storageDiagnostics(const std::vector<ProjectStorageDiagnostic> &items);
};
} // namespace visionrt
