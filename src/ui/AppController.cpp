#include "common/Version.h"
#include "log/LogService.h"
#include "core/DefinitionJson.h"
#include "core/ExampleProject.h"
#include "core/NodeContracts.h"
#include "core/Sequence.h"
#include "core/ToolPaths.h"
#include "runtime/Receiver.h"
#include "runtime/SelectTool.h"
#include "ui/AppController_p.h"
#include "ui/AboutReport.h"
#include "ui/DurationText.h"
#include "ui/Diagnostics.h"
#include "ui/UiPreferences.h"

#include "python/PythonProbe.h"

#include <core/ToolPackage.h>

#include <QClipboard>
#include <QCoreApplication>
#include <QDateTime>
#include <QDeadlineTimer>
#include <QDir>
#include <QEventLoop>
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

const PortDefinition *port(const std::vector<PortDefinition> &ports, PortId id)
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

// Empty in every shipped run: a test states its interpreter before the first controller is built.
QString AppController::Impl::testPythonExecutable;

AppController::Impl::Impl(AppController *owner, std::size_t workers, const QString &directory)
    : q(owner)
    , applicationDirectory(workbench::localPath(directory))
    , tools(owner)
    , outputs(owner)
{
    // Read once, synchronously, while the window is still being built: a corrupt or slow
    // preferences file must not stand between the user and the first project.
    recentProjects = RecentProjects::load();
    // The same read feeds the three platform settings. A damaged file keeps its bytes and is
    // reported; this process still runs on the defaults so the window opens.
    QString settingsFailure;
    UiPreferences::loadSettings(settings, &settingsFailure);

    pluginLoader = std::make_shared<PluginLoader>();
    registry.setPluginLoader(pluginLoader);
    registerReceiveTools(registry);
    registerSelectTools(registry);
    loadComponentBuildInfo();
    loadToolsPackage();
    // The interpreter is created only for an environment the user selected and the check accepted.
    // Without one the backend stays unavailable and every Python tool reports that reason; opening
    // a project, the settings window and every native tool keep working. A test states the choice
    // itself, through the one seam that never exists in a shipped run.
    python = std::make_unique<PythonBackend>();
    pythonProbe = std::make_unique<PythonProbe>();
    QObject::connect(pythonProbe.get(), &PythonProbe::finished, owner, [this] { finishPythonCheck(); });
    const auto &interpreter = testPythonExecutable.isEmpty() ? settings.pythonExecutable : testPythonExecutable;
    if (!interpreter.isEmpty())
        beginPythonCheck(interpreter, PythonCheck::Startup);
    if (!settingsFailure.isEmpty())
    {
        settingsError = settingsFailure;
        report(settingsFailure);
    }
    runtime = std::make_unique<RuntimeEngine>(workers);
    presentation.setRuntime(runtime.get());
    presentation.setExternalBusy([this] { return assist && assist->busy(); });
    runtime->setReadbackEnabled(true);
    // The runtime calls these from its worker. They are posted through the timer object, which lives
    // exactly as long as this implementation: an answer posted while the controller is still being
    // built is delivered once its members exist, and one posted after teardown is dropped with the
    // object instead of reaching a pointer that no longer names anything.
    runtime->setReadbackCallback(
        [this](RuntimeReadback readback)
        {
            QMetaObject::invokeMethod(
                &timer,
                [this, readback]
                {
                    if (!tearingDown && readback.generation >= generation)
                        poll();
                },
                Qt::QueuedConnection);
        });
    runtime->setCompletionCallback(
        [this](RuntimeCompletion completion)
        {
            QMetaObject::invokeMethod(
                &timer,
                [this, completion]
                {
                    if (!tearingDown && completion.generation >= generation)
                        poll();
                },
                Qt::QueuedConnection);
        });
    runtime->setActivityCallback(
        [this]
        {
            QMetaObject::invokeMethod(
                &timer,
                [this]
                {
                    if (!tearingDown)
                        applyActivity();
                },
                Qt::QueuedConnection);
        });
    QObject::connect(&timer, &QTimer::timeout, owner, [this] { poll(); });
    timer.setInterval(34);
    timer.start();
    QObject::connect(qGuiApp, &QGuiApplication::applicationStateChanged, owner,
                     [this](Qt::ApplicationState state)
                     {
                         if (state != Qt::ApplicationActive && roiSession.active())
                         {
                             finishRoi();
                             updateResults();
                         }
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
                         if (auto *viewport = presentation.viewport())
                             viewport->setAssistPreview(assistPreviewRois);
                         emit q->assistChanged();
                     });
    QObject::connect(assist.get(), &AssistPreview::busyChanged, owner,
                     [this]
                     {
                         if (presentation.draining())
                             finishDrain();
                     });
    // Release completion is part of the drain condition: management never joins the worker.
    QObject::connect(assist.get(), &AssistPreview::sessionReleased, owner,
                     [this]
                     {
                         if (presentation.draining())
                             finishDrain();
                     });
    tools.setRegistry(registry);
    toolEditor.setAppController(owner);
    toolEditor.setCustomStore(&customStore);
    toolEditor.setEditBuiltins(settings.editBuiltins);
    if (!editor.project().flows.empty())
        flow = workbench::q(editor.project().flows.front().id);
    if (std::any_of(registry.definitions().begin(), registry.definitions().end(), [](const auto &tool)
                    { return tool.kind != ToolKind::Host; }))
        refresh(false);
    // A test needs the steady state, not the moment before the check answers. The wait uses the same
    // probe and the same refresh a user's first second does; a shipped build never reaches it.
    if (!testPythonExecutable.isEmpty() && pythonCheck != PythonCheck::None)
        waitForPythonCheck();
}

