#pragma once
#include "ui/AppController.h"
#include "ui/ToolTreeModel.h"
#include "ui/OutputModel.h"
#include "ui/ImageViewport.h"
#include "core/ProjectEditor.h"
#include "core/ProjectStorage.h"
#include "python/PythonRuntime.h"
#include "runtime/RuntimeEngine.h"
#include "plugins/PluginLoader.h"
#include <QPointer>
#include <QTimer>
#include <deque>
#include <future>
#include <set>

namespace visionrt {
namespace workbench {
QString q(const std::string& text);
QString pathText(const std::filesystem::path& path);
std::filesystem::path localPath(const QString& text);
NodeId nodeId(const QString& text);
QString typeName(Type type);
const PortDefinition* port(const std::vector<PortDefinition>& ports, std::string_view id);
QVariantMap metadata(const ToolDefinition& definition);
YAML::Node metadataYaml(const QVariantMap& map);
}
struct AppController::Impl {
    explicit Impl(AppController* owner, std::size_t workers, const QString& directory);
    ~Impl();
    AppController* q;
    std::filesystem::path applicationDirectory;
    std::filesystem::path toolsPackageRoot;
    std::filesystem::path indexFile;
    std::shared_ptr<PluginLoader> pluginLoader;
    ToolRegistry registry;
    ProjectEditor editor;
    std::unique_ptr<PythonRuntime> python;
    std::unique_ptr<RuntimeEngine> runtime;
    ToolTreeModel tools;
    OutputModel outputs;
    DisplayConverter converter;
    QTimer timer;
    RuntimeState state = RuntimeState::Idle;
    std::uint64_t generation = 1;
    std::map<NodeId, ResultSlot::Snapshot> snapshots;
    std::set<NodeId> active;
    std::map<NodeId, ToolStatus> lifecycleErrors;
    QString flow = "flow_1";
    QString selected;
    QString search;
    QString timing;
    std::deque<QString> diagnostics;
    QString diagnostic;
    std::string copiedFlow;
    std::vector<NodeId> copiedNodes;
    enum class Pending { None, New, Open, Close, Reset };
    Pending pending = Pending::None;
    QString pendingPath;
    bool pendingApproved = false;
    bool awaitingDecision = false;
    bool pendingReset = false;
    std::optional<ProjectDefinition> replacement;
    bool refreshReconcile = false;
    struct RefreshResult { std::vector<PythonImplementation> implementations; std::vector<std::pair<std::string, ToolFactory>> factories; };
    std::shared_ptr<RefreshResult> refreshResult;
    QPointer<ImageViewport> viewport;
    std::vector<QPointer<ImageViewport>> thumbnails;
    std::vector<QPointer<ImageViewport>> displayTargets;
    std::vector<OwnedImage> displayImages;
    QString displayColor;
    std::vector<Roi> displayInputs, displayOutputs;
    std::optional<RoiEditTarget> displayRoiTarget;
    quint64 presentedRequest = 0;
    quint64 displayRequest = 0;
    QString imageOutput;
    int index = 0;
    int count = 0;
    struct RoiPreview {
        NodeId node;
        std::string input;
        ImageBatch images;
        RoiBatch rois;
        int index = 0;
        int resultIndex = 0;
        ImageViewport::ViewState view;
    };
    std::optional<RoiPreview> roi;
    struct TextResult { QString text; bool copy; std::uint64_t generation; };
    std::future<TextResult> textTask;
    std::optional<OutputModel::Captured> approvedText;
    bool approvedCopy = false;
    const FlowDefinition* currentFlow() const;
    const NodeDefinition* currentNode() const;
    const NodeDefinition* findNode(NodeId id) const;
    const ToolDefinition* currentTool() const;
    const PortDefinition* parameter(const std::string& id, bool initialization) const;
    bool idle();
    void report(const QString& text);
    void changed(bool structure = false);
    template<class Function> bool command(Function&& operation, bool structure = false) {
        if (!idle()) return false;
        try { if (operation()) changed(structure); return true; }
        catch (const std::exception& error) { report(workbench::q(error.what())); return false; }
    }
    void setState(RuntimeState value);
    void poll();
    void start(RunMode mode);
    void request(Pending action, QString path = {});
    void advancePending();
    void clearResults();
    void refresh(bool rescan);
    void rescanTools();
    void refreshFinished();
    void updateResults(bool newSnapshot = false);
    void updateDisplay();
    void applyOverlays();
    PortValue resolvedInput(const NodeDefinition& node, const std::string& input) const;
    void commitRoi(const RoiEditTarget& target, const Roi* value);
    void finishRoi();
    void storageDiagnostics(const std::vector<ProjectStorageDiagnostic>& items);
};
}
