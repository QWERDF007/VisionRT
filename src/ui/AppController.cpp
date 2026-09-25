#include "ui/AppController_p.h"
#include "core/ValueYaml.h"
#include "tools/CpuTools.h"
#include <QClipboard>
#include <QCoreApplication>
#include <QDir>
#include <QGuiApplication>
#include <QUrl>
#include <algorithm>
#include <chrono>
#include <cmath>
#include <limits>

namespace visionrt {
namespace workbench {
QString q(const std::string& text) { return QString::fromUtf8(text.data(), static_cast<qsizetype>(text.size())); }
QString pathText(const std::filesystem::path& path) {
    const auto text = path.generic_u8string();
    return QString::fromUtf8(reinterpret_cast<const char*>(text.data()), static_cast<qsizetype>(text.size()));
}
std::filesystem::path localPath(const QString& text) {
    const QUrl url(text);
    const auto local = url.isLocalFile() ? url.toLocalFile() : text;
    return std::filesystem::u8path(local.toUtf8().toStdString()).lexically_normal();
}
NodeId nodeId(const QString& text) {
    bool okay = false;
    const auto id = text.toULongLong(&okay);
    if (!okay || text.isEmpty() || text.startsWith('-')) throw ContractError("Invalid node ID");
    return id;
}
QString typeName(Type type) {
    static const char* names[] = {"Int", "Float", "Bool", "String", "ImageBatch", "NumericArray", "StringArray", "RoiBatch"};
    return QString::fromLatin1(names[static_cast<int>(type)]);
}
const PortDefinition* port(const std::vector<PortDefinition>& ports, std::string_view id) {
    for (const auto& value : ports) if (value.id == id) return &value;
    return nullptr;
}
}
using workbench::q;
using workbench::nodeId;
namespace {
std::filesystem::path pythonHome(const std::filesystem::path& application) {
    const auto override = qEnvironmentVariable("VISIONRT_PYTHON_HOME");
    if (!override.isEmpty()) return workbench::localPath(override);
    if (std::filesystem::is_directory(application / "python")) return application / "python";
#ifdef VISIONRT_PYTHON_HOME
    return std::filesystem::u8path(VISIONRT_PYTHON_HOME);
#else
    return {};
#endif
}
std::vector<NodeId> ids(const QVariantList& values) {
    std::vector<NodeId> result; result.reserve(values.size());
    for (const auto& value : values) result.push_back(nodeId(value.toString()));
    return result;
}
QString stateName(RuntimeState state) {
    switch (state) {
    case RuntimeState::Idle: return "Idle";
    case RuntimeState::RunningOnce: return "RunningOnce";
    case RuntimeState::RunningLoop: return "RunningLoop";
    case RuntimeState::RunningNode: return "RunningNode";
    case RuntimeState::Stopping: return "Stopping";
    case RuntimeState::Resetting: return "Resetting";
    case RuntimeState::Refreshing: return "Refreshing";
    }
    return {};
}
}
AppController::Impl::Impl(AppController* owner, std::size_t workers, const QString& directory)
    : q(owner), applicationDirectory(workbench::localPath(directory)), tools(owner), outputs(owner), converter(owner) {
    registry.scan(applicationDirectory / "tools");
    registerCpuTools(registry);
    registerInferTools(registry);
    for (const auto& item : registry.diagnostics()) report(workbench::pathText(item.path) + ": " + workbench::q(item.message));
    // Interpreter lifetime encloses the worker pool and all Python instances.
    try { python = std::make_unique<PythonRuntime>(pythonHome(applicationDirectory)); }
    catch (const std::exception& error) { report(QStringLiteral("Python 不可用：") + workbench::q(error.what())); }
    runtime = std::make_unique<RuntimeEngine>(workers);
    runtime->setCompletionCallback([owner](RuntimeCompletion completion) {
        QMetaObject::invokeMethod(owner, [owner, completion] {
            if (owner->impl_ && completion.generation >= owner->impl_->generation) owner->impl_->poll();
        }, Qt::QueuedConnection);
    });
    QObject::connect(&timer, &QTimer::timeout, owner, [this] { poll(); });
    timer.setInterval(34);
    timer.start();
    QObject::connect(&converter, &DisplayConverter::batchConverted, owner, [this](const std::vector<DisplayFrame>& frames) {
        for (const auto& frame : frames) {
            if (frame.generation != generation || frame.requestId != displayRequest || frame.batchIndex < 0 ||
                static_cast<std::size_t>(frame.batchIndex) >= displayTargets.size()) continue;
            if (auto target = displayTargets[frame.batchIndex]) {
                target->setDisplayFrame(frame);
                if (target == viewport) { presentedRequest = frame.requestId; applyOverlays(); }
            }
        }
    });
    tools.setRegistry(registry);
    if (std::any_of(registry.definitions().begin(), registry.definitions().end(), [](const auto& tool) {
            return tool.kind == ToolKind::PythonFunction || tool.kind == ToolKind::PythonClass; })) refresh(false);
}
AppController::Impl::~Impl() {
    timer.stop();
    runtime->setCompletionCallback({});
    runtime.reset();
    if (textTask.valid()) textTask.wait();
    if (gpuTask.valid()) gpuTask.wait();
    refreshResult.reset();
    for (const auto& tool : registry.definitions())
        if (tool.kind == ToolKind::PythonFunction || tool.kind == ToolKind::PythonClass)
            registry.setPythonImplementation(tool.id, {}, {-3, "Interpreter shutting down"});
    python.reset();
}
const FlowDefinition* AppController::Impl::currentFlow() const {
    for (const auto& value : editor.project().flows) if (value.id == flow.toStdString()) return &value;
    return nullptr;
}
const NodeDefinition* AppController::Impl::currentNode() const {
    if (selected.isEmpty()) return nullptr;
    const auto* current = currentFlow();
    if (current) for (const auto& node : current->nodes) if (QString::number(node.id) == selected) return &node;
    return nullptr;
}
const NodeDefinition* AppController::Impl::findNode(NodeId id) const {
    for (const auto& item : editor.project().flows) for (const auto& node : item.nodes) if (node.id == id) return &node;
    return nullptr;
}
const ToolDefinition* AppController::Impl::currentTool() const {
    const auto* node = currentNode(); return node ? registry.find(node->toolId) : nullptr;
}
const PortDefinition* AppController::Impl::parameter(const std::string& id, bool initialization) const {
    const auto* tool = currentTool(); return tool ? workbench::port(initialization ? tool->initParams : tool->inputs, id) : nullptr;
}
bool AppController::Impl::idle() {
    if (state == RuntimeState::Idle && pending == Pending::None) return true;
    report(QStringLiteral("当前任务尚未结束，配置编辑不可用")); return false;
}
void AppController::Impl::report(const QString& text) {
    if (text.isEmpty()) return;
    const auto bounded = text.size() > 16384 ? text.left(16360) + QStringLiteral("…[诊断已截断]") : text;
    if (diagnostics.empty() || diagnostics.back() != bounded) diagnostics.push_back(bounded);
    while (diagnostics.size() > 100) diagnostics.pop_front();
    diagnostic = bounded;
    emit q->diagnosticChanged();
}
void AppController::Impl::setState(RuntimeState value) {
    if (state == value) return;
    state = value; editor.setBusy(state != RuntimeState::Idle);
    if (viewport) viewport->setReadOnly(state != RuntimeState::Idle || !roi);
    emit q->runtimeChanged();
}
void AppController::Impl::changed(bool structure) {
    if (!currentNode() && !selected.isEmpty()) { finishRoi(); selected.clear(); emit q->selectionChanged(); }
    emit q->projectChanged(); emit q->nodesChanged(); emit q->parametersChanged();
    if (structure || !search.isEmpty()) emit q->searchChanged();
    updateResults();
}
void AppController::Impl::storageDiagnostics(const std::vector<ProjectStorageDiagnostic>& items) {
    for (const auto& item : items) report(workbench::q(item.message));
}
void AppController::Impl::clearResults() {
    snapshots.clear(); active.clear(); lifecycleErrors.clear(); timing.clear();
    finishRoi(); converter.invalidate(generation); updateResults(true);
    emit q->nodesChanged(); emit q->runtimeChanged();
}
void AppController::Impl::poll() {
    auto update = runtime->pollLatest();
    if (update.generation >= generation) {
        if (update.generation != generation) { generation = update.generation; clearResults(); }
        bool nodesChanged = false, selectedChanged = false;
        for (auto& item : update.nodes) {
            nodesChanged = true;
            if (item.active) active.insert(item.node); else active.erase(item.node);
            if (item.lifecycleDiagnostic) lifecycleErrors[item.node] = *item.lifecycleDiagnostic;
            if (item.snapshot && item.snapshot->generation == generation) {
                snapshots[item.node] = std::move(item.snapshot);
                selectedChanged |= QString::number(item.node) == selected;
            }
        }
        for (const auto& message : update.diagnostics) report(workbench::q(message));
        if (update.roundMilliseconds > 0) {
            timing = QStringLiteral("本轮 %1 ms").arg(update.roundMilliseconds, 0, 'f', 2);
            for (const auto& item : update.flows) timing += QStringLiteral(" · %1 %2 ms").arg(workbench::q(item.flow)).arg(item.milliseconds, 0, 'f', 2);
            emit q->runtimeChanged();
        }
        setState(update.state);
        if (nodesChanged) emit q->nodesChanged();
        if (selectedChanged) updateResults(true);
        if (update.completion && runtime->acknowledgeCompletion(*update.completion)) {
            setState(RuntimeState::Idle);
            if (refreshResult) refreshFinished();
            advancePending();
        }
    }
    using namespace std::chrono_literals;
    if (textTask.valid() && textTask.wait_for(0ms) == std::future_status::ready) {
        try {
            auto result = textTask.get();
            if (result.generation == generation) {
                if (result.copy) QGuiApplication::clipboard()->setText(result.text);
                emit q->outputTextReady(result.text);
            }
        } catch (const std::exception& error) { report(workbench::q(error.what())); }
    }
    if (gpuTask.valid() && gpuTask.wait_for(0ms) == std::future_status::ready) {
        QVariantList devices;
        try {
            auto result = gpuTask.get();
            gpuInfo = workbench::q(result.status.message);
            if (!result.status) report(gpuInfo);
            for (const auto& device : result.devices)
                devices.push_back(QVariantMap{{"id", QString::number(device.index)}, {"index", device.index}, {"name", workbench::q(device.name)}});
        } catch (const std::exception& error) { report(workbench::q(error.what())); }
        emit q->gpuDevicesReady(devices);
    }
}
void AppController::Impl::start(RunMode mode) {
    if (!idle()) return;
    finishRoi();
    std::optional<NodeId> target;
    if (mode == RunMode::Node) {
        const auto* node = currentNode();
        if (!node) { report(QStringLiteral("请先选择工具节点")); return; }
        target = node->id;
    }
    const auto result = runtime->start(editor.project(), registry, mode, target);
    if (!result) { report(workbench::q(result.message)); return; }
    active.clear(); lifecycleErrors.clear();
    setState(mode == RunMode::Once ? RuntimeState::RunningOnce : mode == RunMode::Loop ? RuntimeState::RunningLoop : RuntimeState::RunningNode);
}
void AppController::Impl::request(Pending action, QString path) {
    if (pending != Pending::None) { report(QStringLiteral("请先完成或取消当前文件操作")); return; }
    finishRoi(); pending = action; pendingPath = std::move(path); pendingApproved = action == Pending::Reset;
    awaitingDecision = false; pendingReset = false; replacement.reset();
    if (state != RuntimeState::Idle) {
        runtime->requestStop();
        if (state == RuntimeState::RunningOnce || state == RuntimeState::RunningLoop || state == RuntimeState::RunningNode) setState(RuntimeState::Stopping);
    } else advancePending();
}
void AppController::Impl::advancePending() {
    if (pending == Pending::None || state != RuntimeState::Idle) return;
    if (!pendingApproved && editor.project().dirty) {
        if (!awaitingDecision) { awaitingDecision = true; emit q->unsavedDecisionRequired(); }
        return;
    }
    pendingApproved = true;
    if (pending != Pending::Close && !pendingReset) {
        try {
            if (pending == Pending::Open) {
                auto loaded = ProjectStorage(registry).load(workbench::localPath(pendingPath));
                storageDiagnostics(loaded.diagnostics); replacement = std::move(loaded.project);
            } else if (pending == Pending::New) replacement = ProjectDefinition{};
        } catch (const std::exception& error) {
            report(workbench::q(error.what())); pending = Pending::None; return;
        }
        const auto result = runtime->requestReset();
        if (!result) { report(workbench::q(result.message)); pending = Pending::None; return; }
        pendingReset = true; setState(RuntimeState::Resetting); return;
    }
    const auto completed = pending;
    pending = Pending::None; pendingPath.clear(); awaitingDecision = false; pendingApproved = false; pendingReset = false;
    if (replacement) {
        editor.replaceProject(std::move(*replacement)); replacement.reset();
        flow = editor.project().flows.empty() ? QString{} : workbench::q(editor.project().flows.front().id);
        selected.clear(); copiedNodes.clear(); copiedFlow.clear(); search.clear();
        clearResults(); changed(true); emit q->selectionChanged(); emit q->projectReplaced();
    }
    if (completed == Pending::Close) emit q->closeReady();
}
void AppController::Impl::refresh(bool rescan) {
    if (state != RuntimeState::Idle) return;
    finishRoi();
    if (rescan) registry.scanPython(applicationDirectory / "tools");
    auto result = std::make_shared<RefreshResult>();
    auto definitions = registry.definitions();
    auto* pythonRuntime = python.get();
    const auto accepted = runtime->requestRefresh([result, definitions = std::move(definitions), pythonRuntime] {
        if (pythonRuntime) {
            result->implementations = pythonRuntime->refresh(definitions);
            for (const auto& tool : definitions) if (tool.kind == ToolKind::PythonFunction || tool.kind == ToolKind::PythonClass) {
                const auto status = std::find_if(result->implementations.begin(), result->implementations.end(), [&](const auto& item) { return item.toolId == tool.id; });
                if (status != result->implementations.end() && status->status) result->factories.emplace_back(tool.id, pythonRuntime->factory(tool));
            }
        } else for (const auto& tool : definitions) if (tool.kind == ToolKind::PythonFunction || tool.kind == ToolKind::PythonClass)
            result->implementations.push_back({tool.id, {-3, "Python interpreter unavailable"}});
        return ToolStatus{};
    });
    if (!accepted) { report(workbench::q(accepted.message)); return; }
    refreshResult = std::move(result); refreshReconcile = rescan; setState(RuntimeState::Refreshing);
}
void AppController::Impl::refreshFinished() {
    auto result = std::move(refreshResult);
    for (auto& implementation : result->implementations) {
        ToolFactory factory;
        auto found = std::find_if(result->factories.begin(), result->factories.end(), [&](const auto& item) { return item.first == implementation.toolId; });
        if (found != result->factories.end()) factory = std::move(found->second);
        registry.setPythonImplementation(implementation.toolId, std::move(factory), implementation.status);
        if (!implementation.status) report(QStringLiteral("定义已加载，实现不可用：") + workbench::q(implementation.status.message));
    }
    for (const auto& item : registry.diagnostics()) report(workbench::pathText(item.path) + ": " + workbench::q(item.message));
    if (refreshReconcile) {
        try { auto reconciled = ProjectStorage(registry).reconcile(editor.project()); storageDiagnostics(reconciled.diagnostics); editor.replaceProject(std::move(reconciled.project)); }
        catch (const std::exception& error) { report(workbench::q(error.what())); }
    }
    refreshReconcile = false; tools.setRegistry(registry); changed(true);
}
AppController::AppController(std::size_t workers, const QString& applicationDirectory, QObject* parent)
    : QObject(parent), impl_(std::make_unique<Impl>(this, workers, applicationDirectory)) {}
AppController::~AppController() = default;
QString AppController::title() const { return q(impl_->editor.project().name) + (dirty() ? " *" : "") + " — VisionRT V3"; }
bool AppController::busy() const { return impl_->state != RuntimeState::Idle; }
QString AppController::runState() const { return stateName(impl_->state); }
QString AppController::diagnostic() const { return impl_->diagnostic; }
QString AppController::currentFlow() const { return impl_->flow; }
QString AppController::selectedNode() const { return impl_->selected; }
QString AppController::timingText() const { return impl_->timing; }
bool AppController::dirty() const { return impl_->editor.project().dirty; }
QString AppController::aboutText() const {
    return QStringLiteral("VisionRT V3\nCopyright © VisionRT contributors\nQt %1\nPython: %2\nInferRT: 按需加载 VisionRTInfer.dll\n%3")
        .arg(QString::fromLatin1(qVersion()), impl_->python ? QStringLiteral("解释器可用") : QStringLiteral("不可用"), impl_->gpuInfo);
}
QAbstractItemModel* AppController::toolModel() const { return &impl_->tools; }
QAbstractItemModel* AppController::outputModel() const { return &impl_->outputs; }
QVariantList AppController::flows() const {
    QVariantList result;
    for (const auto& flow : impl_->editor.project().flows)
        result.push_back(QVariantMap{{"id", q(flow.id)}, {"name", q(flow.name)}, {"selected", flow.runSelected}});
    return result;
}
QVariantList AppController::nodes() const {
    QVariantList result;
    if (const auto* flow = impl_->currentFlow()) for (const auto& node : flow->nodes) {
        QString state = "gray", timing, detail;
        if (impl_->active.contains(node.id)) state = "blue";
        const auto snapshot = impl_->snapshots.find(node.id);
        if (snapshot != impl_->snapshots.end()) {
            const auto& value = *snapshot->second;
            if (state != "blue") state = value.status ? "green" : "red";
            timing = QStringLiteral("%1 ms").arg(value.timing.totalMs, 0, 'f', 2);
            detail = QStringLiteral("prepare %1 · init %2 · execute %3 · convert %4 · GIL %5 ms")
                .arg(value.timing.prepareMs, 0, 'f', 2).arg(value.timing.initMs, 0, 'f', 2).arg(value.timing.executeMs, 0, 'f', 2)
                .arg(value.timing.convertMs, 0, 'f', 2).arg(value.timing.waitGilMs, 0, 'f', 2);
        }
        if (impl_->lifecycleErrors.contains(node.id)) { state = "red"; detail += "\n" + q(impl_->lifecycleErrors.at(node.id).message); }
        const bool missing = impl_->registry.availability(node.toolId).state != ToolAvailability::Available;
        result.push_back(QVariantMap{{"id", QString::number(node.id)}, {"title", q(node.title())}, {"alias", q(node.alias)},
            {"x", node.position.x}, {"y", node.position.y}, {"state", state}, {"timing", timing}, {"timingDetail", detail}, {"missing", missing}});
    }
    return result;
}
QVariantList AppController::wires() const {
    QVariantList result;
    if (const auto* flow = impl_->currentFlow()) {
        std::map<std::pair<NodeId, NodeId>, int> counts;
        for (const auto& node : flow->nodes) for (const auto& [id, binding] : node.inputs)
            if (binding.mode == BindingMode::Upstream && binding.source) ++counts[{binding.source->node, node.id}];
        for (const auto& [edge, count] : counts) result.push_back(QVariantMap{{"source", QString::number(edge.first)}, {"target", QString::number(edge.second)}, {"count", count}});
    }
    return result;
}
QVariantList AppController::parameters() const {
    QVariantList result;
    const auto* node = impl_->currentNode(); if (!node) return result;
    const auto* tool = impl_->currentTool();
    auto add = [&](const std::string& id, const TypeSpec& type, bool initialization, const PortDefinition* declaration) {
        PortValue literal = Missing{};
        QString mode = "literal", source;
        if (initialization) { const auto found = node->initParams.find(id); if (found != node->initParams.end()) literal = found->second; }
        else if (const auto found = node->inputs.find(id); found != node->inputs.end()) {
            literal = found->second.literal; mode = found->second.mode == BindingMode::Literal ? "literal" : "upstream";
            if (found->second.source) {
                const auto* parent = impl_->findNode(found->second.source->node);
                source = (parent ? q(parent->title()) : QString::number(found->second.source->node)) + " → " + q(found->second.source->port);
            }
        }
        QString text;
        try { text = valueText(literal, type); } catch (const std::exception& error) { text = q(error.what()); }
        QVariantList options;
        if (declaration) for (const auto& value : declaration->options) options.push_back(valueText(value, type));
        const auto* string = std::get_if<Utf8>(&literal);
        QVariantMap row{{"id", q(id)}, {"name", declaration ? q(declaration->name) : q(id)}, {"type", workbench::typeName(type.type)},
            {"typeDetail", typeText(type)}, {"init", initialization}, {"mode", mode}, {"literal", text}, {"valueText", string ? q(string->str()) : text},
            {"source", source}, {"title", q(node->title())}, {"editor", declaration ? q(declaration->editor) : QString{}},
            {"tab", declaration ? q(declaration->tab) : QStringLiteral("基本输入")},
            {"group", initialization ? QStringLiteral("初始化配置") : declaration ? q(declaration->group) : QString{}},
            {"options", options}, {"optionsProvider", declaration ? q(declaration->optionsProvider) : QString{}},
            {"imageInput", declaration ? q(declaration->imageInput) : QString{}}, {"minimum", QString{}}, {"maximum", QString{}}, {"step", QString{}}, {"decimals", -1}};
        if (declaration) {
            if (declaration->minimum) row["minimum"] = valueText(*declaration->minimum, type);
            if (declaration->maximum) row["maximum"] = valueText(*declaration->maximum, type);
            if (declaration->step) row["step"] = valueText(*declaration->step, type);
            if (declaration->decimals) row["decimals"] = *declaration->decimals;
        }
        result.push_back(row);
    };
    if (tool) {
        for (const auto& port : tool->initParams) add(port.id, port.type, true, &port);
        for (const auto& port : tool->inputs) add(port.id, port.type, false, &port);
    } else {
        for (const auto& [id, type] : node->signatures.initParams) add(id, type, true, nullptr);
        for (const auto& [id, type] : node->signatures.inputs) add(id, type, false, nullptr);
    }
    return result;
}
void AppController::selectFlow(const QString& id) {
    if (id == impl_->flow) return;
    for (const auto& flow : impl_->editor.project().flows) if (q(flow.id) == id) {
        impl_->finishRoi(); impl_->flow = id; impl_->selected.clear();
        emit selectionChanged(); emit nodesChanged(); emit parametersChanged(); impl_->updateResults(true); return;
    }
}
void AppController::selectNode(const QString& id) {
    if (id == impl_->selected) return;
    if (!id.isEmpty()) {
        const auto* flow = impl_->currentFlow();
        if (!flow || std::none_of(flow->nodes.begin(), flow->nodes.end(), [&](const auto& node) { return QString::number(node.id) == id; })) return;
    }
    impl_->finishRoi(); impl_->selected = id; emit selectionChanged(); emit parametersChanged(); impl_->updateResults(true);
}
void AppController::filterTools(const QString& text) { impl_->tools.setFilter(text); }
void AppController::searchNodes(const QString& text) { impl_->search = text.trimmed(); emit searchChanged(); }
QVariantList AppController::searchResults() const {
    QVariantList result; if (impl_->search.isEmpty()) return result;
    for (const auto& flow : impl_->editor.project().flows) for (const auto& node : flow.nodes)
        if (q(node.title()).contains(impl_->search, Qt::CaseInsensitive))
            result.push_back(QVariantMap{{"flow", q(flow.id)}, {"flowName", q(flow.name)}, {"node", QString::number(node.id)}, {"title", q(node.title())}});
    return result;
}
void AppController::locateNode(const QString& flow, const QString& node) { selectFlow(flow); selectNode(node); if (impl_->selected == node) emit nodeLocated(node); }
void AppController::addNode(const QString& toolId, int x, int y) {
    NodeId added = 0;
    if (impl_->command([&] {
        const auto* tool = impl_->registry.find(toolId.toStdString()); if (!tool) throw ContractError("Tool definition missing");
        added = impl_->editor.addNode(impl_->flow.toStdString(), *tool, {x, y}); return true;
    }, true)) selectNode(QString::number(added));
}
bool AppController::moveNodes(const QVariantList& values, int dx, int dy) {
    return impl_->command([&] { return impl_->editor.moveNodes(impl_->flow.toStdString(), ids(values), {dx, dy}); }, true);
}
void AppController::deleteNodes(const QVariantList& values) {
    impl_->command([&] { impl_->finishRoi(); return impl_->editor.removeNodes(impl_->flow.toStdString(), ids(values)); }, true);
}
void AppController::copyNodes(const QVariantList& values) {
    try { impl_->copiedNodes = ids(values); impl_->copiedFlow = impl_->flow.toStdString(); }
    catch (const std::exception& error) { impl_->report(q(error.what())); }
}
void AppController::pasteNodes(int x, int y) {
    impl_->command([&] {
        if (impl_->copiedNodes.empty()) return false;
        int minimumX = std::numeric_limits<int>::max(), minimumY = minimumX;
        for (const auto id : impl_->copiedNodes) if (const auto* node = impl_->findNode(id)) {
            minimumX = std::min(minimumX, node->position.x); minimumY = std::min(minimumY, node->position.y);
        }
        if (minimumX == std::numeric_limits<int>::max()) throw ContractError("Copied nodes no longer exist");
        const auto dx = static_cast<std::int64_t>(x) - minimumX, dy = static_cast<std::int64_t>(y) - minimumY;
        if (dx < std::numeric_limits<int>::min() || dx > std::numeric_limits<int>::max() || dy < std::numeric_limits<int>::min() || dy > std::numeric_limits<int>::max())
            throw ContractError("Paste grid offset is outside int range");
        return !impl_->editor.copyNodes(impl_->copiedFlow, impl_->copiedNodes, impl_->flow.toStdString(), {static_cast<int>(dx), static_cast<int>(dy)}).empty();
    }, true);
}
void AppController::renameNode(const QString& id, const QString& alias) { impl_->command([&] { return impl_->editor.setAlias(impl_->flow.toStdString(), nodeId(id), alias.toStdString()); }); }
void AppController::addFlow(const QString& name) {
    std::string id; if (impl_->command([&] { id = impl_->editor.addFlow(name.toStdString()); return true; }, true)) selectFlow(q(id));
}
void AppController::renameFlow(const QString& id, const QString& name) { impl_->command([&] { return impl_->editor.renameFlow(id.toStdString(), name.toStdString()); }); }
void AppController::deleteFlow(const QString& id) {
    std::string adjacent;
    if (impl_->command([&] { impl_->finishRoi(); adjacent = impl_->editor.removeFlow(id.toStdString()); return true; }, true) && impl_->flow == id) selectFlow(q(adjacent));
}
void AppController::copyFlow(const QString& id) {
    std::string copied; if (impl_->command([&] { copied = impl_->editor.copyFlow(id.toStdString()); return true; }, true)) selectFlow(q(copied));
}
void AppController::setFlowSelected(const QString& id, bool selected) { impl_->command([&] { return impl_->editor.setRunSelected(id.toStdString(), selected); }); }
bool AppController::setParameter(const QString& id, bool initialization, const QString& yamlText) {
    return impl_->command([&] {
        const auto* node = impl_->currentNode(); if (!node) throw ContractError("No selected node");
        const auto& signatures = initialization ? node->signatures.initParams : node->signatures.inputs;
        const auto signature = signatures.find(id.toStdString()); if (signature == signatures.end()) throw ContractError("Unknown parameter");
        const auto value = decodeValue(YAML::Load(yamlText.toStdString()), signature->second);
        if (const auto* declaration = impl_->parameter(id.toStdString(), initialization); declaration && !isMissing(value)) {
            auto less = [&](const PortValue& a, const PortValue& b) {
                return signature->second.type == Type::Int ? std::get<std::int64_t>(a) < std::get<std::int64_t>(b) : std::get<double>(a) < std::get<double>(b);
            };
            if (signature->second.type == Type::Float && !std::isfinite(std::get<double>(value))) throw ContractError("Numeric parameter must be finite");
            if ((declaration->minimum && less(value, *declaration->minimum)) || (declaration->maximum && less(*declaration->maximum, value))) throw ContractError("Value is outside the declared range");
            if (!declaration->options.empty() && std::none_of(declaration->options.begin(), declaration->options.end(), [&](const auto& option) { return equalValues(value, option); })) throw ContractError("Value is not a declared option");
        }
        return initialization ? impl_->editor.setInitParam(impl_->flow.toStdString(), node->id, id.toStdString(), value)
            : impl_->editor.setLiteral(impl_->flow.toStdString(), node->id, id.toStdString(), value);
    });
}
bool AppController::setParameterMode(const QString& id, bool upstream) {
    return impl_->command([&] { const auto* node = impl_->currentNode(); if (!node) throw ContractError("No selected node");
        impl_->finishRoi(); return impl_->editor.setMode(impl_->flow.toStdString(), node->id, id.toStdString(), upstream ? BindingMode::Upstream : BindingMode::Literal); });
}
QVariantList AppController::bindingCandidates(const QString& input) const {
    QVariantList result; const auto* node = impl_->currentNode(); if (!node) return result;
    try { for (const auto& source : impl_->editor.compatibleSources(impl_->flow.toStdString(), node->id, input.toStdString())) {
        const auto* parent = impl_->findNode(source.node); if (!parent) continue;
        const auto* definition = impl_->registry.find(parent->toolId);
        const auto* port = definition ? workbench::port(definition->outputs, source.port) : nullptr;
        result.push_back(QVariantMap{{"node", QString::number(source.node)}, {"title", q(parent->title())}, {"port", q(source.port)},
            {"name", port ? q(port->name) : q(source.port)}, {"type", port ? typeText(port->type) : QString{}}});
    } } catch (const std::exception& error) { impl_->report(q(error.what())); }
    return result;
}
bool AppController::bindInput(const QString& input, const QString& node, const QString& output) {
    return impl_->command([&] { const auto* target = impl_->currentNode(); if (!target) throw ContractError("No selected node");
        auto binding = target->inputs.at(input.toStdString()); binding.mode = BindingMode::Upstream; binding.source = SourcePort{nodeId(node), output.toStdString()};
        impl_->finishRoi(); return impl_->editor.setBinding(impl_->flow.toStdString(), target->id, input.toStdString(), std::move(binding)); });
}
void AppController::unbindInput(const QString& input) {
    impl_->command([&] { const auto* node = impl_->currentNode(); if (!node) throw ContractError("No selected node");
        impl_->finishRoi(); return impl_->editor.unbind(impl_->flow.toStdString(), node->id, input.toStdString()); });
}
QVariantList AppController::relationTargets(const QString& output) const {
    QVariantList result; const auto* node = impl_->currentNode(); if (!node) return result;
    try { for (const auto& input : impl_->editor.compatibleInputs(impl_->flow.toStdString(), {node->id, output.toStdString()})) {
        const auto* target = impl_->findNode(input.node); if (!target) continue;
        const auto* definition = impl_->registry.find(target->toolId);
        const auto* port = definition ? workbench::port(definition->inputs, input.port) : nullptr;
        result.push_back(QVariantMap{{"node", QString::number(input.node)}, {"title", q(target->title())}, {"port", q(input.port)},
            {"name", port ? q(port->name) : q(input.port)}, {"type", port ? typeText(port->type) : QString{}}});
    } } catch (const std::exception& error) { impl_->report(q(error.what())); }
    return result;
}
bool AppController::bindOutputTo(const QString& output, const QString& node, const QString& input) {
    return impl_->command([&] {
        const auto* source = impl_->currentNode(); const auto* target = impl_->findNode(nodeId(node));
        if (!source || !target) throw ContractError("Node not found");
        const auto candidates = impl_->editor.compatibleInputs(impl_->flow.toStdString(), {source->id, output.toStdString()});
        if (std::find(candidates.begin(), candidates.end(), InputPort{target->id, input.toStdString()}) == candidates.end()) throw ContractError("Input is occupied, incompatible or cyclic");
        auto binding = target->inputs.at(input.toStdString()); binding.mode = BindingMode::Upstream; binding.source = SourcePort{source->id, output.toStdString()};
        return impl_->editor.setBinding(impl_->flow.toStdString(), target->id, input.toStdString(), std::move(binding));
    });
}
void AppController::runOnce() { impl_->start(RunMode::Once); }
void AppController::runNode() { impl_->start(RunMode::Node); }
void AppController::toggleLoop() {
    if (impl_->state == RuntimeState::RunningLoop) { impl_->runtime->requestStop(); impl_->setState(RuntimeState::Stopping); }
    else impl_->start(RunMode::Loop);
}
void AppController::resetRuntime() { impl_->request(Impl::Pending::Reset); }
void AppController::refreshPython() { if (impl_->idle()) impl_->refresh(true); }
void AppController::requestNew() { impl_->request(Impl::Pending::New); }
void AppController::requestOpen(const QString& path) { if (!path.isEmpty()) impl_->request(Impl::Pending::Open, path); }
void AppController::requestClose() { impl_->request(Impl::Pending::Close); }
void AppController::resolveUnsaved(const QString& decision, const QString& savePath) {
    if (impl_->pending == Impl::Pending::None || !impl_->awaitingDecision) return;
    if (decision == "cancel") { impl_->pending = Impl::Pending::None; impl_->awaitingDecision = false; impl_->replacement.reset(); return; }
    if (decision == "save") { if (!save(savePath)) return; }
    else if (decision != "discard") return;
    impl_->pendingApproved = true; impl_->awaitingDecision = false; impl_->advancePending();
}
bool AppController::save(const QString& path) {
    auto candidate = impl_->editor.project();
    auto destination = path.isEmpty() ? candidate.path : workbench::localPath(path);
    if (destination.empty()) { emit savePathRequired(); return false; }
    if (busy() && destination != candidate.path) { impl_->report(QStringLiteral("运行期间不能另存为；可保存当前已命名工程")); return false; }
    try {
        auto diagnostics = ProjectStorage(impl_->registry).save(candidate, destination);
        if (busy()) impl_->editor.markSaved(); else impl_->editor.replaceProject(std::move(candidate));
        impl_->storageDiagnostics(diagnostics); emit projectChanged(); return true;
    } catch (const std::exception& error) { impl_->report(q(error.what())); return false; }
}
void AppController::requestOutputText(int row, bool copy) {
    if (impl_->textTask.valid()) { impl_->report(QStringLiteral("正在生成完整输出，请稍候")); return; }
    auto captured = impl_->outputs.capture(row); if (!captured) return;
    const auto estimated = OutputModel::estimatedTextBytes(*captured);
    if (estimated > 16 * 1024 * 1024 && (!impl_->approvedText || impl_->approvedCopy != copy ||
        impl_->approvedText->publication != captured->publication || impl_->approvedText->port != captured->port)) {
        impl_->approvedText = *captured; impl_->approvedCopy = copy;
        emit outputTextReady(QStringLiteral("完整数据预计约 %1 MiB。再次点击同一查看/复制操作以确认；不会截断全文。")
            .arg(static_cast<double>(estimated) / (1024 * 1024), 0, 'f', 1)); return;
    }
    impl_->approvedText.reset(); const auto generation = impl_->generation;
    impl_->textTask = std::async(std::launch::async, [captured = std::move(*captured), copy, generation] {
        return Impl::TextResult{OutputModel::fullText(captured), copy, generation};
    });
}
void AppController::queryGpuDevices() {
    if (impl_->gpuTask.valid()) return;
    impl_->gpuTask = std::async(std::launch::async, [] { return visionrt::queryGpuDevices(); });
}
}
