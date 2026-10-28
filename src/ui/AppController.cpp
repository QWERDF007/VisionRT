#include "common/Version.h"
#include "log/LogService.h"
#include "core/NodeContracts.h"
#include "core/Sequence.h"
#include "runtime/Receiver.h"
#include "runtime/SelectTool.h"
#include "ui/AppController_p.h"
#include "ui/DurationText.h"
#include "ui/Diagnostics.h"

#include <core/ToolPackage.h>

#include <QClipboard>
#include <QCoreApplication>
#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QGuiApplication>
#include <QJsonDocument>
#include <QJsonObject>
#include <QUrl>
#include <algorithm>
#include <cmath>
#include <limits>
#include <iostream>

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
    // Read once, synchronously, while the window is still being built: a corrupt or slow
    // preferences file must not stand between the user and the first project.
    recentProjects = RecentProjects::load();

    pluginLoader = std::make_shared<PluginLoader>();
    registry.setPluginLoader(pluginLoader);
    registerReceiveTools(registry);
    registerSelectTools(registry);
    loadComponentBuildInfo();
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
    runtime->setReadbackCallback(
        [owner](RuntimeReadback readback)
        {
            QMetaObject::invokeMethod(
                owner,
                [owner, readback]
                {
                    if (owner->impl_ && readback.generation >= owner->impl_->generation)
                        owner->impl_->poll();
                },
                Qt::QueuedConnection);
        });
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
    // The display worker released its batch: management continuations waiting on the drain may
    // advance without ever blocking the GUI thread.
    QObject::connect(&converter, &DisplayConverter::readersIdle, owner,
                     [this]
                     {
                         if (draining)
                             finishDrain();
                     });
    assist = std::make_unique<AssistPreview>();
    QObject::connect(assist.get(), &AssistPreview::outcomeReady, owner,
                     [this](const AssistOutcome &outcome)
                     {
                         if (outcome.learn)
                         {
                             applyAssistOutcome(outcome);
                             return;
                         }
                         if (!assistContextCurrent(outcome.context))
                         {
                             clearAssistPreview();
                             return;
                         }
                         if (!outcome.valid)
                         {
                             clearAssistPreview();
                             setAssistHint(outcome.quality.isEmpty()
                                               ? QStringLiteral("未找到有效目标")
                                               : QStringLiteral("未找到有效目标：") + outcome.quality);
                             return;
                         }
                         assistPreviewRois    = outcome.preview;
                         assistPreviewContext = outcome.context;
                         if (viewport)
                             viewport->setAssistPreview(assistPreviewRois);
                         emit q->assistChanged();
                     });
    QObject::connect(assist.get(), &AssistPreview::busyChanged, owner,
                     [this]
                     {
                         if (draining)
                             finishDrain();
                     });
    // Release completion is part of the drain condition: management never joins the worker.
    QObject::connect(assist.get(), &AssistPreview::sessionReleased, owner,
                     [this]
                     {
                         if (draining)
                             finishDrain();
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
    // The helper owns images and may still be executing; release it before any runtime state goes.
    if (assist)
        assist->shutdown();
    runtime->setCompletionCallback({});
    runtime->setReadbackCallback({});
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

const ToolDefinition *AppController::Impl::effectiveTool() const
{
    const auto *node = currentNode();
    if (!node)
        return nullptr;
    const auto *source = registry.find(node->toolId);
    if (!source)
        return nullptr;
    if (contractCache.contract && contractCache.source == source && contractCache.node == node->id
        && contractCache.executionRevision == node->executionRevision && contractCache.epoch == projectEpoch)
        return contractCache.contract.get();
    contractCache.source            = source;
    contractCache.node              = node->id;
    contractCache.executionRevision = node->executionRevision;
    contractCache.epoch             = projectEpoch;
    try
    {
        contractCache.contract = std::make_shared<const ToolDefinition>(nodeContract(*node, *source));
    }
    catch (const ContractError &)
    {
        // An incomplete node configuration still has declared ports to show; the raw declaration
        // keeps the unresolved rows visible instead of presenting a half-materialised contract.
        contractCache.contract = std::make_shared<const ToolDefinition>(*source);
    }
    return contractCache.contract.get();
}

void AppController::Impl::refreshConnectable(const FlowDefinition &flow, const NodeDefinition &node) const
{
    if (connectable.valid && connectable.node == node.id && connectable.epoch == projectEpoch
        && connectable.structureRevision == flow.structureRevision && connectable.flow.toStdString() == flow.id)
        return;
    connectable.epoch             = projectEpoch;
    connectable.flow              = workbench::q(flow.id);
    connectable.structureRevision = flow.structureRevision;
    connectable.node              = node.id;
    connectable.valid             = true;
    connectable.connectable.clear();
    connectable.candidates.clear();
    for (const auto &[id, type] : node.signatures.outputs)
    {
        auto candidates = candidateInputsWithState(flow, SourcePort{node.id, id});
        bool free       = false;
        for (const auto &candidate : candidates)
            if (!candidate.connected && !candidate.occupied)
            {
                free = true;
                break;
            }
        connectable.connectable.emplace(id, free);
        connectable.candidates.emplace(id, std::move(candidates));
    }
}

const PortDefinition *AppController::Impl::parameter(const std::string &id, bool initialization) const
{
    const auto *tool = effectiveTool();
    return tool ? workbench::port(initialization ? tool->initParams : tool->inputs, id) : nullptr;
}

bool AppController::Impl::idle()
{
    if (isClosing())
        return false;
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
    refreshAssist();
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
    if (isClosing())
        return;
    if (runtime->state() != RuntimeState::Idle)
        return;
    beginDrain([this] { requestSynchronize(); });
}

void AppController::Impl::requestSynchronize()
{
    if (isClosing())
        return;
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
    flowLatestMs.clear();
    latestRoundMs.reset();
    latestElapsedMs = 0;
    timing.clear();
    finishRoi();
    converter.invalidate(generation);
    updateResults();
    publishNodesChanged();
    emit q->runtimeChanged();
    emit q->timingChanged();
}

void AppController::Impl::poll()
{
    if (closePhase == ClosePhase::AwaitOwners)
    {
        if ((!python || python->isCloseReady()) && (!assist || assist->finished()))
        {
            if (assist)
                assist->finalizeShutdown();
            closePhase = ClosePhase::Ready;
            closing    = false;
            pending    = Pending::None;
            emit q->closingChanged();
            if (!closeReadyEmitted)
            {
                closeReadyEmitted = true;
                emit q->closeReady();
            }
        }
        return;
    }
    if (closePhase == ClosePhase::Ready)
        return;
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
                    if (const auto *node = findNode(item.node))
                    {
                        const auto avail = registry.availability(node->toolId);
                        tools.updateToolStatus(QString::fromStdString(node->toolId),
                                               avail.state == ToolAvailability::Available,
                                               QString::fromStdString(avail.status.message));
                    }
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
                    if (const auto *node = findNode(item.node))
                    {
                        const auto avail = registry.availability(node->toolId);
                        tools.updateToolStatus(QString::fromStdString(node->toolId),
                                               avail.state == ToolAvailability::Available,
                                               QString::fromStdString(avail.status.message));
                    }
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
        // Bottom statistics belong to complete runs only. A single-node session updates its own
        // node timing and outputs but never resets, clears or accumulates these three values.
        bool timingChanged = false;
        const bool completeRunTiming
            = update.mode != RunMode::Node
           && (fullRunSession == 0 || update.session == 0 || update.session == fullRunSession);
        if (completeRunTiming && update.session != 0)
            fullRunSession = update.session;
        if (completeRunTiming)
        {
            for (const auto &item : update.flows)
            {
                auto it = flowLatestMs.find(item.flow);
                if (it == flowLatestMs.end() || it->second != item.milliseconds)
                {
                    flowLatestMs[item.flow] = item.milliseconds;
                    timingChanged = true;
                }
            }
            if (update.roundMilliseconds.has_value() && latestRoundMs != update.roundMilliseconds)
            {
                latestRoundMs = update.roundMilliseconds;
                timingChanged = true;
            }
            if (update.session > 0 && latestElapsedMs != update.sessionElapsedMilliseconds)
            {
                latestElapsedMs = update.sessionElapsedMilliseconds;
                timingChanged = true;
            }
        }
        if (timingChanged)
        {
            QString updatedTiming = q->timingText();
            if (timing != updatedTiming)
            {
                timing = updatedTiming;
                emit q->timingChanged();
            }
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
            state = runtime->state();
            editor.setBusy(false);
            if (viewport)
                viewport->setReadOnly(state != RuntimeState::Idle || !roi);
            updateReadback();
            if (refreshResult)
                refreshFinished();
            updateResults();
            emit q->parametersChanged();
            emit q->runtimeChanged();
            flushDeferredSaves();
            advancePending();
        }
    }
    updateReadback();
}

void AppController::Impl::start(RunMode mode)
{
    // A run may supersede a pending synchronize: it waits for the same drain and becomes the
    // continuation, so the newest management intent is the one that executes.
    if (isClosing() || state != RuntimeState::Idle || pending != Pending::None)
        return;
    finishRoi();
    q->parameters();
    beginDrain([this, mode] { beginRun(mode); });
}

void AppController::Impl::beginRun(RunMode mode)
{
    if (isClosing())
        return;
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
    runtime->setReadbackEnabled(true);
    // A read-safe window belongs to the session that requested it; drop the previous one before
    // the new session can publish its own.
    readback.reset();
    const auto result = runtime->start(editor.project(), registry, mode, target);
    if (!result)
    {
        report(result);
        return;
    }
    LogService::write(LogLevel::Info, QStringLiteral("运行"),
                      mode == RunMode::Once ? QStringLiteral("开始单次运行")
                      : mode == RunMode::Loop ? QStringLiteral("开始循环运行")
                                              : QStringLiteral("开始单节点运行"));
    if (mode != RunMode::Node)
    {
        // A successfully accepted complete run starts a new statistics session immediately.
        fullRunSession = 0;
        flowLatestMs.clear();
        latestRoundMs.reset();
        latestElapsedMs = 0.0;
        const QString cleared = q->timingText();
        if (timing != cleared)
        {
            timing = cleared;
            emit q->timingChanged();
        }
    }
    active.clear();
    setState(mode == RunMode::Once   ? RuntimeState::RunningOnce
             : mode == RunMode::Loop ? RuntimeState::RunningLoop
                                     : RuntimeState::RunningNode);
}

void AppController::Impl::request(Pending action, QString path)
{
    if (action == Pending::Close)
    {
        if (closePhase == ClosePhase::Ready || closeReadyEmitted)
            return;
        if (closePhase == ClosePhase::AwaitOwners)
        {
            if ((!python || python->isCloseReady()) && (!assist || assist->finished()))
            {
                if (assist)
                    assist->finalizeShutdown();
                closePhase = ClosePhase::Ready;
                closing    = false;
                pending    = Pending::None;
                emit q->closingChanged();
                if (!closeReadyEmitted)
                {
                    closeReadyEmitted = true;
                    emit q->closeReady();
                }
            }
            else if (python && !python->isCloseReady())
            {
                const auto remaining = python->activeReferences();
                report(QStringLiteral("等待外部资源释放后关闭 (剩余引用数: %1)").arg(remaining));
            }
            else
                report(QStringLiteral("等待智能点选释放后关闭"));
            return;
        }
        if (closePhase == ClosePhase::Resetting || pending == Pending::Close)
            return;
    }
    if (isClosing())
    {
        report(QStringLiteral("正在关闭中，不能执行新操作"));
        return;
    }
    if (pending != Pending::None)
    {
        report(QStringLiteral("请先完成或取消当前文件操作"));
        return;
    }
    finishRoi();
    if (action == Pending::Reset && assist)
        // Resetting tears the assist session down; the drain below waits for the async release.
        assist->releaseSession();
    pending          = action;
    pendingPath      = std::move(path);
    pendingApproved  = action == Pending::Reset;
    awaitingDecision = false;
    pendingReset     = false;
    pendingDrained   = false;
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
        if (command([&] { return editor.removeNodes(deletion.flow, deletion.nodes); }, true))
            LogService::write(LogLevel::Info, QStringLiteral("工程"),
                              QStringLiteral("删除 %1 个节点").arg(static_cast<qulonglong>(deletion.nodes.size())));
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
    if (!pendingReset)
    {
        if (pending == Pending::Close)
        {
            closing    = true;
            closePhase = ClosePhase::Resetting;
            emit q->closingChanged();
            if (!pendingDrained)
            {
                pendingDrained = true;
                beginDrain([this] { advancePending(); });
                return;
            }
            const auto result = runtime->requestReset();
            if (!result)
            {
                report(result);
                pending    = Pending::None;
                closing    = false;
                closePhase = ClosePhase::None;
                emit q->closingChanged();
                return;
            }
            pendingReset = true;
            setState(RuntimeState::Resetting);
            return;
        }
        try
        {
            if (!replacement && pending == Pending::Open)
            {
                auto loaded = ProjectStorage(registry).load(workbench::localPath(pendingPath));
                storageDiagnostics(loaded.diagnostics);
                replacement = std::move(loaded.project);
            }
            else if (!replacement && pending == Pending::New)
                replacement = ProjectDefinition{};
        }
        catch (const std::exception &error)
        {
            report(error, QStringLiteral("工程打开失败"));
            pending = Pending::None;
            return;
        }
        if (!pendingDrained)
        {
            pendingDrained = true;
            beginDrain([this] { advancePending(); });
            return;
        }
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
    const auto completed  = pending;
    const auto openedPath = pendingPath;
    pending               = Pending::None;
    pendingPath.clear();
    awaitingDecision = false;
    pendingApproved  = false;
    pendingReset     = false;
    bool replacementCommitted = false;
    if (replacement)
    {
        editor.replaceProject(std::move(*replacement));
        replacementCommitted = true;
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
    if (completed == Pending::Open && replacementCommitted)
    {
        recordRecent(openedPath);
        LogService::write(LogLevel::Info, QStringLiteral("工程"),
                          QStringLiteral("打开工程 %1").arg(workbench::pathText(workbench::localPath(openedPath))));
    }
    if (completed == Pending::Close)
    {
        closing = true;
        // Release the helper before the drain: the barrier stays closed until the worker
        // acknowledges, so the close never races a release that is still destroying the instance.
        if (assist)
            assist->releaseSession();
        beginDrain([this] { finishClose(); });
        return;
    }
    else if (completed == Pending::Reset)
        synchronizeNodes();
}

void AppController::Impl::recordRecent(const QString &path)
{
    auto updated = RecentProjects::record(path);
    if (updated == recentProjects)
        return;
    recentProjects = std::move(updated);
    emit q->recentProjectsChanged();
}

void AppController::Impl::finishClose()
{
    if (assist)
    {
        // Only the worker performs the release; the GUI waits for its exit through poll().
        assist->beginShutdown();
        assistToolId.clear();
    }
    clearAssistPreview();
    assistEnabled = false;
    converter.invalidate(generation);
    if (viewport)
    {
        viewport->clearImage(displayRequest);
        viewport->setOverlays({}, {});
        viewport->setAssistActive(false);
        viewport->endRoiEdit();
    }
    roi.reset();
    outputs.clear();
    snapshots.clear();
    displayInputs.clear();
    displayOutputs.clear();
    parameterRows.clear();
    parameterRowsNode = 0;
    clipboard.reset();
    replacement.reset();
    refreshResult.reset();
    clearResults();

    if (python)
        python->prepareShutdown();
    const bool assistReady = !assist || assist->finished();
    if (!assistReady || (python && !python->isCloseReady()))
    {
        closePhase      = ClosePhase::AwaitOwners;
        if (python && !python->isCloseReady())
            report(QStringLiteral("等待外部资源释放后关闭 (剩余引用数: %1)").arg(python->activeReferences()));
        else
            report(QStringLiteral("等待智能点选释放后关闭"));
        pending         = Pending::Close;
        pendingApproved = true;
        pendingReset    = true;
        return;
    }
    if (assist)
        assist->finalizeShutdown();
    closePhase = ClosePhase::Ready;
    closing = false;
    emit q->closingChanged();
    if (!closeReadyEmitted)
    {
        closeReadyEmitted = true;
        emit q->closeReady();
    }
}

void AppController::Impl::refresh(bool rescan)
{
    if (state != RuntimeState::Idle || isClosing() || pending == Pending::Close)
        return;
    finishRoi();
    if (rescan)
        loadToolsPackage();
    // The catalog may now expose different modules: drop the assist session so it is rebuilt from
    // the refreshed registry instead of serving from an outdated module.
    assistToolId.clear();
    if (assist)
        assist->invalidate();
    beginDrain([this, rescan] { requestRefresh(rescan); });
}

void AppController::Impl::requestRefresh(bool rescan)
{
    if (isClosing())
        return;
    auto        result        = std::make_shared<RefreshResult>();
    auto        definitions   = registry.definitions();
    auto       *pythonRuntime = python.get();
    const auto *catalog       = &registry;
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

namespace {
// 组件构建元数据由构建完成阶段生成。缺失、损坏或没有时间戳都视为“未知”，绝不回退到别的组件
// 时间——三个组件各自记录自己的构建完成时刻。
std::optional<ComponentBuildInfo> readBuildInfo(const std::filesystem::path &path)
{
    QFile infoFile(workbench::pathText(path));
    if (!infoFile.open(QIODevice::ReadOnly))
        return std::nullopt;
    QJsonParseError parseError;
    const auto      doc = QJsonDocument::fromJson(infoFile.readAll(), &parseError);
    if (parseError.error || !doc.isObject())
        return std::nullopt;
    const auto         obj = doc.object();
    ComponentBuildInfo info;
    info.version = obj.value(QStringLiteral("version")).toString().toStdString();
    info.commit  = obj.value(QStringLiteral("commit")).toString().toStdString();
    info.built_at_epoch = static_cast<std::int64_t>(obj.value(QStringLiteral("built_at_epoch")).toDouble(0));
    if (info.version.empty() && info.commit.empty() && info.built_at_epoch == 0)
        return std::nullopt;
    return info;
}

// 构建时刻按“当前机器的本地时区”显示并带上偏移：同一个包换到别的时区打开时，看到的是同一个
// 瞬间的当地写法，而不是某个固定时区的字符串。
QString builtAtText(const std::optional<ComponentBuildInfo> &info)
{
    if (!info || info->built_at_epoch <= 0)
        return QStringLiteral("未知");
    const auto time     = QDateTime::fromSecsSinceEpoch(info->built_at_epoch).toLocalTime();
    const auto offset   = time.offsetFromUtc();
    const auto sign     = offset < 0 ? QLatin1Char('-') : QLatin1Char('+');
    const auto magnitude = offset < 0 ? -offset : offset;
    return time.toString(QStringLiteral("yyyy-MM-dd HH:mm:ss")) + QLatin1Char(' ') + sign
         + QStringLiteral("%1:%2")
               .arg(magnitude / 3600, 2, 10, QLatin1Char('0'))
               .arg((magnitude % 3600) / 60, 2, 10, QLatin1Char('0'));
}
} // namespace

void AppController::Impl::loadComponentBuildInfo()
{
    hostBuildInfo      = readBuildInfo(applicationDirectory / "build_info.json");
    contractsBuildInfo = readBuildInfo(applicationDirectory / "contracts-build-info.json");
}

// Reads the installed package and replaces the registered tool set; an absent or failing package yields an empty registered tool set.
void AppController::Impl::loadToolsPackage()
{
    const auto candidate = toolsPackageDirectory(applicationDirectory);
    if (candidate.empty())
    {
        toolsPackageRoot.clear();
        toolsPackageInstalled = false;
        toolsPackageBuildInfo.reset();
        registry.registerPackage({});
        emit q->aboutTextChanged();
        return;
    }

    toolsPackageInstalled = true;
    toolsPackageBuildInfo.reset();

    toolsPackageBuildInfo = readBuildInfo(candidate / "build_info.json");

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
    emit q->aboutTextChanged();
}

void AppController::Impl::rescanTools()
{
    if (state != RuntimeState::Idle || isClosing())
        return;
    finishRoi();
    // A catalog reload changes the active tool set, so the persistent helper session must go too.
    if (assist)
        assist->releaseSession();

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
    return impl_->state != RuntimeState::Idle || impl_->draining;
}

bool AppController::runtimeBusy() const
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
    const auto *currentFlowDef = impl_->currentFlow();
    QString flowName = currentFlowDef ? workbench::q(currentFlowDef->name) : QStringLiteral("流程1");
    std::optional<double> currentFlowMs;
    if (currentFlowDef)
    {
        auto it = impl_->flowLatestMs.find(currentFlowDef->id);
        if (it != impl_->flowLatestMs.end())
            currentFlowMs = it->second;
    }
    return QStringLiteral("%1：%2 | 全流程：%3 | 已运行：%4")
        .arg(flowName)
        .arg(formatMs(currentFlowMs))
        .arg(formatMs(impl_->latestRoundMs))
        .arg(formatElapsed(impl_->latestElapsedMs));
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
    auto sanitize = [](const std::string &str) -> QString {
        const QString s = QString::fromStdString(str).trimmed();
        if (s.isEmpty() || s.compare(QStringLiteral("unknown"), Qt::CaseInsensitive) == 0 || s.contains(QStringLiteral("NOTFOUND")))
        {
            return QStringLiteral("未知");
        }
        return s;
    };

    QStringList lines;

    // 三个组件遵循同一条规则：版本与 Commit 来自各自编译期输入，构建时间来自构建完成阶段写入
    // 的组件元数据，缺失即“未知”。任何组件都不会借用别的组件的时间。
    lines << QStringLiteral("VisionRT");
    lines << QStringLiteral("版本          %1").arg(sanitize(GetVersionString()));
    lines << QStringLiteral("Commit        %1").arg(sanitize(GetCommitHashString()));
    lines << QStringLiteral("构建时间      %1").arg(builtAtText(impl_->hostBuildInfo));
    lines << QString();

    lines << QStringLiteral("VisionTools");
    if (!impl_->toolsPackageInstalled)
    {
        lines << QStringLiteral("状态          未安装");
    }
    else if (!impl_->toolsPackageBuildInfo.has_value())
    {
        lines << QStringLiteral("状态          构建信息不可用");
    }
    else
    {
        lines << QStringLiteral("版本          %1").arg(sanitize(impl_->toolsPackageBuildInfo->version));
        lines << QStringLiteral("Commit        %1").arg(sanitize(impl_->toolsPackageBuildInfo->commit));
        lines << QStringLiteral("构建时间      %1").arg(builtAtText(impl_->toolsPackageBuildInfo));
    }
    lines << QString();

    const auto contracts = GetContractsVersion();
    lines << QStringLiteral("VisionRTContracts");
    lines << QStringLiteral("版本          %1").arg(sanitize(contracts.version));
    lines << QStringLiteral("Commit        %1").arg(sanitize(contracts.commit));
    lines << QStringLiteral("构建时间      %1").arg(builtAtText(impl_->contractsBuildInfo));
    lines << QString();

    // Environment
    lines << QStringLiteral("运行环境");
    lines << QStringLiteral("Qt            %1").arg(QString::fromLatin1(qVersion()));
    lines << QStringLiteral("Python        %1").arg(impl_->python ? QStringLiteral("解释器可用") : QStringLiteral("不可用"));
    lines << QString();

    // Copyright
    lines << QStringLiteral("Copyright © VisionRT contributors");

    return lines.join(QLatin1Char('\n'));
}

void AppController::copyAboutText() const
{
    QGuiApplication::clipboard()->setText(aboutText());
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
            const auto disposition = snapshot ? snapshot->disposition : NodeDisposition::Pending;
            QString state = "idle";
            if (running)
                state = "running";
            else if (snapshot)
                switch (disposition)
                {
                case NodeDisposition::Skipped:
                    state = "skipped";
                    break;
                case NodeDisposition::Blocked:
                    state = "blocked";
                    break;
                case NodeDisposition::Canceled:
                    state = "cancelled";
                    break;
                case NodeDisposition::Failed:
                    state = "failed";
                    break;
                default:
                    state = snapshot->status.code == 0 ? "succeeded" : "failed";
                    break;
                }
            std::int64_t statusCode = 0;
            QString      statusMessage;
            QString      statusDetail;
            if (snapshot && (snapshot->status.code != 0 || disposition == NodeDisposition::Skipped
                             || disposition == NodeDisposition::Blocked))
            {
                statusCode = snapshot->status.code;
                // The gate reason stays separate from tool errors: no error code is invented.
                statusMessage = disposition == NodeDisposition::Skipped || disposition == NodeDisposition::Blocked
                                  ? (q(snapshot->status.message).isEmpty()
                                         ? QStringLiteral("节点未执行")
                                         : QStringLiteral("节点未执行：") + q(snapshot->status.message))
                                  : diagnosticMessage({snapshot->node, snapshot->toolId, snapshot->phase,
                                                       snapshot->status});
                statusDetail = statusDisplay(snapshot->status).detail;
            }
            const auto timing = nodeTimingText(snapshot.get(), running);
            QVariantList phases;
            for (const auto &phase : timingPhasesOf(timing))
                phases.push_back(QVariantMap{{"label", phase.label}, {"value", phase.value}});
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
            // A snapshot produced by an older configuration is still shown, but it must not claim
            // to describe the current parameters.
            const bool    stale           = snapshot && snapshot->executionRevision != node.executionRevision;
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
                {          "disposition",          dispositionName(disposition)},
                {             "executed", disposition == NodeDisposition::Succeeded
                                              || disposition == NodeDisposition::Failed
                                              || disposition == NodeDisposition::Canceled},
                {                "stale",                   stale},
                {           "timingText",             timing.total},
                {         "timingPhases",                  phases},
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

void AppController::Impl::requireAvailableTool(std::string_view toolId) const
{
    const auto availability = registry.availability(toolId);
    if (availability.state != ToolAvailability::Available)
        throw RegistryError(availability.state, availability.status);
}

void AppController::addNode(const QString &toolId, int x, int y)
{
    NodeId added = 0;
    if (impl_->command(
            [&]
            {
                impl_->requireAvailableTool(toolId.toStdString());
                const auto *tool = impl_->registry.find(toolId.toStdString());
                if (!tool)
                    throw ContractError("工具定义缺失：" + toolId.toStdString());
                added = impl_->editor.addNode(impl_->flow.toStdString(), *tool, {x, y});
                return true;
            },
            true))
    {
        LogService::write(LogLevel::Info, QStringLiteral("工程"),
                          QStringLiteral("添加节点 %1（节点 %2）").arg(toolId, QString::number(added)));
        selectNode(QString::number(added));
    }
}

bool AppController::moveNodes(const QVariantList &values, int dx, int dy)
{
    return impl_->command([&] { return impl_->editor.moveNodes(impl_->flow.toStdString(), ids(values), {dx, dy}); },
                          true);
}

void AppController::deleteNodes(const QVariantList &values)
{
    if (impl_->isClosing() || impl_->pending != Impl::Pending::None)
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
    if (impl_->isClosing())
        return;
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
                impl_->requireAvailableTool(node.toolId);
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
        LogService::write(LogLevel::Info, QStringLiteral("运行"), QStringLiteral("请求停止循环运行"));
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

void AppController::openRecentProject(const QString &path)
{
    // The list is only a shortcut: it enters through the same pending-open state machine as the
    // file dialog, so the unsaved decision, the drain and the reset cannot be bypassed.
    requestOpen(path);
}

QVariantList AppController::recentProjects() const
{
    return RecentProjects::toVariantList(impl_->recentProjects);
}

void AppController::requestClose()
{
    impl_->request(Impl::Pending::Close);
}

bool AppController::isCloseReady() const
{
    return impl_->closePhase == Impl::ClosePhase::Ready
        || (impl_->closePhase == Impl::ClosePhase::None && (!impl_->python || impl_->python->isCloseReady()));
}

bool AppController::isClosing() const
{
    return impl_->isClosing();
}

void AppController::resolveUnsaved(const QString &decision, const QString &savePath)
{
    if (impl_->pending == Impl::Pending::None || !impl_->awaitingDecision)
        return;
    if (decision == "cancel")
    {
        if (impl_->pending == Impl::Pending::Close)
        {
            impl_->closing = false;
            impl_->closePhase = Impl::ClosePhase::None;
            impl_->closeReadyEmitted = false;
            emit closingChanged();
        }
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
    if (impl_->isClosing())
        return false;
    const auto previousPath = impl_->editor.project().path;
    auto destination = path.isEmpty() ? previousPath : workbench::localPath(path);
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
        // The editor project is frozen while a run owns the runtime, so a mid-run save is queued
        // and written once the run finished instead of serialising a moving project.
        if (busy() || !impl_->readsAllowed())
        {
            impl_->readRequests.push_back(Impl::SaveRequest{workbench::pathText(destination), impl_->projectEpoch});
            impl_->report(QStringLiteral("工程保存已排队，等待运行结束后写入"));
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
        // A first save or a save-as puts the new file in the recent list; a plain Ctrl+S writes the
        // same file again and must leave the order alone.
        if (destination != previousPath)
            impl_->recordRecent(workbench::pathText(destination));
        LogService::write(LogLevel::Info, QStringLiteral("工程"),
                          QStringLiteral("保存工程 %1").arg(workbench::pathText(destination)));
        return true;
    }
    catch (const std::exception &error)
    {
        impl_->report(error, QStringLiteral("工程保存失败"));
        return false;
    }
}

} // namespace visionrt
