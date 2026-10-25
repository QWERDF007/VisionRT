// Node-model materialisation, atomic editing transactions, execution gating and
// merge semantics. These contracts cover schema 3 / format 4 behaviour that the
// earlier suite could not exercise.
#include "core/NodeContracts.h"
#include "core/ProjectEditor.h"
#include "core/ProjectStorage.h"
#include "runtime/RuntimeEngine.h"
#include "runtime/SelectTool.h"

#include <atomic>
#include <chrono>
#include <functional>
#include <iostream>
#include <memory>
#include <stdexcept>
#include <string>
#include <QTemporaryDir>

using namespace visionrt;
using namespace std::chrono_literals;

namespace {
void require(bool condition, const std::string &message)
{
    if (!condition)
        throw std::runtime_error(message);
}

ToolDefinition expressionTool()
{
    ToolDefinition tool;
    tool.schemaVersion  = 3;
    tool.id             = "test.logic.expression";
    tool.displayName    = "表达式";
    tool.kind           = ToolKind::PythonClass;
    tool.nodeModel      = NodeModel::Expression;
    tool.entry          = PythonEntry{"logic", "expression/tool.py", "ExpressionTool"};
    tool.initParams     = {{"variables", "变量", {Type::Array, DType::String, 2}},
                           {"program", "公式", {Type::Array, DType::String, 2}}};
    tool.outputs        = {{"__status_code", "状态码", {Type::Int}},
                           {"__status_message", "状态消息", {Type::String}}};
    return tool;
}

ToolDefinition conditionTool()
{
    auto tool        = expressionTool();
    tool.id          = "test.logic.branch";
    tool.displayName = "条件分支";
    tool.nodeModel   = NodeModel::Condition;
    tool.outputs     = {{"__status_code", "状态码", {Type::Int}},
                        {"__status_message", "状态消息", {Type::String}},
                        {"on_true", "成立", {Type::Bool}},
                        {"on_false", "否则", {Type::Bool}}};
    return tool;
}

ToolDefinition selectTool()
{
    ToolDefinition tool;
    tool.schemaVersion = 3;
    tool.id            = "visionrt.control.select";
    tool.displayName   = "分支合流";
    tool.kind          = ToolKind::Host;
    tool.nodeModel     = NodeModel::Select;
    tool.hostBinding   = "visionrt.select";
    tool.outputs       = {{"__status_code", "状态码", {Type::Int}},
                          {"__status_message", "状态消息", {Type::String}}};
    return tool;
}

class Function final : public IToolInstance
{
public:
    std::function<ToolResult(const Values &)> run;
    ToolStatus initialize(const Values &) override
    {
        return {};
    }
    ToolResult execute(const Values &inputs) override
    {
        return run(inputs);
    }
    ToolStatus release() override
    {
        return {};
    }
};

void install(ToolRegistry &registry, const ToolDefinition &tool, std::function<ToolResult(const Values &)> run)
{
    registry.registerCpp(tool,
                         [run](const ToolServices &)
                         {
                             auto instance = std::make_unique<Function>();
                             instance->run = run;
                             return instance;
                         });
    registry.addDefinition(tool);
}

ToolDefinition intTool(std::string id, std::vector<std::string> inputs = {})
{
    ToolDefinition tool;
    tool.schemaVersion = 3;
    tool.id            = std::move(id);
    tool.displayName   = tool.id;
    tool.version       = "1";
    tool.kind          = ToolKind::Cpp;
    for (auto &input : inputs) tool.inputs.push_back({input, input, {Type::Int}});
    tool.outputs = {{"value", "值", {Type::Int}}};
    return tool;
}

ToolDefinition boolTool()
{
    ToolDefinition tool;
    tool.schemaVersion = 3;
    tool.id            = "test.logic.flag";
    tool.displayName   = "布尔来源";
    tool.version       = "1";
    tool.kind          = ToolKind::Cpp;
    tool.outputs       = {{"value", "条件", {Type::Bool}}};
    return tool;
}

NodeDefinition node(NodeId id, const ToolDefinition &tool, int y)
{
    NodeDefinition n;
    n.id               = id;
    n.position         = {0, y};
    n.toolId           = tool.id;
    n.toolNameSnapshot = tool.displayName;
    if (tool.nodeModel == NodeModel::Expression || tool.nodeModel == NodeModel::Condition)
        n.expression = ExpressionConfig{};
    if (tool.nodeModel == NodeModel::Select)
        n.selectType = TypeSpec{Type::Int};
    const auto contract = tool.derivesPorts() ? nodeContract(n, tool) : tool;
    for (const auto &port : contract.initParams) n.signatures.initParams.emplace(port.id, port.type);
    for (const auto &port : contract.inputs) n.signatures.inputs.emplace(port.id, port.type);
    for (const auto &port : contract.outputs) n.signatures.outputs.emplace(port.id, port.type);
    return n;
}

RuntimeUpdate finish(RuntimeEngine &engine)
{
    const auto deadline = std::chrono::steady_clock::now() + 10s;
    while (std::chrono::steady_clock::now() < deadline)
    {
        auto update = engine.pollLatest();
        if (update.completion)
        {
            require(engine.acknowledgeCompletion(*update.completion), "completion acknowledgement failed");
            return update;
        }
        std::this_thread::sleep_for(1ms);
    }
    throw std::runtime_error("runtime did not complete");
}

ResultSlot::Snapshot snapshot(const RuntimeUpdate &update, NodeId id)
{
    for (const auto &item : update.nodes)
        if (item.node == id)
            return item.snapshot;
    throw std::runtime_error("node snapshot absent");
}

ExpressionConfig sampleExpression()
{
    ExpressionConfig config;
    ExpressionVariable input;
    input.portId = "p1";
    input.name   = "半径列表";
    input.symbol = "radii";
    input.type   = {Type::Array, DType::Float64, 1};
    ExpressionResult first;
    first.portId    = "p2";
    first.name      = "数量";
    first.symbol    = "count";
    first.expression = "len(radii)";
    first.type      = {Type::Int};
    first.exposed   = true;
    ExpressionResult hidden;
    hidden.portId     = "p3";
    hidden.name       = "有效";
    hidden.symbol     = "ok";
    hidden.expression = "all_nonempty(v_gt(radii, 5.0))";
    hidden.type       = {Type::Bool};
    hidden.exposed    = false;
    config.nextPortSeq = 4;
    config.variables   = {input};
    config.program     = {first, hidden};
    return config;
}

void materialisationAndTransactions()
{
    const auto expression = expressionTool();
    ProjectEditor editor;
    const auto    id = editor.addNode("flow_1", expression, {});
    require(editor.project().flows.front().nodes.front().signatures.inputs.empty(),
            "a fresh expression node must not declare undeclared inputs");

    const auto config = sampleExpression();
    require(editor.setExpressionConfig("flow_1", id, config, expression), "expression transaction did not commit");
    auto contract = nodeContract(editor.project().flows.front().nodes.front(), expression);
    require(contract.inputs.size() == 1 && contract.inputs.front().id == "p1"
                && contract.inputs.front().name == "半径列表",
            "variable port was not materialised");
    require(contract.outputs.size() == 3 && contract.outputs[0].id == "__status_code"
                && contract.outputs[1].id == "__status_message" && contract.outputs[2].id == "p2",
            "only exposed results become business outputs");

    // Deleting a bound output and changing a bound input type are rejected as one
    // transaction; the stored configuration must stay untouched.
    const auto consumerTool = intTool("test.consumer", {"input"});
    const auto consumer     = editor.addNode("flow_1", consumerTool, {0, 1});
    require(editor.setBinding("flow_1", consumer, "input", {BindingMode::Upstream, Missing{}, SourcePort{id, "p2"}}),
            "binding the exposed expression result failed");
    const auto revision = editor.project().flows.front().nodes.front().executionRevision;
    auto       narrowed = config;
    narrowed.program.erase(narrowed.program.begin());
    bool rejected = false;
    try
    {
        editor.setExpressionConfig("flow_1", id, narrowed, expression);
    }
    catch (const ContractError &)
    {
        rejected = true;
    }
    require(rejected, "deleting a connected output must be rejected");
    require(editor.project().flows.front().nodes.front().executionRevision == revision,
            "a rejected transaction must not bump the revision");

    auto retyped           = config;
    retyped.variables[0].type = {Type::Float};
    rejected               = false;
    try
    {
        editor.setExpressionConfig("flow_1", id, retyped, expression);
    }
    catch (const ContractError &)
    {
        rejected = true;
    }
    require(rejected == false, "an unconnected variable may change type");

    // Forward references and duplicate symbols are rejected before commit.
    auto forward                     = sampleExpression();
    forward.program.back().symbol    = "later";
    forward.program.front().expression = "later + 1";
    bool forwardRejected               = false;
    try
    {
        editor.setExpressionConfig("flow_1", id, forward, expression);
    }
    catch (const ContractError &)
    {
        forwardRejected = true;
    }
    require(forwardRejected, "forward references must be rejected");

    const auto condition = conditionTool();
    const auto conditionId = editor.addNode("flow_1", condition, {0, 2});
    auto       conditionConfig = sampleExpression();
    conditionConfig.program.back().exposed = true;
    bool conditionRejected = false;
    try
    {
        editor.setExpressionConfig("flow_1", conditionId, conditionConfig, condition);
    }
    catch (const ContractError &)
    {
        conditionRejected = true;
    }
    require(conditionRejected, "condition results must stay hidden");

    auto validCondition     = sampleExpression();
    validCondition.program  = {validCondition.program.back()};
    validCondition.program.front().exposed = false;
    require(editor.setExpressionConfig("flow_1", conditionId, validCondition, condition),
            "a single hidden Bool condition must commit");
    const auto conditionContract = nodeContract(editor.project().flows.front().nodes.at(2), condition);
    require(conditionContract.outputs.size() == 4 && conditionContract.outputs[2].id == "on_true"
                && conditionContract.outputs[3].type == TypeSpec{Type::Bool},
            "condition outputs are fixed regardless of the formula list");
}

void guardAndSelect()
{
    const auto flag     = boolTool();
    const auto counter  = intTool("test.counter", {"input"});
    const auto provider = intTool("test.provider");
    const auto select   = selectTool();

    std::atomic<int>  flagMode{0}; // 0 false, 1 true, 2 failure
    std::atomic<int>  executions{0};
    const auto        flagRun = [&](const Values &) -> ToolResult
    {
        if (flagMode.load() == 2)
            return ToolResult{{3, "条件失败"}, {{"value", Missing{}}}};
        return ToolResult{{}, {{"value", flagMode.load() == 1}}};
    };
    ToolRegistry registry;
    install(registry, flag, flagRun);
    install(registry, counter,
            [&](const Values &inputs)
            {
                ++executions;
                return ToolResult{{}, {{"value", std::get<std::int64_t>(inputs.at("input"))}}};
            });
    install(registry, provider, [](const Values &) { return ToolResult{{}, {{"value", std::int64_t{7}}}}; });
    registerSelectTools(registry);
    registry.addDefinition(select);

    auto source   = node(1, flag, 0);
    auto providerNode = node(3, provider, 0);
    auto work     = node(2, counter, 1);
    work.inputs["input"]   = {BindingMode::Upstream, Missing{}, SourcePort{3, "value"}};
    work.executionGuard    = SourcePort{1, "value"};
    ProjectDefinition project;
    project.flows.front().nodes = {source, providerNode, work};

    const auto started = [](RuntimeEngine &engine, const ProjectDefinition &project, const ToolRegistry &registry)
    {
        const auto status = engine.start(project, registry, RunMode::Once);
        require(static_cast<bool>(status), "runtime start failed: " + status.message);
    };
    RuntimeEngine engine(2);
    started(engine, project, registry);
    auto result = finish(engine);
    require(executions.load() == 0, "a false guard must not execute the formal tool");
    require(snapshot(result, 2)->disposition == NodeDisposition::Skipped,
            "a false guard publishes the skipped terminal state");
    require(snapshot(result, 2)->status.message.find("条件为假") != std::string::npos,
            "skipped message must explain the guard");
    require(isMissing(snapshot(result, 2)->outputs.at("value")), "skipped outputs stay missing");

    flagMode.store(1);
    started(engine, project, registry);
    result = finish(engine);
    require(executions.load() == 1, "a true guard must execute exactly once");
    require(snapshot(result, 2)->disposition == NodeDisposition::Succeeded,
            "a true guard publishes success");

    // A failing guard source blocks the node instead of falling into the false path.
    flagMode.store(2);
    started(engine, project, registry);
    result = finish(engine);
    require(snapshot(result, 2)->disposition == NodeDisposition::Blocked,
            "a failed guard blocks the node");
    require(snapshot(result, 2)->status.message.find("阻塞") != std::string::npos,
            "blocked message must be explicit");
    require(executions.load() == 1, "a blocked node never executes the tool");

    // Merge: the unselected branch may be skipped, the selected one must exist.
    RuntimeEngine merge(2);
    ToolRegistry  mergeRegistry;
    install(mergeRegistry, flag, flagRun);
    install(mergeRegistry, provider, [](const Values &) { return ToolResult{{}, {{"value", std::int64_t{7}}}}; });
    install(mergeRegistry, counter, [&](const Values &inputs)
            { return ToolResult{{}, {{"value", std::get<std::int64_t>(inputs.at("input"))}}}; });
    registerSelectTools(mergeRegistry);
    mergeRegistry.addDefinition(select);
    flagMode.store(0);
    auto selector = node(1, flag, 0);
    auto branchA  = node(2, provider, 1);
    branchA.executionGuard = SourcePort{1, "value"}; // skipped while the flag is false
    auto branchB = node(3, provider, 1);
    auto merge_  = node(4, select, 2);
    merge_.selectType = TypeSpec{Type::Int};
    merge_.inputs["selector"] = {BindingMode::Upstream, Missing{}, SourcePort{1, "value"}};
    merge_.inputs["if_true"]  = {BindingMode::Upstream, Missing{}, SourcePort{2, "value"}};
    merge_.inputs["if_false"] = {BindingMode::Upstream, Missing{}, SourcePort{3, "value"}};
    ProjectDefinition mergeProject;
    mergeProject.flows.front().nodes = {selector, branchA, branchB, merge_};
    started(merge, mergeProject, mergeRegistry);
    const auto merged = finish(merge);
    require(snapshot(merged, 2)->disposition == NodeDisposition::Skipped, "unselected branch must skip");
    require(snapshot(merged, 4)->disposition == NodeDisposition::Succeeded,
            "select must accept a skipped unselected branch");
    require(std::get<std::int64_t>(snapshot(merged, 4)->outputs.at("value")) == 7,
            "select forwards the chosen branch value");
}

void projectRoundtrip()
{
    QTemporaryDir directory;
    require(directory.isValid(), "temporary directory unavailable");
    const auto expression = expressionTool();
    const auto condition  = conditionTool();
    const auto select     = selectTool();
    ToolRegistry registry;
    registry.addDefinition(expression);
    registry.addDefinition(condition);
    registry.addDefinition(select);
    ProjectEditor editor;
    const auto    expressionId = editor.addNode("flow_1", expression, {});
    auto expressionConfig                  = sampleExpression();
    expressionConfig.program.back().exposed = true; // connectable Bool result for the guard
    require(editor.setExpressionConfig("flow_1", expressionId, expressionConfig, expression),
            "expression configuration commit failed");
    const auto conditionId = editor.addNode("flow_1", condition, {0, 1});
    auto       conditionConfig = sampleExpression();
    conditionConfig.program    = {conditionConfig.program.back()};
    conditionConfig.program.front().exposed = false;
    require(editor.setExpressionConfig("flow_1", conditionId, conditionConfig, condition),
            "condition configuration commit failed");
    bool guardRejected = false;
    try
    {
        editor.setExecutionGuard("flow_1", conditionId, SourcePort{expressionId, "p2"});
    }
    catch (const ContractError &)
    {
        guardRejected = true;
    }
    require(guardRejected, "an Int output cannot act as an execution guard");
    require(editor.setExecutionGuard("flow_1", conditionId, SourcePort{expressionId, "p3"}),
            "Boolean guard binding failed");
    const auto selectId = editor.addNode("flow_1", select, {0, 2});
    require(editor.setSelectType("flow_1", selectId, {Type::Float}, select), "select type commit failed");

    ProjectStorage storage(registry);
    auto           project = editor.project();
    const auto     path    = std::filesystem::path(directory.path().toUtf8().toStdString()) / "models.vrt.json";
    storage.save(project, path);
    const auto loaded = storage.load(path);
    for (const auto &diagnostic : loaded.diagnostics)
        require(diagnostic.code != ProjectStorageCode::SchemaChanged,
                "roundtrip introduced a schema diagnostic: " + diagnostic.message);
    const auto &nodes = loaded.project.flows.front().nodes;
    require(nodes.at(0).expression && nodes.at(0).expression->program.size() == 2,
            "expression program did not survive the roundtrip");
    require(nodes.at(0).expression->nextPortSeq == 4, "port sequence lost");
    require(nodes.at(1).executionGuard && nodes.at(1).executionGuard->port == "p3",
            "execution guard did not survive the roundtrip");
    require(nodes.at(2).selectType && *nodes.at(2).selectType == TypeSpec{Type::Float},
            "select type did not survive the roundtrip");
}
} // namespace

int main()
try
{
    materialisationAndTransactions();
    guardAndSelect();
    projectRoundtrip();
    std::cout << "NODE_MODEL_PASS materialisation, transactions, gating, merge and format4 roundtrip" << std::endl;
    return 0;
}
catch (const std::exception &error)
{
    std::cerr << "NODE_MODEL_FAIL " << error.what() << std::endl;
    return 1;
}