AppController::Impl::~Impl()
{
    // Queued runtime answers check this before touching anything: the members they read are about to
    // go, and the events they arrived in are removed with the timer below.
    tearingDown = true;
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
    presentation.setRuntime(nullptr);
    if (auto *viewport = presentation.viewport())
        viewport->endRoiEdit();
    roiSession.end();
    presentation.clearDisplay();
    presentation.clear();
    clipboard.reset();
    operations.clearReplacement();
    refreshResult.reset();
    // The check outlives nothing: closing the window cancels it, and its answer can no longer reach
    // a backend that is already gone.
    pythonProbe.reset();
    for (const auto &tool : registry.definitions())
        if (tool.kind == ToolKind::PythonFunction || tool.kind == ToolKind::PythonClass)
            registry.setPythonImplementation(
                tool.nameEn, {}, makeStatus(ErrorCode::PythonUnavailable, "Interpreter shutting down"));
    python.reset();
}

ResultSlot::Snapshot AppController::Impl::displaySnapshot(const NodeDefinition &node) const
{
    return presentation.displaySnapshot(node);
}

PortValue AppController::Impl::resolvedInput(const NodeDefinition &node, PortId input) const
{
    const auto *value = presentation.resolvedInput(node, input);
    return value ? *value : PortValue{Missing{}};
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

const std::map<PortId, bool> *
AppController::Impl::refreshConnectable(const FlowDefinition &flow, const NodeDefinition &node) const
{
    if (connectable.valid && connectable.node == node.id && connectable.epoch == projectEpoch
        && connectable.structureRevision == flow.structureRevision && connectable.flow.toStdString() == flow.id)
        return connectable.valid ? &connectable.connectable : nullptr;
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
    return &connectable.connectable;
}

const PortDefinition *AppController::Impl::parameter(PortId id, bool initialization) const
{
    const auto *tool = effectiveTool();
    return tool ? workbench::port(initialization ? tool->initParams : tool->inputs, id) : nullptr;
}

bool AppController::Impl::idle()
{
    if (operations.isClosing())
        return false;
    if (state == RuntimeState::Idle && !operations.active())
        return true;
    report(QStringLiteral("当前任务尚未结束，配置编辑不可用"), LogSource::Ui);
    return false;
}

void AppController::Impl::setState(RuntimeState value)
{
    editor.setBusy(value != RuntimeState::Idle);
    if (state == value)
        return;
    state = value;
    if (auto *viewport = presentation.viewport())
        viewport->setReadOnly(state != RuntimeState::Idle || !roiSession.active());
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
    const auto kept = presentation.snapshotCount();
    presentation.retain(existing);
    if (presentation.snapshotCount() != kept)
    {
        if (roiSession.active() && !existing.contains(roiSession.node()))
            finishRoi();
        updateResults();
    }
}

void AppController::Impl::publishNodesChanged()
{
    // Structural change: reconcile the model first so QML delegates never observe stale rows.
    refreshNodeModel();
    emit q->nodesChanged();
}

void AppController::Impl::publishWiresChanged()
{
    // The wires binding depends on the edge topology only; timings and node state must not repaint
    // every connection. A signature keeps the decision here instead of in QML.
    QString signature;
    if (const auto *flow = currentFlow())
        for (const auto &relation : portRelations(*flow).active)
            signature += QString::number(relation.source.node) + u':' + QString::number(relation.source.port) + u'>'
                       + QString::number(relation.target.node) + u':' + QString::number(relation.target.port) + u';';
    if (signature == wireState)
        return;
    wireState = std::move(signature);
    emit q->wiresChanged();
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
    refreshNodeModel();
    publishWiresChanged();
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
    if (operations.isClosing())
        return;
    if (runtime->state() != RuntimeState::Idle)
        return;
    beginDrain([this] { requestSynchronize(); });
}

void AppController::Impl::requestSynchronize()
{
    if (operations.isClosing())
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
    parameterRows.clear();
    parameterRowsNode = 0;
    flowLatestMs.clear();
    latestRoundMs.reset();
    latestElapsedMs = 0;
    timing.clear();
    finishRoi();
    presentation.clear();
    activityEpoch.clear();
    updateResults();
    publishNodesChanged();
    emit q->runtimeChanged();
    emit q->timingChanged();
}

void AppController::Impl::applyActivity()
{
    if (operations.isClosing())
        return;
    const auto events = runtime->drainActivity();
    if (events.empty())
        return;
    bool        changed = false;
    std::set<NodeId> touched;
    for (const auto &event : events)
    {
        if (event.generation != generation || !nodeExists(event.node))
            continue;
        auto &epoch = activityEpoch[event.node];
        if (event.session < epoch.session
            || (event.session == epoch.session && event.round < epoch.round))
            continue;
        epoch = {event.session, event.round};
        if (event.kind == ActivityKind::EnteredExecution)
        {
            // A queued enter can arrive after the run already published its terminal snapshot. The
            // snapshot is the authority; do not repaint a finished node as running for one frame.
            const auto *snapshot = presentation.snapshot(event.node);
            if (snapshot && (*snapshot)->round == event.round
                && (*snapshot)->disposition != NodeDisposition::Running)
                continue;
        }
        if (presentation.setActive(event.node, event.kind == ActivityKind::EnteredExecution))
        {
            touched.insert(event.node);
            changed = true;
        }
    }
    if (changed)
    {
        for (const auto node : touched)
            if (const auto *definition = findNode(node))
                nodesModel.update(nodeRow(*definition));
        emit q->nodesChanged();
    }
    // The event only says "this node entered/left execution"; the terminal state, timing and
    // diagnostics still come from the runtime snapshot.
    poll();
}

void AppController::Impl::poll()
{
    if (operations.closePhase() == ProjectOperations::ClosePhase::AwaitOwners)
    {
        if ((!python || python->isCloseReady()) && (!assist || assist->finished()))
        {
            if (assist)
                assist->finalizeShutdown();
            const bool announce = operations.markReady();
            emit q->closingChanged();
            if (announce)
                emit q->closeReady();
        }
        return;
    }
    if (operations.closePhase() == ProjectOperations::ClosePhase::Ready)
        return;
    auto update = runtime->pollLatest();
    if (update.generation >= generation)
    {
        if (update.generation != generation)
        {
            generation = update.generation;
            // Every presented frame and request carries the runtime generation; a stale value would
            // make the viewport reject frames the converter just produced.
            presentation.setGeneration(generation);
            clearResults();
        }
        const bool enteringReadback = update.readback && presentation.setReadback(&*update.readback);
        bool nodesChanged = false, selectedChanged = false, parametersChanged = false;
        std::set<NodeId> touched;
        for (auto &item : update.nodes)
        {
            if (!nodeExists(item.node))
            {
                presentation.forgetNode(item.node);
                continue;
            }
            const auto *lifecycleBefore = presentation.lifecycle(item.node);
            const auto *instanceBefore  = presentation.instance(item.node);
            if (!presentation.applyNode(item.node, item.active, item.lifecycleDiagnostic,
                                        item.instanceDiagnostic, std::move(item.snapshot), generation))
                continue;
            nodesChanged = true;
            touched.insert(item.node);
            const auto *lifecycle = presentation.lifecycle(item.node);
            if (lifecycle && lifecycle != lifecycleBefore)
            {
                selectedChanged |= selected == QString::number(item.node);
                report(*lifecycle);
                if (const auto *node = findNode(item.node))
                {
                    const auto avail = registry.availability(node->toolId);
                    tools.updateToolStatus(QString::fromStdString(node->toolId),
                                           avail.state == ToolAvailability::Available,
                                           QString::fromStdString(avail.status.message));
                }
            }
            const auto *instance = presentation.instance(item.node);
            if (instance && instance != instanceBefore)
            {
                report(*instance);
                if (const auto *node = findNode(item.node))
                {
                    const auto avail = registry.availability(node->toolId);
                    tools.updateToolStatus(QString::fromStdString(node->toolId),
                                           avail.state == ToolAvailability::Available,
                                           QString::fromStdString(avail.status.message));
                }
            }
            const auto *snapshot = presentation.snapshot(item.node);
            if (snapshot && (*snapshot)->generation == generation)
            {
                selectedChanged |= QString::number(item.node) == selected;
                if (const auto *node = currentNode())
                    parametersChanged |= std::any_of(node->inputs.begin(), node->inputs.end(),
                                                     [&](const auto &input)
                                                     {
                                                         return input.second.mode == BindingMode::Upstream
                                                             && input.second.source
                                                             && input.second.source->node == item.node;
                                                     });
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
        {
            // Only the rows whose runtime state changed are recomputed and reported.
            for (const auto node : touched)
                if (const auto *definition = findNode(node))
                    nodesModel.update(nodeRow(*definition));
            emit q->nodesChanged();
        }
        if (parametersChanged || enteringReadback)
        {
            if (roiSession.active())
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
            if (auto *viewport = presentation.viewport())
                viewport->setReadOnly(state != RuntimeState::Idle || !roiSession.active());
            updateReadback();
            if (refreshResult)
                refreshFinished();
            updateResults();
            emit q->parametersChanged();
            emit q->runtimeChanged();
            advancePending();
        }
    }
    // A Python check that answered during a run has not reached the tool list yet; the moment the
    // runtime is idle again it does, without asking the user to refresh manually.
    if (pythonRefreshPending && state == RuntimeState::Idle && !operations.isClosing())
        schedulePythonRefresh();
    updateReadback();
}

void AppController::Impl::start(RunMode mode)
{
    // A run may supersede a pending synchronize: it waits for the same drain and becomes the
    // continuation, so the newest management intent is the one that executes.
    if (operations.isClosing() || state != RuntimeState::Idle || operations.active())
        return;
    finishRoi();
    q->parameters();
    beginDrain([this, mode] { beginRun(mode); });
}

void AppController::Impl::beginRun(RunMode mode)
{
    if (operations.isClosing())
        return;
    std::optional<NodeId> target;
    if (mode == RunMode::Node)
    {
        const auto *node = currentNode();
        if (!node)
        {
            report(QStringLiteral("请先选择工具节点"), LogSource::Ui);
            return;
        }
        target = node->id;
    }
    runtime->setReadbackEnabled(true);
    // A read-safe window belongs to the session that requested it; drop the previous one before
    // the new session can publish its own.
    presentation.setReadback(nullptr);
    const auto result = runtime->start(editor.project(), registry, mode, target);
    if (!result)
    {
        report(result);
        return;
    }
    LogService::write(LogLevel::Info, LogSource::System,
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
    presentation.clearActive();
    setState(mode == RunMode::Once   ? RuntimeState::RunningOnce
             : mode == RunMode::Loop ? RuntimeState::RunningLoop
                                     : RuntimeState::RunningNode);
}

void AppController::Impl::request(ProjectOperations::Action action, QString path)
{
    using Action     = ProjectOperations::Action;
    using ClosePhase = ProjectOperations::ClosePhase;
    if (action == Action::Close)
    {
        if (operations.closePhase() == ClosePhase::Ready || operations.closeReadyEmitted())
            return;
        if (operations.closePhase() == ClosePhase::AwaitOwners)
        {
            if ((!python || python->isCloseReady()) && (!assist || assist->finished()))
            {
                if (assist)
                    assist->finalizeShutdown();
                const bool announce = operations.markReady();
                emit q->closingChanged();
                if (announce)
                    emit q->closeReady();
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
        if (operations.closePhase() == ClosePhase::Resetting || operations.action() == Action::Close)
            return;
    }
    if (operations.isClosing())
    {
        report(QStringLiteral("正在关闭中，不能执行新操作"));
        return;
    }
    if (operations.active())
    {
        report(QStringLiteral("请先完成或取消当前文件操作"));
        return;
    }
    finishRoi();
    if (action == Action::Reset && assist)
        // Resetting tears the assist session down; the drain below waits for the async release.
        assist->releaseSession();
    if (!operations.begin(action, std::move(path)))
        return;
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
    using Action     = ProjectOperations::Action;
    using ClosePhase = ProjectOperations::ClosePhase;
    if (!operations.active() || state != RuntimeState::Idle)
        return;
    if (operations.action() == Action::DeleteNodes)
    {
        auto deletion = std::move(*pendingDeletion);
        pendingDeletion.reset();
        operations.complete();
        if (command([&] { return editor.removeNodes(deletion.flow, deletion.nodes); }, true))
            LogService::write(LogLevel::Info, LogSource::System,
                              QStringLiteral("删除 %1 个节点").arg(static_cast<qulonglong>(deletion.nodes.size())));
        return;
    }
    if (!operations.approved() && editor.project().dirty)
    {
        if (!operations.awaitingDecision())
        {
            operations.requestDecision();
            emit q->unsavedDecisionRequired();
        }
        return;
    }
    operations.approve();
    if (!operations.resetStarted())
    {
        if (operations.action() == Action::Close)
        {
            operations.beginResetting();
            emit q->closingChanged();
            if (!operations.drained())
            {
                operations.markDrained();
                beginDrain([this] { advancePending(); });
                return;
            }
            const auto result = runtime->requestReset();
            if (!result)
            {
                report(result);
                operations.failReset();
                emit q->closingChanged();
                return;
            }
            operations.markResetStarted();
            setState(RuntimeState::Resetting);
            return;
        }
        try
        {
            if (!operations.replacement() && operations.action() == Action::Open)
            {
                auto loaded = ProjectStorage(registry).load(workbench::localPath(operations.path()));
                storageDiagnostics(loaded.diagnostics);
                pendingToolUpdates          = std::move(loaded.toolUpdates);
                operations.replacement()    = std::move(loaded.project);
            }
            else if (!operations.replacement() && operations.action() == Action::New)
            {
                pendingToolUpdates       = {};
                operations.replacement() = ProjectDefinition{};
            }
        }
        catch (const std::exception &error)
        {
            report(error, QStringLiteral("工程打开失败"));
            operations.cancel();
            return;
        }
        if (!operations.drained())
        {
            operations.markDrained();
            beginDrain([this] { advancePending(); });
            return;
        }
        const auto result = runtime->requestReset();
        if (!result)
        {
            report(result);
            operations.cancel();
            return;
        }
        operations.markResetStarted();
        setState(RuntimeState::Resetting);
        return;
    }
    auto completion = operations.complete();
    bool replacementCommitted = false;
    if (completion.replacement)
    {
        editor.replaceProject(std::move(*completion.replacement));
        replacementCommitted = true;
        ++projectEpoch;
        flow = editor.project().flows.empty() ? QString{} : workbench::q(editor.project().flows.front().id);
        selected.clear();
        clipboard.reset();
        emit q->pasteChanged();
        search.clear();
        clearResults();
        changed(true);
        emit q->selectionChanged();
        emit q->projectReplaced();
        // The notice describes the project that is now open, so it waits for the commit.
        publishToolUpdates(pendingToolUpdates);
        pendingToolUpdates.clear();
    }
    if (completion.action == Action::Open && replacementCommitted)
    {
        recordRecent(completion.path);
        LogService::write(LogLevel::Info, LogSource::System,
                          QStringLiteral("打开工程 %1").arg(workbench::pathText(workbench::localPath(completion.path))));
    }
    if (completion.action == Action::Close)
    {
        // Release the helper before the drain: the barrier stays closed until the worker
        // acknowledges, so the close never races a release that is still destroying the instance.
        if (assist)
            assist->releaseSession();
        beginDrain([this] { finishClose(); });
        return;
    }
    else if (completion.action == Action::Reset)
        synchronizeNodes();
}

void AppController::Impl::recordRecent(const QString &path)
{
    bool       persisted = true;
    auto       updated   = RecentProjects::record(path, &persisted);
    if (!persisted)
        report(QStringLiteral("最近工程列表无法写入，只在本次运行可见：") + UiPreferences::path());
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
    presentation.clearDisplay();
    if (auto *viewport = presentation.viewport())
        viewport->setAssistActive(false);
    roiSession.end();
    parameterRows.clear();
    parameterRowsNode = 0;
    clipboard.reset();
    operations.clearReplacement();
    refreshResult.reset();
    clearResults();

    if (python)
        python->prepareShutdown();
    const bool assistReady = !assist || assist->finished();
    if (!assistReady || (python && !python->isCloseReady()))
    {
        operations.holdForOwners();
        if (python && !python->isCloseReady())
            report(QStringLiteral("等待外部资源释放后关闭 (剩余引用数: %1)").arg(python->activeReferences()));
        else
            report(QStringLiteral("等待智能点选释放后关闭"));
        return;
    }
    if (assist)
        assist->finalizeShutdown();
    const bool announce = operations.markReady();
    emit q->closingChanged();
    if (announce)
        emit q->closeReady();
}

void AppController::Impl::refresh(bool rescan)
{
    if (state != RuntimeState::Idle || operations.isClosing() || operations.action() == ProjectOperations::Action::Close)
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
    if (operations.isClosing())
        return;
    auto        result        = std::make_shared<RefreshResult>();
    auto        definitions   = registry.definitions();
    auto       *pythonBackend = python.get();
    const auto *catalog       = &registry;
    // The status is snapshotted here, on the thread that owns it; the refresh itself runs on a
    // runtime worker and must not read a value another thread can replace under it.
    const auto pythonStatus = pythonBackend ? pythonBackend->environmentStatus() : PythonEnvironmentStatus{};
    result->environment     = pythonStatus;
    const auto  accepted      = runtime->requestRefresh(
        [result, definitions = std::move(definitions), pythonBackend, pythonStatus, catalog]
        {
            catalog->loadNativeImplementations();
            if (pythonBackend)
            {
                result->implementations = pythonBackend->refresh(definitions, pythonStatus);
                for (const auto &tool : definitions)
                    if (tool.kind == ToolKind::PythonFunction || tool.kind == ToolKind::PythonClass)
                    {
                        const auto status = std::find_if(result->implementations.begin(), result->implementations.end(),
                                                               [&](const auto &item) { return item.toolId == tool.nameEn; });
                        if (status != result->implementations.end() && status->status)
                            result->factories.emplace_back(tool.nameEn, pythonBackend->factory(tool));
                    }
            }
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

bool AppController::Impl::beginPythonCheck(const QString &selection, PythonCheck purpose)
{
    if (!pythonProbe || pythonProbe->running())
        return false;
    if (purpose == PythonCheck::Startup)
    {
        // The window opens now and the tool list states why Python tools are not usable yet. The
        // phase is the host's answer for the time being, not a second status the UI has to combine.
        PythonEnvironmentStatus status;
        status.phase    = PythonEnvironmentPhase::Probing;
        status.selected = selection.toStdString();
        status.reason   = "正在检查 Python 环境";
        python->setEnvironmentStatus(std::move(status));
        emit q->pythonEnvironmentChanged();
    }
    pythonCheck = purpose;
    if (!pythonProbe->start(selection))
    {
        pythonCheck = PythonCheck::None;
        return false;
    }
    return true;
}

void AppController::Impl::finishPythonCheck()
{
    const auto purpose = pythonCheck;
    pythonCheck        = PythonCheck::None;
    if (purpose == PythonCheck::None)
        return;
    const auto result = pythonProbe->result();

    if (purpose == PythonCheck::Draft)
    {
        if (!pendingSettings)
            return;
        if (!result.status.available())
        {
            const auto reason = result.status.reason.empty()
                                    ? QStringLiteral("所选 Python 环境不可用")
                                    : QString::fromStdString(result.status.reason);
            pendingSettings.reset();
            settingsError      = reason;
            settingsErrorField = QStringLiteral("runtime");
            emit q->settingsChanged();
            emit q->settingsApplied(false);
            return;
        }
        const auto next = *pendingSettings;
        commitSettings(next);
        return;
    }

    if (!result.status.available())
    {
        pythonEnvironment.reset();
        python->setEnvironmentStatus(result.status);
        emit q->pythonEnvironmentChanged();
        report(QString::fromStdString(result.status.reason));
    }
    else
    {
        // The bridge is created here, on the thread that owns it and only once every check passed.
        pythonEnvironment = result.environment;
        QString error;
        if (!python->start(*result.environment, &error))
            report(error.isEmpty() ? python->unavailableReason() : error);
        emit q->pythonEnvironmentChanged();
    }
    schedulePythonRefresh();
}

void AppController::Impl::schedulePythonRefresh()
{
    if (!runtime || operations.isClosing())
        return;
    if (state != RuntimeState::Idle)
    {
        pythonRefreshPending = true;
        return;
    }
    pythonRefreshPending = false;
    // Same gate as any other registry change: the readers release their instances first, then the
    // tool statuses are replaced.
    beginDrain([this] { requestRefresh(false); });
}

void AppController::Impl::waitForPythonCheck()
{
    // A test needs the steady state, not the moment before the check answers. The wait pumps the
    // same events a user's window does; nothing here shortcuts the probe or the refresh.
    const auto deadline = QDeadlineTimer(120000);
    while ((pythonCheck != PythonCheck::None || pythonRefreshPending || state != RuntimeState::Idle
            || presentation.draining())
           && !deadline.hasExpired())
        QCoreApplication::processEvents(QEventLoop::AllEvents, 20);
}

bool AppController::Impl::commitSettings(Settings next)
{
    QString error;
    // Write first, then change what the process does: a failed save must leave both the file and
    // the running behaviour exactly as they were.
    if (!UiPreferences::saveSettings(next, &error))
    {
        pendingSettings.reset();
        settingsError      = error;
        settingsErrorField.clear();
        emit q->settingsChanged();
        emit q->settingsApplied(false);
        return false;
    }

    pendingSettings.reset();
    const bool builtinsChanged = next.editBuiltins != settings.editBuiltins;
    settings                   = std::move(next);
    if (builtinsChanged)
    {
        toolEditor.setEditBuiltins(settings.editBuiltins);
        emit q->editBuiltinsChanged();
    }
    LogService::instance().setMinimumLevel(settings.loggingLevel);
    settingsError.clear();
    settingsErrorField.clear();
    emit q->settingsChanged();
    emit q->settingsApplied(true);
    return true;
}

void AppController::Impl::refreshFinished()
{
    auto result = std::move(refreshResult);
    for (const auto &definition : registry.definitions())
        if (definition.kind == ToolKind::Cpp)
        {
            const auto availability = registry.availability(definition.nameEn);
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
        // A tool that fails on its own is reported by its own name. While the environment is not
        // available, every Python tool carries the same environment sentence, so a single check
        // would add one identical line per tool for a state the tool list and the settings window
        // already show. Only per-tool failures reach the log.
        if (!implementation.status && result->environment.available())
            report(implementation.status);
    }
    if (refreshReconcile)
    {
        try
        {
            auto reconciled = ProjectStorage(registry).reconcile(editor.project());
            storageDiagnostics(reconciled.diagnostics);
            publishToolUpdates(reconciled.toolUpdates);
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
        std::vector<std::string> customProblems;
        for (const auto &userTool : customStore.discoverCustomTools(&customProblems))
        {
            // Duplicate ids are reported instead of silently dropped: the packaged tool keeps its
            // published identity, and the user needs both paths to find the conflicting file.
            if (const auto *existing = registry.find(userTool.definition.nameEn))
            {
                report(QStringLiteral("自定义工具与已发布工具同名，已保留原工具：%1（%2 / %3）")
                           .arg(QString::fromStdString(userTool.definition.nameEn),
                                workbench::pathText(existing->definitionPath),
                                workbench::pathText(userTool.definition.definitionPath)));
                continue;
            }
            registry.addDefinition(userTool.definition);
        }
        for (const auto &problem : customProblems)
            report(QStringLiteral("自定义工具声明已跳过：%1").arg(QString::fromStdString(problem)));
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
    if (state != RuntimeState::Idle || operations.isClosing())
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
    // The model notifies its own rows; no cache behind the nodes property can go stale.
}

AppController::~AppController() = default;

ToolEditorController *AppController::toolEditor() const noexcept
{
    return &impl_->toolEditor;
}

bool AppController::editBuiltins() const
{
    return impl_->settings.editBuiltins;
}

QString AppController::settingsError() const
{
    return impl_->settingsError;
}

QString AppController::settingsErrorField() const
{
    return impl_->settingsErrorField;
}

bool AppController::settingsApplying() const
{
    return impl_->pendingSettings.has_value();
}

QString AppController::pythonEnvironmentPhase() const
{
    return QString::fromLatin1(pythonEnvironmentPhaseKey(impl_->python->environmentStatus().phase));
}

QString AppController::pythonEnvironmentText() const
{
    // The host's own sentence, not a second wording of the same state: only "ready" needs a label
    // the backend has no reason to carry.
    const auto &status = impl_->python->environmentStatus();
    if (status.phase != PythonEnvironmentPhase::Ready)
        return QString::fromStdString(status.reason);
    return status.version.empty() ? QStringLiteral("已就绪")
                                  : QStringLiteral("已就绪：Python %1")
                                        .arg(QString::fromStdString(status.version));
}

QVariantMap AppController::pythonEnvironmentReport() const
{
    const auto &status = impl_->python->environmentStatus();
    const auto  text   = [](const std::string &value)
    {
        return QString::fromStdString(value);
    };
    const auto fileText = [](const std::filesystem::path &path)
    {
        const auto bytes = path.generic_u8string();
        return QString::fromUtf8(reinterpret_cast<const char *>(bytes.data()), static_cast<qsizetype>(bytes.size()));
    };

    QVariantMap environment{{QStringLiteral("phase"), QString::fromLatin1(pythonEnvironmentPhaseKey(status.phase))},
                            {QStringLiteral("reason"), text(status.reason)},
                            {QStringLiteral("selected"), text(status.selected)},
                            {QStringLiteral("executable"), text(status.executable)},
                            {QStringLiteral("version"), text(status.version)},
                            {QStringLiteral("numpyVersion"), text(status.numpyVersion)},
                            {QStringLiteral("numpyFile"), text(status.numpyFile)}};
    // The prefix and search paths come from the accepted environment, not from the status: they are
    // what the bridge was started with, and the evidence has to name the real files.
    if (impl_->pythonEnvironment)
    {
        environment.insert(QStringLiteral("prefix"), fileText(impl_->pythonEnvironment->prefix));
        environment.insert(QStringLiteral("basePrefix"), fileText(impl_->pythonEnvironment->basePrefix));
        environment.insert(QStringLiteral("probeRuntime"), fileText(impl_->pythonEnvironment->dll));
    }

    const auto          facts = impl_->python->runtimeFacts();
    QVariantList        modules;
    for (const auto &module : facts.modules)
        modules.push_back(QVariantMap{{QStringLiteral("name"), text(module.first)},
                                      {QStringLiteral("path"), text(module.second)}});
    const QVariantMap process{{QStringLiteral("runtime"), text(facts.runtime)},
                              {QStringLiteral("bridge"), facts.bridge},
                              {QStringLiteral("modules"), modules}};

    return {{QStringLiteral("schemaVersion"), 1},
            {QStringLiteral("application"), QCoreApplication::applicationDirPath()},
            {QStringLiteral("version"), QCoreApplication::applicationVersion()},
            {QStringLiteral("environment"), environment},
            {QStringLiteral("process"), process}};
}

QVariantMap AppController::settingsSnapshot() const
{
    const auto level = logLevelKey(impl_->settings.loggingLevel);
    return {{QStringLiteral("editBuiltins"), impl_->settings.editBuiltins},
            {QStringLiteral("loggingLevel"), QString::fromLatin1(level.data(), static_cast<qsizetype>(level.size()))},
            {QStringLiteral("pythonExecutable"), impl_->settings.pythonExecutable}};
}

bool AppController::applySettings(const QVariantMap &values)
{
    const auto fail = [this](const QString &field, const QString &reason)
    {
        // A refused draft never became a commit: the caller reads settingsError, and settingsApplied
        // stays reserved for commits that were attempted and finished.
        impl_->settingsError      = reason;
        impl_->settingsErrorField = field;
        emit settingsChanged();
        return false;
    };
    // One commit at a time: a second apply while the interpreter is being checked would start a
    // second process against the same settings file.
    if (impl_->pendingSettings)
        return fail(QStringLiteral("runtime"), QStringLiteral("正在检查 Python 环境，请稍候"));

    Settings next = impl_->settings;
    next.editBuiltins = values.value(QStringLiteral("editBuiltins"), next.editBuiltins).toBool();

    LogLevel level = next.loggingLevel;
    if (!parseLogLevel(values.value(QStringLiteral("loggingLevel")).toString().toStdString(), level))
        return fail(QStringLiteral("logging"), QStringLiteral("日志等级无效"));
    next.loggingLevel = level;

    // A chosen interpreter is resolved and checked before anything is written: an incompatible
    // environment is reported on the field that produced it, and no other setting is committed
    // either. An unchanged path is not checked again, so a temporarily unavailable environment does
    // not block saving the log level.
    QString    error;
    const auto executable = resolvePythonExecutable(values.value(QStringLiteral("pythonExecutable")).toString(), &error);
    if (!error.isEmpty())
        return fail(QStringLiteral("runtime"), error);
    // Clearing the choice needs no interpreter and no check: "not configured" is a legal setting.
    const bool needsCheck = !executable.isEmpty() && executable != impl_->settings.pythonExecutable;
    next.pythonExecutable = executable;

    if (next.editBuiltins != impl_->settings.editBuiltins && !impl_->toolEditor.canSetEditBuiltins(next.editBuiltins))
        return fail(QStringLiteral("builtins"), QStringLiteral("内置工具还有未保存的修改，请先保存或取消"));

    if (needsCheck)
    {
        // The answer arrives as settingsApplied: the window stays responsive while the interpreter
        // starts, and the draft is written only if that check accepts it. The draft is recorded
        // before the check can answer, and only once the check actually started.
        if (!impl_->beginPythonCheck(executable, Impl::PythonCheck::Draft))
            return fail(QStringLiteral("runtime"), QStringLiteral("正在检查 Python 环境，请稍候"));
        impl_->pendingSettings = next;
        impl_->settingsError.clear();
        impl_->settingsErrorField.clear();
        emit settingsChanged();
        return true;
    }
    return impl_->commitSettings(std::move(next));
}

void AppController::setEditBuiltins(bool enabled)
{
    auto values               = settingsSnapshot();
    values[QStringLiteral("editBuiltins")] = enabled;
    applySettings(values);
}

bool AppController::saveBuiltinTool(const ToolDefinition &authored, QString *error)
{
    const auto fail = [error](const QString &message)
    {
        if (error)
            *error = message;
        return false;
    };
    if (authored.definitionPath.empty())
        return fail(QStringLiteral("内置工具没有声明文件路径，不能保存"));

    try
    {
        const auto root = softwareDataDir();
        // resolveToolPath refuses a declaration outside the software root, the only place this
        // application writes. A relative lookup also throws across drives, which is the same answer.
        (void)resolveToolPath(root, std::filesystem::relative(authored.definitionPath, root));
        // The compiled implementation owns the ports: a declaration that no longer matches it would
        // be rejected the next time the tool is registered, so it is refused before anything is
        // written and the editor keeps the draft.
        impl_->registry.validateContract(authored);
        const auto stored = writeToolDefinitionFile(authored.definitionPath, authored);
        impl_->registry.replaceDefinition(stored);
        emit catalogChanged();
        return true;
    }
    catch (const std::exception &failure)
    {
        return fail(QString::fromUtf8(failure.what()));
    }
}

const ProjectDefinition &AppController::project() const noexcept
{
    return impl_->editor.project();
}

const std::vector<ToolDefinition> &AppController::toolCatalog() const noexcept
{
    return impl_->registry.definitions();
}

QString AppController::title() const
{
    return q(impl_->editor.project().name) + (dirty() ? " *" : "") + " — VisionRT "
        + q(GetVersionString());
}

bool AppController::busy() const
{
    return impl_->state != RuntimeState::Idle || impl_->presentation.draining();
}

bool AppController::runtimeBusy() const
{
    return impl_->state != RuntimeState::Idle;
}

bool AppController::workspaceEnabled() const
{
    // Idle, not draining and not closing: the same conditions the editing commands already enforce.
    return !busy() && !impl_->operations.isClosing();
}

bool AppController::stopEnabled() const
{
    return impl_->state == RuntimeState::RunningOnce || impl_->state == RuntimeState::RunningLoop
        || impl_->state == RuntimeState::RunningNode;
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
    AboutReport report;
    report.host            = impl_->hostBuildInfo;
    report.toolsInstalled  = impl_->toolsPackageInstalled;
    report.tools           = impl_->toolsPackageBuildInfo;
    report.contracts       = GetContractsVersion();
    report.contractsBuild  = impl_->contractsBuildInfo;
    report.pythonAvailable = static_cast<bool>(impl_->python);
    return aboutReportText(report);
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

PythonBackend *AppController::pythonBackend() const
{
    return impl_ ? impl_->python.get() : nullptr;
}

void AppController::usePythonForTesting(const QString &executable)
{
    // One string, set before the first controller is built. It is never exported to a model, never
    // written to the preferences file and never consulted when it is empty.
    Impl::testPythonExecutable = executable;
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
    return impl_->nodesModel.snapshot();
}

QAbstractListModel *AppController::nodeListModel() const
{
    return &impl_->nodesModel;
}

QVariantMap AppController::nodeRecord(const QString &id) const
{
    return impl_->nodesModel.rowMap(id);
}

QPointF AppController::nodePosition(const QString &id) const
{
    // A cell position, not a pixel one: the canvas owns the zoom and the grid size.
    if (const auto *row = impl_->nodesModel.rowAt(impl_->nodesModel.rowOf(id)))
        return {row->x, row->y};
    return {};
}

QVariantList AppController::nodePositions() const
{
    QVariantList result;
    const auto   rows = impl_->nodesModel.snapshot();
    result.reserve(rows.size());
    for (const auto &value : rows)
    {
        const auto row = value.toMap();
        result.push_back(QVariantMap{{"id", row.value("id")}, {"x", row.value("x")}, {"y", row.value("y")}});
    }
    return result;
}

NodeListModel::Row AppController::Impl::nodeRow(const NodeDefinition &node) const
{
    const bool running = presentation.active(node.id);
    const auto snapshot = displaySnapshot(node);
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
                          ? (workbench::q(snapshot->status.message).isEmpty()
                                 ? QStringLiteral("节点未执行")
                                 : QStringLiteral("节点未执行：") + workbench::q(snapshot->status.message))
                          : diagnosticMessage({snapshot->node, snapshot->toolId, snapshot->phase, snapshot->status});
        statusDetail = statusDisplay(snapshot->status).detail;
    }
    const auto timing = nodeTimingText(snapshot.get(), running);
    QVariantList phases;
    for (const auto &phase : timingPhasesOf(timing))
        phases.push_back(QVariantMap{{"label", phase.label}, {"value", phase.value}});

    NodeListModel::Row row;
    row.id            = QString::number(node.id);
    row.title         = workbench::q(node.title());
    row.alias         = workbench::q(node.alias);
    row.toolId        = workbench::q(node.toolId);
    row.x             = node.position.x;
    row.y             = node.position.y;
    row.state         = state;
    row.disposition   = dispositionName(disposition);
    row.executed      = disposition == NodeDisposition::Succeeded || disposition == NodeDisposition::Failed
                   || disposition == NodeDisposition::Canceled;
    row.timingText    = timing.total;
    row.timingPhases  = phases;
    row.statusCode    = statusCode;
    row.statusMessage = statusMessage;
    row.statusDetail  = statusDetail;
    if (const auto *instanceDiagnostic = presentation.instance(node.id))
    {
        row.state         = "failed";
        row.statusCode    = instanceDiagnostic->status.code;
        row.statusMessage = diagnosticMessage(*instanceDiagnostic);
        row.statusDetail  = workbench::q(instanceDiagnostic->status.detail);
    }
    if (const auto *lifecycleDiagnostic = presentation.lifecycle(node.id))
    {
        row.lifecycleMessage = diagnosticMessage(*lifecycleDiagnostic);
        row.lifecycleDetail  = workbench::q(lifecycleDiagnostic->status.detail);
    }
    const auto   *def     = registry.find(node.toolId);
    row.version           = def ? workbench::q(def->version) : QStringLiteral("1.0");
    // A snapshot produced by an older configuration is still shown, but it must not claim to
    // describe the current parameters.
    row.stale             = snapshot && snapshot->executionRevision != node.executionRevision;
    row.category          = def ? workbench::q(categoryPath(*def)) : QString();
    const auto avail      = registry.availability(node.toolId);
    row.defMissing        = avail.state == ToolAvailability::DefinitionMissing;
    row.implUnavailable   = !row.defMissing
                          && (avail.state != ToolAvailability::Available || presentation.instance(node.id) != nullptr);
    row.missing           = row.defMissing || row.implUnavailable;
    return row;
}

void AppController::Impl::refreshNodeModel()
{
    std::vector<NodeListModel::Row> rows;
    if (const auto *flow = currentFlow())
        for (const auto &node : flow->nodes)
            rows.push_back(nodeRow(node));
    nodesModel.sync(std::move(rows));
}

QVariantList AppController::wires() const
{
    QVariantList result;
    if (const auto *flow = impl_->currentFlow())
    {
        std::map<std::pair<NodeId, NodeId>, QVariantList> groups;
        for (const auto &relation : portRelations(*flow).active)
            groups[{relation.source.node, relation.target.node}].push_back(QVariantMap{
                {"output", relation.source.port},
                {"input", relation.target.port}
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
            impl_->wireState.clear();
            impl_->refreshNodeModel();
            emit selectionChanged();
            emit wiresChanged();
            emit nodesChanged();
            emit parametersChanged();
            impl_->updateResults();
            return;
        }
}

void AppController::selectNode(const QString &id)
{
    // Selection is a user command: a run owns the workspace, so a late click cannot move it.
    if (impl_->runActive() || impl_->operations.isClosing())
        return;
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
        LogService::write(LogLevel::Info, LogSource::System,
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
    if (impl_->operations.isClosing() || impl_->operations.active())
    {
        impl_->report(QStringLiteral("请先完成或取消当前操作"));
        return;
    }
    try
    {
        impl_->pendingDeletion = Impl::PendingDeletion{impl_->flow.toStdString(), ids(values)};
        impl_->request(ProjectOperations::Action::DeleteNodes);
    }
    catch (const std::exception &error)
    {
        impl_->report(error, QStringLiteral("节点删除失败"));
    }
}

void AppController::copyNodes(const QVariantList &values)
{
    if (impl_->operations.isClosing())
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


bool AppController::setParameterMode(int port, bool upstream)
{
    const auto id = static_cast<PortId>(port);
    return impl_->command(
        [&]
        {
            const auto *node = impl_->currentNode();
            if (!node)
                throw ContractError("未选择节点");
            const auto *tool = impl_->currentTool();
            if (tool)
            {
                if (const auto *declaration = workbench::port(tool->inputs, id))
                {
                    if (declaration->type.type == Type::Image && !upstream)
                        return false;
                }
            }
            impl_->finishRoi();
            return impl_->editor.setMode(impl_->flow.toStdString(), node->id, id,
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
        stop();
    else
        impl_->start(RunMode::Loop);
}

void AppController::stop()
{
    if (!stopEnabled())
        return;
    LogService::write(LogLevel::Info, LogSource::System, QStringLiteral("请求停止运行"));
    impl_->runtime->requestStop();
    impl_->setState(RuntimeState::Stopping);
}

void AppController::resetRuntime()
{
    impl_->request(ProjectOperations::Action::Reset);
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
    impl_->request(ProjectOperations::Action::New);
}

void AppController::requestOpen(const QString &path)
{
    if (!path.isEmpty())
        impl_->request(ProjectOperations::Action::Open, path);
}

QString AppController::examplesDirectory() const
{
    return workbench::pathText(ExampleProject::directory(impl_->applicationDirectory));
}

QUrl AppController::examplesDirectoryUrl() const
{
    const auto directory = ExampleProject::directory(impl_->applicationDirectory);
    if (!std::filesystem::is_directory(directory))
    {
        impl_->report(QStringLiteral("未安装示例工程：") + workbench::pathText(directory));
        return {};
    }
    return QUrl::fromLocalFile(workbench::pathText(directory));
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
    impl_->request(ProjectOperations::Action::Close);
}

bool AppController::isClosing() const
{
    return impl_->operations.isClosing();
}

void AppController::resolveUnsaved(const QString &decision, const QString &savePath)
{
    if (!impl_->operations.active() || !impl_->operations.awaitingDecision())
        return;
    if (decision == "cancel")
    {
        const bool wasClosing = impl_->operations.isClosing();
        impl_->operations.cancel();
        if (wasClosing)
            emit closingChanged();
        return;
    }
    if (decision == "save")
    {
        if (!save(savePath))
            return;
    }
    else if (decision != "discard")
        return;
    impl_->operations.approve();
    impl_->operations.decisionMade();
    impl_->advancePending();
}

bool AppController::save(const QString &path)
{
    if (impl_->operations.isClosing())
        return false;
    const auto previousPath = impl_->editor.project().path;
    auto destination = path.isEmpty() ? previousPath : workbench::localPath(path);
    if (destination.empty())
    {
        emit savePathRequired();
        return false;
    }
    if (!workspaceEnabled())
    {
        // A run owns the project; writing it mid-run would serialise a moving project, so the
        // request is refused instead of being queued for later.
        impl_->report(QStringLiteral("运行期间不能保存工程，请先停止运行"));
        return false;
    }
    // An installed example stays as the product shipped it. Ctrl+S therefore asks where the user's
    // own copy goes instead of writing the installation; the same rule covers an example reached
    // through the recent list or the ordinary open dialog.
    const bool installedExample = ExampleProject::owns(impl_->applicationDirectory, previousPath);
    if (path.isEmpty() && installedExample)
    {
        emit savePathRequired();
        return false;
    }
    try
    {
        auto candidate = impl_->editor.project();
        ProjectStorage(impl_->registry).save(candidate, destination);
        impl_->editor.replaceProject(std::move(candidate));
        impl_->synchronizeNodes();
        emit projectChanged();
        // A first save or a save-as puts the new file in the recent list; a plain Ctrl+S writes the
        // same file again and must leave the order alone.
        if (destination != previousPath)
            impl_->recordRecent(workbench::pathText(destination));
        LogService::write(LogLevel::Info, LogSource::System,
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
