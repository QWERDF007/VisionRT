#include "common/Version.h"
#include "core/Sequence.h"
#include "runtime/Receiver.h"
#include "ui/AppController_p.h"
#include "ui/DurationText.h"
#include "ui/Diagnostics.h"

#include <core/ToolPackage.h>

#include <QCoreApplication>
#include <QDir>
#include <QGuiApplication>
#include <QUrl>
#include <algorithm>
#include <cmath>
#include <limits>

namespace visionrt {
namespace workbench {
QString q(const std::string &text)
{
    return QString::fromUtf8(text.data(), static_cast<qsizetype>(text.size()));
}

QString pathText(const std::filesystem::path &path)
{
    const auto text = path.generic_u8string();
    return QString::fromUtf8(reinterpret_cast<const char *>(text.data()), static_cast<qsizetype>(text.size()));
}

std::filesystem::path localPath(const QString &text)
{
    const QUrl url(text);
    const auto local = url.isLocalFile() ? url.toLocalFile() : text;
    return std::filesystem::u8path(local.toUtf8().toStdString()).lexically_normal();
}

NodeId nodeId(const QString &text)
{
    return parseSequence(text.toUtf8().toStdString(), "节点标识");
}

const PortDefinition *port(const std::vector<PortDefinition> &ports, std::string_view id)
{
    for (const auto &value : ports)
        if (value.id == id)
            return &value;
    return nullptr;
}
} // namespace workbench

using workbench::nodeId;
using workbench::q;

namespace {
std::filesystem::path pythonHome(const std::filesystem::path &application)
{
    const auto override = qEnvironmentVariable("VISIONRT_PYTHON_HOME");
    if (!override.isEmpty())
        return workbench::localPath(override);
    if (std::filesystem::is_directory(application / "python"))
        return application / "python";
#ifdef VISIONRT_PYTHON_HOME
    return std::filesystem::u8path(VISIONRT_PYTHON_HOME);
#else
    return {};
#endif
}

std::vector<NodeId> ids(const QVariantList &values)
{
    std::vector<NodeId> result;
    result.reserve(values.size());
    for (const auto &value : values) result.push_back(nodeId(value.toString()));
    return result;
}

QString stateName(RuntimeState state)
{
    switch (state)
    {
    case RuntimeState::Idle:
        return "Idle";
    case RuntimeState::RunningOnce:
        return "RunningOnce";
    case RuntimeState::RunningLoop:
        return "RunningLoop";
    case RuntimeState::RunningNode:
        return "RunningNode";
    case RuntimeState::Stopping:
        return "Stopping";
    case RuntimeState::Resetting:
        return "Resetting";
    case RuntimeState::Refreshing:
        return "Refreshing";
    }
    return {};
}

// The installed tools package lives at exactly one location; a missing package is an empty
// workbench with a diagnostic, never a reason to probe other candidate roots (D-07).
std::filesystem::path toolsPackageDirectory(const std::filesystem::path &application)
{
    const auto installed = (application / "tools-package").lexically_normal();
    if (std::filesystem::exists(installed / "module_index.json"))
        return installed;
    return {};
}
} // namespace

AppController::Impl::Impl(AppController *owner, std::size_t workers, const QString &directory)
    : q(owner)
    , applicationDirectory(workbench::localPath(directory))
    , tools(owner)
    , outputs(owner)
    , converter(owner)
{
    pluginLoader = std::make_shared<PluginLoader>();
    registry.setPluginLoader(pluginLoader);
    registerReceiveTools(registry);
    loadToolsPackage();
    // Interpreter lifetime encloses the worker pool and all Python instances.
    try
    {
        python = std::make_unique<PythonRuntime>(pythonHome(applicationDirectory));
    }
    catch (const std::exception &error)
    {
        report(error, QStringLiteral("Python 不可用"));
    }
    runtime = std::make_unique<RuntimeEngine>(workers);
    runtime->setReadbackEnabled(true);
    runtime->setCompletionCallback(
        [owner](RuntimeCompletion completion)
        {
            QMetaObject::invokeMethod(
                owner,
                [owner, completion]
                {
                    if (owner->impl_ && completion.generation >= owner->impl_->generation)
                        owner->impl_->poll();
                },
                Qt::QueuedConnection);
        });
    QObject::connect(&timer, &QTimer::timeout, owner, [this] { poll(); });
    timer.setInterval(34);
    timer.start();
    QObject::connect(qGuiApp, &QGuiApplication::applicationStateChanged, owner,
                     [this](Qt::ApplicationState state)
                     {
                         if (state != Qt::ApplicationActive && roi)
                         {
                             finishRoi();
                             updateResults();
                         }
                     });
    QObject::connect(&converter, &DisplayConverter::batchConverted, owner,
                     [this](const std::vector<DisplayFrame> &frames)
                     {
                         for (const auto &frame : frames)
                         {
                             if (frame.generation != generation || frame.requestId != displayRequest
                                 || frame.batchIndex < 0
                                 || static_cast<std::size_t>(frame.batchIndex) >= displayTargets.size())
                                 continue;
                             if (auto target = displayTargets[frame.batchIndex])
                             {
                                 target->setDisplayFrame(frame);
                                 if (target == viewport)
                                 {
                                     applyOverlays();
                                 }
                             }
                         }
                         updateReadback();
                     });
    tools.setRegistry(registry);
    if (!editor.project().flows.empty())
        flow = workbench::q(editor.project().flows.front().id);
    if (std::any_of(registry.definitions().begin(), registry.definitions().end(), [](const auto &tool)
                    { return tool.kind != ToolKind::Host; }))
        refresh(false);
}

AppController::Impl::~Impl()
{
    timer.stop();
    runtime->setCompletionCallback({});
    runtime->requestStop();
    drainReaders();
    runtime->setReadbackEnabled(false);
    runtime.reset();
    textTask = {};
    readRequests.clear();
    if (viewport)
        viewport->endRoiEdit();
    roi.reset();
    outputs.clear();
    snapshots.clear();
    displayInputs.clear();
    displayOutputs.clear();
    clipboard.reset();
    replacement.reset();
    refreshResult.reset();
    for (const auto &tool : registry.definitions())
        if (tool.kind == ToolKind::PythonFunction || tool.kind == ToolKind::PythonClass)
            registry.setPythonImplementation(tool.id, {}, {-3, "Interpreter shutting down"});
    python.reset();
}

const FlowDefinition *AppController::Impl::currentFlow() const
{
    for (const auto &value : editor.project().flows)
        if (value.id == flow.toStdString())
            return &value;
    return nullptr;
}

const NodeDefinition *AppController::Impl::currentNode() const
{
    if (selected.isEmpty())
        return nullptr;
    const auto *current = currentFlow();
    if (current)
        for (const auto &node : current->nodes)
            if (QString::number(node.id) == selected)
                return &node;
    return nullptr;
}

const NodeDefinition *AppController::Impl::findNode(NodeId id) const
{
    for (const auto &item : editor.project().flows)
        for (const auto &node : item.nodes)
            if (node.id == id)
                return &node;
    return nullptr;
}

const ToolDefinition *AppController::Impl::currentTool() const
{
    const auto *node = currentNode();
    return node ? registry.find(node->toolId) : nullptr;
}

const PortDefinition *AppController::Impl::parameter(const std::string &id, bool initialization) const
{
    const auto *tool = currentTool();
    return tool ? workbench::port(initialization ? tool->initParams : tool->inputs, id) : nullptr;
}

bool AppController::Impl::idle()
{
    if (state == RuntimeState::Idle && pending == Pending::None)
        return true;
    report(QStringLiteral("当前任务尚未结束，配置编辑不可用"));
    return false;
}

void AppController::Impl::setState(RuntimeState value)
{
    editor.setBusy(value != RuntimeState::Idle);
    if (state == value)
        return;
    state = value;
    if (viewport)
        viewport->setReadOnly(state != RuntimeState::Idle || !roi);
    emit q->runtimeChanged();
}

bool AppController::Impl::nodeExists(NodeId id) const
{
    for (const auto &flow : editor.project().flows)
        for (const auto &node : flow.nodes)
            if (node.id == id)
                return true;
    return false;
}

void AppController::Impl::pruneDeletedNodes()
{
    std::set<NodeId> existing;
    for (const auto &flow : editor.project().flows)
        for (const auto &node : flow.nodes)
            existing.insert(node.id);
    std::vector<NodeId> removed;
    for (auto it = snapshots.begin(); it != snapshots.end();)
    {
        if (existing.contains(it->first))
        {
            ++it;
            continue;
        }
        removed.push_back(it->first);
        it = snapshots.erase(it);
    }
    for (auto it = lifecycleErrors.begin(); it != lifecycleErrors.end();)
        it = existing.contains(it->first) ? std::next(it) : lifecycleErrors.erase(it);
    for (auto it = instanceErrors.begin(); it != instanceErrors.end();)
        it = existing.contains(it->first) ? std::next(it) : instanceErrors.erase(it);
    for (auto it = active.begin(); it != active.end();)
        it = existing.contains(*it) ? std::next(it) : active.erase(it);
    if (!removed.empty())
    {
        if (roi && !existing.contains(roi->node))
            finishRoi();
        updateResults();
    }
}

void AppController::Impl::publishNodesChanged()
{
    // QML bindings read app.nodes while this signal is delivered; invalidate the
    // cached rows first so they never observe the previous state.
    nodeRows.reset();
    emit q->nodesChanged();
}

void AppController::Impl::changed(bool structure)
{
    if (structure)
        pruneDeletedNodes();
    if (!currentNode() && !selected.isEmpty())
    {
        finishRoi();
        selected.clear();
        emit q->selectionChanged();
    }
    nodeRows.reset();
    emit q->projectChanged();
    emit q->nodesChanged();
    emit q->parametersChanged();
    if (structure || !search.isEmpty())
        emit q->searchChanged();
    synchronizeNodes();
    updateResults();
}

void AppController::Impl::synchronizeNodes()
{
    if (runtime->state() != RuntimeState::Idle)
        return;
    drainReaders();
    const auto status = runtime->requestSynchronize(editor.project(), registry);
    if (!status)
        report(status);
    else if (const auto current = runtime->state(); current != RuntimeState::Idle)
        setState(current);
}

void AppController::Impl::storageDiagnostics(const std::vector<ProjectStorageDiagnostic> &items)
{
    for (const auto &item : items) report(ApplicationDiagnostic{item});
}

void AppController::Impl::clearResults()
{
    readback.reset();
    parameterRows.clear();
    parameterRowsNode = 0;
    snapshots.clear();
    active.clear();
    lifecycleErrors.clear();
    instanceErrors.clear();
    timing.clear();
    finishRoi();
    converter.invalidate(generation);
    updateResults();
    publishNodesChanged();
    emit q->runtimeChanged();
}

void AppController::Impl::poll()
{
    auto update = runtime->pollLatest();
    if (update.generation >= generation)
    {
        if (update.generation != generation)
        {
            generation = update.generation;
            clearResults();
        }
        const bool enteringReadback = update.readback && readback != update.readback;
        if (enteringReadback)
            readback = update.readback;
        bool nodesChanged = false, selectedChanged = false, parametersChanged = false;
        for (auto &item : update.nodes)
        {
            if (!nodeExists(item.node))
            {
                snapshots.erase(item.node);
                active.erase(item.node);
                lifecycleErrors.erase(item.node);
                continue;
            }
            if (item.active != active.contains(item.node))
            {
                nodesChanged = true;
                if (item.active)
                    active.insert(item.node);
                else
                    active.erase(item.node);
            }
            if (item.lifecycleDiagnostic)
            {
                const auto previous = lifecycleErrors.find(item.node);
                if (previous == lifecycleErrors.end() || previous->second != item.lifecycleDiagnostic)
                {
                    lifecycleErrors[item.node]  = item.lifecycleDiagnostic;
                    nodesChanged                = true;
                    selectedChanged |= selected == QString::number(item.node);
                    report(*item.lifecycleDiagnostic);
                }
            }
            else if (lifecycleErrors.erase(item.node))
                nodesChanged = true;
            if (item.instanceDiagnostic)
            {
                const auto previous = instanceErrors.find(item.node);
                if (previous == instanceErrors.end() || previous->second != item.instanceDiagnostic)
                {
                    instanceErrors[item.node] = item.instanceDiagnostic;
                    nodesChanged              = true;
                    report(*item.instanceDiagnostic);
                }
            }
            else if (instanceErrors.erase(item.node))
                nodesChanged = true;
            if (item.snapshot && item.snapshot->generation == generation)
            {
                const auto previous = snapshots.find(item.node);
                if (previous == snapshots.end() || previous->second != item.snapshot)
                {
                    snapshots[item.node] = std::move(item.snapshot);
                    nodesChanged         = true;
                    selectedChanged |= QString::number(item.node) == selected;
                    if (const auto *node = currentNode())
                        parametersChanged |= std::any_of(node->inputs.begin(), node->inputs.end(), [&](const auto &input)
                        {
                            return input.second.mode == BindingMode::Upstream && input.second.source
                                && input.second.source->node == item.node;
                        });
                }
            }
        }
        for (const auto &event : update.diagnostics) report(event);
        if (update.droppedDiagnostics > 0)
            report(QStringLiteral("诊断已截断，丢失 %1 条").arg(update.droppedDiagnostics));
        QString updatedTiming;
        if (update.roundMilliseconds > 0)
        {
            updatedTiming = QStringLiteral("本轮 %1 ms").arg(update.roundMilliseconds, 0, 'f', 2);
            for (const auto &item : update.flows)
                updatedTiming += QStringLiteral(" · %1 %2 ms").arg(workbench::q(item.flow)).arg(item.milliseconds, 0, 'f', 2);
        }
        if (timing != updatedTiming)
        {
            timing = updatedTiming;
            emit q->runtimeChanged();
        }
        setState(update.state);
        if (nodesChanged)
            publishNodesChanged();
        if (parametersChanged || enteringReadback)
        {
            if (roi)
                updateDisplay();
            emit q->parametersChanged();
        }
        if (selectedChanged || enteringReadback)
            updateResults();
        if (enteringReadback)
            q->parameters();
        if (update.completion && runtime->acknowledgeCompletion(*update.completion))
        {
            editor.setBusy(false);
            updateReadback();
            if (refreshResult)
                refreshFinished();
            updateResults();
            emit q->parametersChanged();
            setState(runtime->state());
            advancePending();
        }
    }
    updateReadback();
}

void AppController::Impl::start(RunMode mode)
{
    if (!idle())
        return;
    finishRoi();
    q->parameters();
    drainReaders();
    std::optional<NodeId> target;
    if (mode == RunMode::Node)
    {
        const auto *node = currentNode();
        if (!node)
        {
            report(QStringLiteral("请先选择工具节点"));
            return;
        }
        target = node->id;
    }
    const auto result = runtime->start(editor.project(), registry, mode, target);
    if (!result)
    {
        report(result);
        return;
    }
    active.clear();
    setState(mode == RunMode::Once   ? RuntimeState::RunningOnce
             : mode == RunMode::Loop ? RuntimeState::RunningLoop
                                     : RuntimeState::RunningNode);
}

void AppController::Impl::request(Pending action, QString path)
{
    if (action == Pending::Close && closing)
        return;
    if (pending != Pending::None)
    {
        report(QStringLiteral("请先完成或取消当前文件操作"));
        return;
    }
    finishRoi();
    pending          = action;
    pendingPath      = std::move(path);
    pendingApproved  = action == Pending::Reset;
    awaitingDecision = false;
    pendingReset     = false;
    replacement.reset();
    if (state != RuntimeState::Idle)
    {
        runtime->requestStop();
        if (state == RuntimeState::RunningOnce || state == RuntimeState::RunningLoop
            || state == RuntimeState::RunningNode)
            setState(RuntimeState::Stopping);
    }
    else
        advancePending();
}

void AppController::Impl::advancePending()
{
    if (pending == Pending::None || state != RuntimeState::Idle)
        return;
    if (pending == Pending::DeleteNodes)
    {
        auto deletion = std::move(*pendingDeletion);
        pendingDeletion.reset();
        pending = Pending::None;
        command([&] { return editor.removeNodes(deletion.flow, deletion.nodes); }, true);
        return;
    }
    if (!pendingApproved && editor.project().dirty)
    {
        if (!awaitingDecision)
        {
            awaitingDecision = true;
            emit q->unsavedDecisionRequired();
        }
        return;
    }
    pendingApproved = true;
    if (pending != Pending::Close && !pendingReset)
    {
        try
        {
            if (pending == Pending::Open)
            {
                auto loaded = ProjectStorage(registry).load(workbench::localPath(pendingPath));
                storageDiagnostics(loaded.diagnostics);
                replacement = std::move(loaded.project);
            }
            else if (pending == Pending::New)
                replacement = ProjectDefinition{};
        }
        catch (const std::exception &error)
        {
            report(error, QStringLiteral("工程打开失败"));
            pending = Pending::None;
            return;
        }
        drainReaders();
        const auto result = runtime->requestReset();
        if (!result)
        {
            report(result);
            pending = Pending::None;
            return;
        }
        pendingReset = true;
        setState(RuntimeState::Resetting);
        return;
    }
    const auto completed = pending;
    pending              = Pending::None;
    pendingPath.clear();
    awaitingDecision = false;
    pendingApproved  = false;
    pendingReset     = false;
    if (replacement)
    {
        editor.replaceProject(std::move(*replacement));
        ++projectEpoch;
        replacement.reset();
        flow = editor.project().flows.empty() ? QString{} : workbench::q(editor.project().flows.front().id);
        selected.clear();
        clipboard.reset();
        emit q->pasteChanged();
        search.clear();
        clearResults();
        changed(true);
        emit q->selectionChanged();
        emit q->projectReplaced();
    }
    if (completed == Pending::Close)
    {
        closing = true;
        emit q->closeReady();
    }
    else if (completed == Pending::Reset)
        synchronizeNodes();
}

void AppController::Impl::refresh(bool rescan)
{
    if (state != RuntimeState::Idle)
        return;
    finishRoi();
    if (rescan)
        loadToolsPackage();
    auto        result        = std::make_shared<RefreshResult>();
    auto        definitions   = registry.definitions();
    auto       *pythonRuntime = python.get();
    const auto *catalog       = &registry;
    drainReaders();
    const auto  accepted      = runtime->requestRefresh(
        [result, definitions = std::move(definitions), pythonRuntime, catalog]
        {
            catalog->loadNativeImplementations();
            if (pythonRuntime)
            {
                result->implementations = pythonRuntime->refresh(definitions);
                for (const auto &tool : definitions)
                    if (tool.kind == ToolKind::PythonFunction || tool.kind == ToolKind::PythonClass)
                    {
                        const auto status = std::find_if(result->implementations.begin(), result->implementations.end(),
                                                               [&](const auto &item) { return item.toolId == tool.id; });
                        if (status != result->implementations.end() && status->status)
                            result->factories.emplace_back(tool.id, pythonRuntime->factory(tool));
                    }
            }
            else
                for (const auto &tool : definitions)
                    if (tool.kind == ToolKind::PythonFunction || tool.kind == ToolKind::PythonClass)
                        result->implementations.push_back({
                            tool.id,
                            {-3, "Python interpreter unavailable"}
                        });
            return ToolStatus{};
        });
    if (!accepted)
    {
        refreshReconcile = false;
        tools.setRegistry(registry);
        changed(true);
        emit q->catalogChanged();
        report(accepted);
        return;
    }
    refreshResult    = std::move(result);
    refreshReconcile = refreshReconcile || rescan;
    setState(RuntimeState::Refreshing);
}

void AppController::Impl::refreshFinished()
{
    auto result = std::move(refreshResult);
    for (const auto &definition : registry.definitions())
        if (definition.kind == ToolKind::Cpp)
        {
            const auto availability = registry.availability(definition.id);
            if (availability.state != ToolAvailability::Available)
                report(availability.status);
        }
    for (auto &implementation : result->implementations)
    {
        ToolFactory factory;
        auto        found = std::find_if(result->factories.begin(), result->factories.end(),
                                         [&](const auto &item) { return item.first == implementation.toolId; });
        if (found != result->factories.end())
            factory = std::move(found->second);
        registry.setPythonImplementation(implementation.toolId, std::move(factory), implementation.status);
        if (!implementation.status)
            report(implementation.status);
    }
    if (refreshReconcile)
    {
        try
        {
            auto reconciled = ProjectStorage(registry).reconcile(editor.project());
            storageDiagnostics(reconciled.diagnostics);
            editor.replaceProject(std::move(reconciled.project));
        }
        catch (const std::exception &error)
        {
            report(error, QStringLiteral("工程工具定义同步失败"));
        }
    }
    refreshReconcile = false;
    tools.setRegistry(registry);
    changed(true);
    emit q->catalogChanged();
}

// Reads the installed package and replaces the registered tool set; an absent or failing package yields an empty registered tool set.
void AppController::Impl::loadToolsPackage()
{
    const auto candidate = toolsPackageDirectory(applicationDirectory);
    if (candidate.empty())
    {
        toolsPackageRoot.clear();
        registry.registerPackage({});
        return;
    }
    try
    {
        registry.registerPackage(readToolsPackage(candidate));
        toolsPackageRoot = candidate;
    }
    catch (const std::exception &error)
    {
        if (toolsPackageRoot.empty())
        {
            registry.registerPackage({});
        }
        report(error, QStringLiteral("工具目录读取失败：") + workbench::pathText(candidate));
    }
}

void AppController::Impl::rescanTools()
{
    if (state != RuntimeState::Idle)
        return;
    finishRoi();

    loadToolsPackage();
    refreshReconcile = true;
    refresh(false);
}

AppController::AppController(std::size_t workers, const QString &applicationDirectory, QObject *parent)
    : QObject(parent)
    , impl_(std::make_unique<Impl>(this, workers, applicationDirectory))
{
    for (const auto signal :
         {&AppController::nodesChanged, &AppController::projectChanged, &AppController::selectionChanged,
          &AppController::runtimeChanged, &AppController::diagnosticChanged})
        connect(this, signal, this, [this] { impl_->nodeRows.reset(); });
}

AppController::~AppController() = default;

QString AppController::title() const
{
    return q(impl_->editor.project().name) + (dirty() ? " *" : "") + " — VisionRT "
        + q(GetVersionString());
}

bool AppController::busy() const
{
    return impl_->state != RuntimeState::Idle;
}

QString AppController::runState() const
{
    return stateName(impl_->state);
}

QString AppController::currentFlow() const
{
    return impl_->flow;
}

QString AppController::selectedNode() const
{
    return impl_->selected;
}

QString AppController::timingText() const
{
    return impl_->timing;
}

bool AppController::dirty() const
{
    return impl_->editor.project().dirty;
}

bool AppController::canPaste() const
{
    return impl_->clipboard.has_value();
}

QString AppController::aboutText() const
{
    return QStringLiteral("VisionRT %1\nCopyright © VisionRT contributors\nQt %2\nPython: %3")
        .arg(q(GetVersionString()), QString::fromLatin1(qVersion()),
             impl_->python ? QStringLiteral("解释器可用") : QStringLiteral("不可用"));
}

QAbstractItemModel *AppController::toolModel() const
{
    return &impl_->tools;
}

QAbstractItemModel *AppController::outputModel() const
{
    return &impl_->outputs;
}

PythonRuntime *AppController::pythonRuntime() const
{
    return impl_ ? impl_->python.get() : nullptr;
}

QVariantList AppController::flows() const
{
    QVariantList result;
    for (const auto &flow : impl_->editor.project().flows)
        result.push_back(QVariantMap{
            {      "id",       q(flow.id)},
            {    "name",     q(flow.name)},
            {"selected", flow.runSelected}
        });
    return result;
}

QVariantList AppController::parameterTabs() const
{
    QVariantList result;
    for (const auto tab : toolTabs()) result.push_back(QString::fromUtf8(tab.data(), static_cast<qsizetype>(tab.size())));
    return result;
}

QVariantList AppController::nodes() const
{
    if (impl_->nodeRows)
        return *impl_->nodeRows;
    QVariantList result;
    if (const auto *flow = impl_->currentFlow())
        for (const auto &node : flow->nodes)
        {
            const bool running = impl_->active.contains(node.id);
            const auto snapshot = impl_->displaySnapshot(node);
            QString state = "idle";
            if (running)
                state = "running";
            else if (snapshot)
                state = snapshot->status.code == 0 ? "succeeded"
                      : snapshot->status.code == kCancelledStatus ? "cancelled" : "failed";
            std::int64_t statusCode = 0;
            QString      statusMessage;
            QString      statusDetail;
            if (snapshot && snapshot->status.code != 0)
            {
                statusCode    = snapshot->status.code;
                statusMessage
                    = diagnosticMessage({snapshot->node, snapshot->toolId, snapshot->phase, snapshot->status});
                statusDetail = q(snapshot->status.detail);
            }
            const auto timing = nodeTimingText(snapshot.get(), running);
            if (impl_->instanceErrors.contains(node.id))
            {
                state         = "failed";
                const auto &diagnostic = *impl_->instanceErrors.at(node.id);
                statusCode             = diagnostic.status.code;
                statusMessage          = diagnosticMessage(diagnostic);
                statusDetail           = q(diagnostic.status.detail);
            }
            QString lifecycleMessage;
            QString lifecycleDetail;
            if (impl_->lifecycleErrors.contains(node.id))
            {
                const auto &diagnostic = *impl_->lifecycleErrors.at(node.id);
                lifecycleMessage       = diagnosticMessage(diagnostic);
                lifecycleDetail        = q(diagnostic.status.detail);
            }
            const auto   *def             = impl_->registry.find(node.toolId);
            const QString version         = def ? q(def->version) : QStringLiteral("1.0");
            const QString category        = def ? q(def->category) : QString();
            const auto    avail           = impl_->registry.availability(node.toolId);
            const bool    defMissing      = (avail.state == ToolAvailability::DefinitionMissing);
            const bool    implUnavailable
                = !defMissing
               && (avail.state != ToolAvailability::Available || impl_->instanceErrors.contains(node.id));
            const bool    missing         = defMissing || implUnavailable;
            result.push_back(QVariantMap{
                {                   "id", QString::number(node.id)},
                {                "title",          q(node.title())},
                {                "alias",            q(node.alias)},
                {               "toolId",           q(node.toolId)},
                {              "version",                  version},
                {             "category",                 category},
                {                    "x",          node.position.x},
                {                    "y",          node.position.y},
                {                "state",                    state},
                {           "timingText",             timing.total},
                {   "timingOperatorText",          timing.toolCall},
                { "timingPreprocessText",        timing.preprocess},
                {"timingPostprocessText",       timing.postprocess},
                {              "missing",                  missing},
                {           "defMissing",               defMissing},
                {      "implUnavailable",          implUnavailable},
                {           "statusCode",               statusCode},
                {        "statusMessage",            statusMessage},
                {         "statusDetail",             statusDetail},
                {     "lifecycleMessage",         lifecycleMessage},
                {      "lifecycleDetail",          lifecycleDetail}
            });
        }
    impl_->nodeRows = result;
    return result;
}

QVariantList AppController::wires() const
{
    QVariantList result;
    if (const auto *flow = impl_->currentFlow())
    {
        std::map<std::pair<NodeId, NodeId>, QVariantList> groups;
        for (const auto &relation : portRelations(*flow).active)
            groups[{relation.source.node, relation.target.node}].push_back(QVariantMap{
                {"output", q(relation.source.port)},
                {"input", q(relation.target.port)}
            });
        for (const auto &[edge, ports] : groups)
            result.push_back(QVariantMap{
                {"source",  QString::number(edge.first)},
                {"target", QString::number(edge.second)},
                { "count", static_cast<int>(ports.size())},
                { "ports",                         ports}
            });
    }
    return result;
}

void AppController::selectFlow(const QString &id)
{
    if (id == impl_->flow)
        return;
    for (const auto &flow : impl_->editor.project().flows)
        if (q(flow.id) == id)
        {
            impl_->finishRoi();
            impl_->flow = id;
            impl_->selected.clear();
            impl_->nodeRows.reset();
            emit selectionChanged();
            emit nodesChanged();
            emit parametersChanged();
            impl_->updateResults();
            return;
        }
}

void AppController::selectNode(const QString &id)
{
    if (id == impl_->selected)
        return;
    if (!id.isEmpty())
    {
        const auto *flow = impl_->currentFlow();
        if (!flow
            || std::none_of(flow->nodes.begin(), flow->nodes.end(),
                            [&](const auto &node) { return QString::number(node.id) == id; }))
            return;
    }
    impl_->finishRoi();
    impl_->selected = id;
    emit selectionChanged();
    emit parametersChanged();
    impl_->updateResults();
}

void AppController::filterTools(const QString &text)
{
    impl_->tools.setFilter(text);
}

void AppController::searchNodes(const QString &text)
{
    impl_->search = text.trimmed();
    emit searchChanged();
}

QVariantList AppController::searchResults() const
{
    QVariantList result;
    if (impl_->search.isEmpty())
        return result;
    for (const auto &flow : impl_->editor.project().flows)
        for (const auto &node : flow.nodes)
            if (q(node.title()).contains(impl_->search, Qt::CaseInsensitive))
                result.push_back(QVariantMap{
                    {    "flow",               q(flow.id)},
                    {"flowName",             q(flow.name)},
                    {    "node", QString::number(node.id)},
                    {   "title",          q(node.title())}
                });
    return result;
}

void AppController::locateNode(const QString &flow, const QString &node)
{
    selectFlow(flow);
    selectNode(node);
    if (impl_->selected == node)
        emit nodeLocated(node);
}

void AppController::addNode(const QString &toolId, int x, int y)
{
    NodeId added = 0;
    if (impl_->command(
            [&]
            {
                const auto availability = impl_->registry.availability(toolId.toStdString());
                if (availability.state != ToolAvailability::Available)
                    throw RegistryError(availability.state, availability.status);
                const auto *tool = impl_->registry.find(toolId.toStdString());
                if (!tool)
                    throw ContractError("工具定义缺失：" + toolId.toStdString());
                added = impl_->editor.addNode(impl_->flow.toStdString(), *tool, {x, y});
                return true;
            },
            true))
        selectNode(QString::number(added));
}

bool AppController::moveNodes(const QVariantList &values, int dx, int dy)
{
    return impl_->command([&] { return impl_->editor.moveNodes(impl_->flow.toStdString(), ids(values), {dx, dy}); },
                          true);
}

void AppController::deleteNodes(const QVariantList &values)
{
    if (impl_->pending != Impl::Pending::None)
    {
        impl_->report(QStringLiteral("请先完成或取消当前操作"));
        return;
    }
    try
    {
        impl_->pendingDeletion = Impl::PendingDeletion{impl_->flow.toStdString(), ids(values)};
        impl_->request(Impl::Pending::DeleteNodes);
    }
    catch (const std::exception &error)
    {
        impl_->report(error, QStringLiteral("节点删除失败"));
    }
}

void AppController::copyNodes(const QVariantList &values)
{
    try
    {
        const auto nodes = ids(values);
        if (nodes.empty())
            return;
        impl_->clipboard = impl_->editor.copySelection(impl_->flow.toStdString(), nodes);
        emit pasteChanged();
    }
    catch (const std::exception &error)
    {
        impl_->report(error, QStringLiteral("节点复制失败"));
    }
}

void AppController::pasteNodes(int x, int y)
{
    impl_->command(
        [&]
        {
            if (!impl_->clipboard)
                return false;
            for (const auto &node : impl_->clipboard->nodes)
            {
                const auto availability = impl_->registry.availability(node.toolId);
                if (availability.state != ToolAvailability::Available)
                    throw RegistryError(availability.state, availability.status);
            }
            impl_->editor.pasteSelection(impl_->flow.toStdString(), *impl_->clipboard, {x, y});
            return true;
        },
        true);
}

void AppController::renameNode(const QString &id, const QString &alias)
{
    impl_->command([&] { return impl_->editor.setAlias(impl_->flow.toStdString(), nodeId(id), alias.toStdString()); });
}

void AppController::addFlow(const QString &name)
{
    std::string id;
    if (impl_->command(
            [&]
            {
                id = impl_->editor.addFlow(name.toStdString());
                return true;
            },
            true))
        selectFlow(q(id));
}

void AppController::renameFlow(const QString &id, const QString &name)
{
    impl_->command([&] { return impl_->editor.renameFlow(id.toStdString(), name.toStdString()); });
}

void AppController::deleteFlow(const QString &id)
{
    std::string adjacent;
    if (impl_->command(
            [&]
            {
                impl_->finishRoi();
                adjacent = impl_->editor.removeFlow(id.toStdString());
                return true;
            },
            true)
        && impl_->flow == id)
        selectFlow(q(adjacent));
}

void AppController::copyFlow(const QString &id)
{
    std::string copied;
    if (impl_->command(
            [&]
            {
                copied = impl_->editor.copyFlow(id.toStdString());
                return true;
            },
            true))
        selectFlow(q(copied));
}

void AppController::setFlowSelected(const QString &id, bool selected)
{
    impl_->command([&] { return impl_->editor.setRunSelected(id.toStdString(), selected); });
}


bool AppController::setParameterMode(const QString &id, bool upstream)
{
    return impl_->command(
        [&]
        {
            const auto *node = impl_->currentNode();
            if (!node)
                throw ContractError("未选择节点");
            const auto *tool = impl_->currentTool();
            if (tool)
            {
                if (const auto *port = workbench::port(tool->inputs, id.toStdString()))
                {
                    if (port->type.type == Type::Image && !upstream)
                        return false;
                }
            }
            impl_->finishRoi();
            return impl_->editor.setMode(impl_->flow.toStdString(), node->id, id.toStdString(),
                                         upstream ? BindingMode::Upstream : BindingMode::Literal);
        });
}


void AppController::runOnce()
{
    impl_->start(RunMode::Once);
}

void AppController::runNode()
{
    impl_->start(RunMode::Node);
}

void AppController::toggleLoop()
{
    if (impl_->state == RuntimeState::RunningLoop)
    {
        impl_->runtime->requestStop();
        impl_->setState(RuntimeState::Stopping);
    }
    else
        impl_->start(RunMode::Loop);
}

void AppController::resetRuntime()
{
    impl_->request(Impl::Pending::Reset);
}

void AppController::refreshPython()
{
    if (impl_->idle())
        impl_->refresh(true);
}

void AppController::rescanTools()
{
    if (impl_->idle())
        impl_->rescanTools();
}

void AppController::requestNew()
{
    impl_->request(Impl::Pending::New);
}

void AppController::requestOpen(const QString &path)
{
    if (!path.isEmpty())
        impl_->request(Impl::Pending::Open, path);
}

void AppController::requestClose()
{
    impl_->request(Impl::Pending::Close);
}

void AppController::resolveUnsaved(const QString &decision, const QString &savePath)
{
    if (impl_->pending == Impl::Pending::None || !impl_->awaitingDecision)
        return;
    if (decision == "cancel")
    {
        impl_->pending          = Impl::Pending::None;
        impl_->awaitingDecision = false;
        impl_->replacement.reset();
        return;
    }
    if (decision == "save")
    {
        if (!save(savePath))
            return;
    }
    else if (decision != "discard")
        return;
    impl_->pendingApproved  = true;
    impl_->awaitingDecision = false;
    impl_->advancePending();
}

bool AppController::save(const QString &path)
{
    auto destination = path.isEmpty() ? impl_->editor.project().path : workbench::localPath(path);
    if (destination.empty())
    {
        emit savePathRequired();
        return false;
    }
    if (busy() && destination != impl_->editor.project().path)
    {
        impl_->report(QStringLiteral("运行期间不能另存为；可保存当前已命名工程"));
        return false;
    }
    try
    {
        if (!impl_->readsAllowed())
        {
            impl_->readRequests.push_back(Impl::SaveRequest{workbench::pathText(destination), impl_->projectEpoch});
            impl_->report(QStringLiteral("工程保存已排队，等待本轮读取阶段"));
            return false;
        }
        auto candidate   = impl_->editor.project();
        auto diagnostics = ProjectStorage(impl_->registry).save(candidate, destination);
        if (busy())
            impl_->editor.markSaved();
        else
            impl_->editor.replaceProject(std::move(candidate));
        impl_->synchronizeNodes();
        impl_->storageDiagnostics(diagnostics);
        emit projectChanged();
        return true;
    }
    catch (const std::exception &error)
    {
        impl_->report(error, QStringLiteral("工程保存失败"));
        return false;
    }
}

} // namespace visionrt
