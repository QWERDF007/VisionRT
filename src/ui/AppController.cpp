#include "runtime/Receiver.h"
#include "ui/AppController_p.h"
#include "ui/DurationText.h"

#include <visionrt/sdk/Package.h>
#include <visionrt/sdk/ValueYaml.h>

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
    bool       okay = false;
    const auto id   = text.toULongLong(&okay);
    if (!okay || text.isEmpty() || text.startsWith('-'))
        throw ContractError("Invalid node ID");
    return id;
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
    if (std::filesystem::exists(installed / "module_index.yaml"))
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
        report(QStringLiteral("Python 不可用：") + workbench::q(error.what()));
    }
    runtime = std::make_unique<RuntimeEngine>(workers);
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
                                     presentedRequest = frame.requestId;
                                     applyOverlays();
                                 }
                             }
                         }
                     });
    tools.setRegistry(registry);
    if (!editor.project().flows.empty())
        flow = workbench::q(editor.project().flows.front().id);
    if (std::any_of(registry.definitions().begin(), registry.definitions().end(), [](const auto &tool)
                    { return tool.kind == ToolKind::PythonFunction || tool.kind == ToolKind::PythonClass; }))
        refresh(false);
}

AppController::Impl::~Impl()
{
    timer.stop();
    runtime->setCompletionCallback({});
    runtime.reset();
    if (textTask.valid())
        textTask.wait();
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

void AppController::Impl::report(const QString &text)
{
    if (text.isEmpty())
        return;
    const auto bounded = text.size() > 16384 ? text.left(16360) + QStringLiteral("…[诊断已截断]") : text;
    if (diagnostics.empty() || diagnostics.back() != bounded)
        diagnostics.push_back(bounded);
    while (diagnostics.size() > 100) diagnostics.pop_front();
    diagnostic = bounded;
    emit q->diagnosticChanged();
}

void AppController::Impl::setState(RuntimeState value)
{
    if (state == value)
        return;
    state = value;
    editor.setBusy(state != RuntimeState::Idle);
    if (viewport)
        viewport->setReadOnly(state != RuntimeState::Idle || !roi);
    emit q->runtimeChanged();
}

void AppController::Impl::changed(bool structure)
{
    if (!currentNode() && !selected.isEmpty())
    {
        finishRoi();
        selected.clear();
        emit q->selectionChanged();
    }
    emit q->projectChanged();
    emit q->nodesChanged();
    emit q->parametersChanged();
    if (structure || !search.isEmpty())
        emit q->searchChanged();
    updateResults();
}

void AppController::Impl::storageDiagnostics(const std::vector<ProjectStorageDiagnostic> &items)
{
    for (const auto &item : items) report(workbench::q(item.message));
}

void AppController::Impl::clearResults()
{
    snapshots.clear();
    active.clear();
    lifecycleErrors.clear();
    timing.clear();
    finishRoi();
    converter.invalidate(generation);
    updateResults();
    emit q->nodesChanged();
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
        bool nodesChanged = false, selectedChanged = false, parametersChanged = false;
        for (auto &item : update.nodes)
        {
            nodesChanged = true;
            if (item.active)
                active.insert(item.node);
            else
                active.erase(item.node);
            if (item.lifecycleDiagnostic)
                lifecycleErrors[item.node] = *item.lifecycleDiagnostic;
            if (item.snapshot && item.snapshot->generation == generation)
            {
                const auto previous = snapshots.find(item.node);
                if (previous == snapshots.end() || previous->second != item.snapshot)
                {
                    snapshots[item.node] = std::move(item.snapshot);
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
        for (const auto &message : update.diagnostics) report(workbench::q(message));
        if (update.roundMilliseconds > 0)
        {
            timing = QStringLiteral("本轮 %1 ms").arg(update.roundMilliseconds, 0, 'f', 2);
            for (const auto &item : update.flows)
                timing += QStringLiteral(" · %1 %2 ms").arg(workbench::q(item.flow)).arg(item.milliseconds, 0, 'f', 2);
            emit q->runtimeChanged();
        }
        setState(update.state);
        if (nodesChanged)
            emit q->nodesChanged();
        if (parametersChanged)
        {
            if (roi)
                updateDisplay();
            emit q->parametersChanged();
        }
        if (selectedChanged)
            updateResults();
        if (update.completion && runtime->acknowledgeCompletion(*update.completion))
        {
            setState(RuntimeState::Idle);
            if (refreshResult)
                refreshFinished();
            advancePending();
        }
    }
    using namespace std::chrono_literals;
    if (textTask.valid() && textTask.wait_for(0ms) == std::future_status::ready)
    {
        try
        {
            auto result = textTask.get();
            if (result.generation == generation && q->portContextCurrent(result.context))
            {
                if (result.copy)
                    QGuiApplication::clipboard()->setText(result.text);
                emit q->outputTextReady(result.text);
            }
        }
        catch (const std::exception &error)
        {
            report(workbench::q(error.what()));
        }
    }
}

void AppController::Impl::start(RunMode mode)
{
    if (!idle())
        return;
    finishRoi();
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
        report(workbench::q(result.message));
        return;
    }
    active.clear();
    lifecycleErrors.clear();
    setState(mode == RunMode::Once   ? RuntimeState::RunningOnce
             : mode == RunMode::Loop ? RuntimeState::RunningLoop
                                     : RuntimeState::RunningNode);
}

void AppController::Impl::request(Pending action, QString path)
{
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
            report(workbench::q(error.what()));
            pending = Pending::None;
            return;
        }
        const auto result = runtime->requestReset();
        if (!result)
        {
            report(workbench::q(result.message));
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
        copiedNodes.clear();
        copiedFlow.clear();
        search.clear();
        clearResults();
        changed(true);
        emit q->selectionChanged();
        emit q->projectReplaced();
    }
    if (completed == Pending::Close)
        emit q->closeReady();
}

void AppController::Impl::refresh(bool rescan)
{
    if (state != RuntimeState::Idle)
        return;
    finishRoi();
    if (rescan)
        loadToolsPackage();
    auto       result        = std::make_shared<RefreshResult>();
    auto       definitions   = registry.definitions();
    auto      *pythonRuntime = python.get();
    const auto accepted      = runtime->requestRefresh(
        [result, definitions = std::move(definitions), pythonRuntime]
        {
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
        report(workbench::q(accepted.message));
        return;
    }
    refreshResult    = std::move(result);
    refreshReconcile = refreshReconcile || rescan;
    setState(RuntimeState::Refreshing);
}

void AppController::Impl::refreshFinished()
{
    auto result = std::move(refreshResult);
    for (auto &implementation : result->implementations)
    {
        ToolFactory factory;
        auto        found = std::find_if(result->factories.begin(), result->factories.end(),
                                         [&](const auto &item) { return item.first == implementation.toolId; });
        if (found != result->factories.end())
            factory = std::move(found->second);
        registry.setPythonImplementation(implementation.toolId, std::move(factory), implementation.status);
        if (!implementation.status)
            report(QStringLiteral("定义已加载，实现不可用：") + workbench::q(implementation.status.message));
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
            report(workbench::q(error.what()));
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
        report(workbench::pathText(candidate) + ": " + workbench::q(error.what()));
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
}

AppController::~AppController() = default;

QString AppController::title() const
{
    return q(impl_->editor.project().name) + (dirty() ? " *" : "") + " — VisionRT V3";
}

bool AppController::busy() const
{
    return impl_->state != RuntimeState::Idle;
}

QString AppController::runState() const
{
    return stateName(impl_->state);
}

QString AppController::diagnostic() const
{
    return impl_->diagnostic;
}

QStringList AppController::diagnostics() const
{
    QStringList result;
    for (const auto &item : impl_->diagnostics) result.push_back(item);
    return result;
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

QString AppController::aboutText() const
{
    return QStringLiteral("VisionRT V3\nCopyright © VisionRT contributors\nQt %1\nPython: %2")
        .arg(QString::fromLatin1(qVersion()), impl_->python ? QStringLiteral("解释器可用") : QStringLiteral("不可用"));
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

QVariantList AppController::nodes() const
{
    QVariantList result;
    if (const auto *flow = impl_->currentFlow())
        for (const auto &node : flow->nodes)
        {
            const bool  running = impl_->active.contains(node.id);
            QString     state   = running ? "blue" : "gray";
            const auto  found   = impl_->snapshots.find(node.id);
            const auto *snapshot = found != impl_->snapshots.end()
                                       && found->second->executionRevision == node.executionRevision
                                     ? found->second.get()
                                     : nullptr;
            std::int64_t statusCode = 0;
            QString      statusMessage;
            if (snapshot)
            {
                if (!running)
                    state = snapshot->status ? "green" : "red";
                if (!snapshot->status)
                {
                    statusCode    = snapshot->status.code;
                    statusMessage = q(snapshot->status.message);
                }
            }
            const auto timing = nodeTimingText(snapshot, running);
            if (impl_->lifecycleErrors.contains(node.id))
            {
                state         = "red";
                statusCode    = impl_->lifecycleErrors.at(node.id).code;
                statusMessage = q(impl_->lifecycleErrors.at(node.id).message);
            }
            const auto   *def             = impl_->registry.find(node.toolId);
            const QString version         = def ? q(def->version) : QStringLiteral("1.0");
            const QString category        = def ? q(def->category) : QString();
            const auto    avail           = impl_->registry.availability(node.toolId);
            const bool    defMissing      = (avail.state == ToolAvailability::DefinitionMissing);
            const bool    implUnavailable = (avail.state == ToolAvailability::ImplementationUnavailable);
            const bool    missing         = defMissing || implUnavailable;
            result.push_back(QVariantMap{
                {             "id", QString::number(node.id)},
                {          "title",          q(node.title())},
                {          "alias",            q(node.alias)},
                {         "toolId",           q(node.toolId)},
                {        "version",                  version},
                {       "category",                 category},
                {              "x",          node.position.x},
                {              "y",          node.position.y},
                {          "state",                    state},
                {      "timingText",                timing.total},
                {"timingOperatorText",             timing.toolCall},
                {"timingPreprocessText",         timing.preprocess},
                {"timingPostprocessText",        timing.postprocess},
                {        "missing",                  missing},
                {     "defMissing",               defMissing},
                {"implUnavailable",          implUnavailable},
                {     "statusCode",               statusCode},
                {  "statusMessage",            statusMessage}
            });
        }
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
                const auto *tool = impl_->registry.find(toolId.toStdString());
                if (!tool)
                    throw ContractError("Tool definition missing");
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
    impl_->command(
        [&]
        {
            impl_->finishRoi();
            return impl_->editor.removeNodes(impl_->flow.toStdString(), ids(values));
        },
        true);
}

void AppController::copyNodes(const QVariantList &values)
{
    try
    {
        impl_->copiedNodes = ids(values);
        impl_->copiedFlow  = impl_->flow.toStdString();
    }
    catch (const std::exception &error)
    {
        impl_->report(q(error.what()));
    }
}

void AppController::pasteNodes(int x, int y)
{
    impl_->command(
        [&]
        {
            if (impl_->copiedNodes.empty())
                return false;
            int minimumX = std::numeric_limits<int>::max(), minimumY = minimumX;
            for (const auto id : impl_->copiedNodes)
                if (const auto *node = impl_->findNode(id))
                {
                    minimumX = std::min(minimumX, node->position.x);
                    minimumY = std::min(minimumY, node->position.y);
                }
            if (minimumX == std::numeric_limits<int>::max())
                throw ContractError("Copied nodes no longer exist");
            const auto dx = static_cast<std::int64_t>(x) - minimumX, dy = static_cast<std::int64_t>(y) - minimumY;
            if (dx < std::numeric_limits<int>::min() || dx > std::numeric_limits<int>::max()
                || dy < std::numeric_limits<int>::min() || dy > std::numeric_limits<int>::max())
                throw ContractError("Paste grid offset is outside int range");
            return !impl_->editor
                        .copyNodes(impl_->copiedFlow, impl_->copiedNodes, impl_->flow.toStdString(),
                                   {static_cast<int>(dx), static_cast<int>(dy)})
                        .empty();
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
                throw ContractError("No selected node");
            const auto *tool = impl_->currentTool();
            if (tool)
            {
                if (const auto *port = workbench::port(tool->inputs, id.toStdString()))
                {
                    if (port->type.type == Type::ImageBatch && !upstream)
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
    auto candidate   = impl_->editor.project();
    auto destination = path.isEmpty() ? candidate.path : workbench::localPath(path);
    if (destination.empty())
    {
        emit savePathRequired();
        return false;
    }
    if (busy() && destination != candidate.path)
    {
        impl_->report(QStringLiteral("运行期间不能另存为；可保存当前已命名工程"));
        return false;
    }
    try
    {
        auto diagnostics = ProjectStorage(impl_->registry).save(candidate, destination);
        if (busy())
            impl_->editor.markSaved();
        else
            impl_->editor.replaceProject(std::move(candidate));
        impl_->storageDiagnostics(diagnostics);
        emit projectChanged();
        return true;
    }
    catch (const std::exception &error)
    {
        impl_->report(q(error.what()));
        return false;
    }
}

void AppController::requestOutputText(const QVariantMap &context, bool copy)
{
    if (!context.value("output").toBool() || !portContextCurrent(context))
        return;
    if (impl_->textTask.valid())
    {
        impl_->report(QStringLiteral("正在生成完整输出，请稍候"));
        return;
    }
    auto captured = impl_->outputs.capture(context.value("port").toString());
    if (!captured)
        return;
    const auto estimated = OutputModel::estimatedTextBytes(*captured);
    if (estimated > 16 * 1024 * 1024
        && (impl_->approvedTextContext != context || impl_->approvedCopy != copy))
    {
        impl_->approvedTextContext = context;
        impl_->approvedCopy = copy;
        emit outputTextReady(QStringLiteral("完整数据预计约 %1 MiB。再次点击同一查看/复制操作以确认；不会截断全文。")
                                 .arg(static_cast<double>(estimated) / (1024 * 1024), 0, 'f', 1));
        return;
    }
    impl_->approvedTextContext.clear();
    const auto generation = impl_->generation;
    impl_->textTask       = std::async(std::launch::async, [captured = std::move(*captured), copy, generation, context]
                                       { return Impl::TextResult{OutputModel::fullText(captured), copy, generation, context}; });
}
} // namespace visionrt
