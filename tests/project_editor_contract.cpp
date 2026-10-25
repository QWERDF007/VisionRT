#include "core/ProjectEditor.h"
#include "core/ToolRegistry.h"
#include "core/DefinitionJson.h"

#include <algorithm>
#include <array>
#include <cassert>
#include <limits>
#include <iostream>
#include <string_view>

using namespace visionrt;

namespace {
template<class F>
void rejects(F &&operation, std::string_view reason = {})
{
    bool rejected = false;
    try
    {
        operation();
    }
    catch (const ContractError &error)
    {
        assert(std::string_view(error.what()).find(reason) != std::string_view::npos);
        rejected = true;
    }
    assert(rejected);
}

ToolDefinition integerTool()
{
    ToolDefinition tool;
    tool.id          = "test.integer";
    tool.displayName = "整数";
    tool.kind        = ToolKind::Cpp;
    tool.inputs      = {
        { "first", "输入一", {Type::Int}, std::int64_t{10}},
        {"second", "输入二", {Type::Int}, std::int64_t{20}}
    };
    tool.initParams = {
        {"seed", "初值", {Type::Int}, std::int64_t{1}}
    };
    tool.outputs = {
        {"value", "输出", {Type::Int}}
    };
    return tool;
}

const NodeDefinition &node(const ProjectEditor &editor, NodeId id)
{
    for (const auto &flow : editor.project().flows)
        for (const auto &item : flow.nodes)
            if (item.id == id)
                return item;
    throw ContractError("Test node absent");
}

void portProjectionRetainsIdentityAndRejectsInvalidReferences()
{
    auto tool = integerTool();
    tool.outputs.push_back({"other", "另一输出", {Type::Int}});
    ProjectEditor editor;
    const auto source = editor.addNode("flow_1", tool, {0, 0});
    const auto target = editor.addNode("flow_1", tool, {0, 1});
    const auto branch = editor.addNode("flow_1", tool, {0, 2});
    editor.setBinding("flow_1", target, "first", {BindingMode::Upstream, Missing{}, SourcePort{source, "value"}});
    editor.setBinding("flow_1", target, "second", {BindingMode::Upstream, Missing{}, SourcePort{source, "other"}});
    editor.setBinding("flow_1", branch, "first", {BindingMode::Upstream, Missing{}, SourcePort{source, "value"}});
    editor.setSource("flow_1", branch, "second", SourcePort{target, "value"});
    const auto projection = portRelations(editor.project().flows.front());
    assert(projection.errors.empty());
    assert((projection.active == std::vector<PortRelation>{
        {{source, "value"}, {target, "first"}},
        {{source, "other"}, {target, "second"}},
        {{source, "value"}, {branch, "first"}}
    }));
    assert(dependencyEdges(editor.project().flows.front()).size() == 2);
    assert(editor.referenceCount("flow_1", {source, "value"}) == 2);
    assert(editor.referenceCount("flow_1", {source, "other"}) == 1);

    auto flow = editor.project().flows.front();
    flow.nodes[1].inputs.at("first").source = SourcePort{source, "missing"};
    const auto missingPort = portRelations(flow);
    assert(missingPort.active.size() == 2 && missingPort.errors.size() == 1);
    assert(missingPort.errors.front().find(".missing") != std::string::npos);
    rejects([&] { dependencyEdges(flow); });
    flow.nodes[1].inputs.at("first").source = SourcePort{999, "value"};
    assert(portRelations(flow).active.size() == 2 && portRelations(flow).errors.size() == 1);
    rejects([&] { dependencyEdges(flow); });

    flow = editor.project().flows.front();
    flow.nodes[0].signatures.outputs.at("value") = {Type::Array, DType::Float32, 2};
    const auto incompatible = portRelations(flow);
    assert(incompatible.active.size() == 1 && incompatible.errors.size() == 2);
    assert((incompatible.active.front() == PortRelation{{source, "other"}, {target, "second"}}));
    rejects([&] { dependencyEdges(flow); });
    flow.nodes[0].signatures.outputs.erase("value");
    assert(portRelations(flow).active.size() == 1 && portRelations(flow).errors.size() == 2);
    flow.nodes.push_back(flow.nodes.front());
    rejects([&] { portRelations(flow); });
    assert(portRelations(editor.project().flows.front()).active == projection.active);
}

void cycleRejectionIsAtomic()
{
    ProjectEditor editor;
    const auto    tool  = integerTool();
    const auto    a     = editor.addNode("flow_1", tool, {0, 1});
    const auto    b     = editor.addNode("flow_1", tool, {0, 2});
    const auto    c     = editor.addNode("flow_1", tool, {0, 3});
    const auto    spare = editor.addNode("flow_1", tool, {0, 0});
    editor.setBinding("flow_1", b, "first",
                      {
                          BindingMode::Upstream, std::int64_t{10},
                           SourcePort{a, "value"}
    });
    editor.setBinding("flow_1", b, "second",
                      {
                          BindingMode::Upstream, std::int64_t{20},
                           SourcePort{a, "value"}
    });
    editor.setBinding("flow_1", c, "first",
                      {
                          BindingMode::Upstream, std::int64_t{10},
                           SourcePort{b, "value"}
    });
    editor.setBinding("flow_1", a, "first",
                      {
                          BindingMode::Upstream, std::int64_t{10},
                           SourcePort{spare, "value"}
    });
    assert(dependencyEdges(editor.project().flows.front()).size() == 3);
    assert(editor.referenceCount("flow_1", {a, "value"}) == 2);
    editor.markSaved();
    const auto execution = node(editor, a).executionRevision;
    const auto structure = editor.project().flows.front().structureRevision;
    rejects([&] { editor.setSource("flow_1", a, "first", SourcePort{c, "value"}); });
    assert(!editor.project().dirty);
    assert(node(editor, a).executionRevision == execution);
    assert(editor.project().flows.front().structureRevision == structure);
    assert((node(editor, a).inputs.at("first").source == SourcePort{spare, "value"}));
    const auto candidates = editor.compatibleSources("flow_1", a, "first");
    assert(std::find(candidates.begin(), candidates.end(), SourcePort{c, "value"}) == candidates.end());
    assert(std::find(candidates.begin(), candidates.end(), SourcePort{spare, "value"}) != candidates.end());
}

void inactiveDraftsOnlyDirty()
{
    ProjectEditor editor;
    const auto    tool      = integerTool();
    const auto    a         = editor.addNode("flow_1", tool, {0, 0});
    const auto    b         = editor.addNode("flow_1", tool, {0, 1});
    const auto    structure = editor.project().flows.front().structureRevision;
    editor.markSaved();
    assert(editor.setSource("flow_1", b, "first", SourcePort{a, "value"}));
    assert(editor.project().dirty);
    assert(node(editor, b).executionRevision == 0);
    assert(editor.project().flows.front().structureRevision == structure);
    assert(dependencyEdges(editor.project().flows.front()).empty());
    editor.setMode("flow_1", b, "first", BindingMode::Upstream);
    assert(node(editor, b).executionRevision == 1);
    assert(editor.referenceCount("flow_1", {a, "value"}) == 1);
    editor.setSource("flow_1", a, "first", SourcePort{b, "value"});
    assert(node(editor, a).executionRevision == 0);
    editor.markSaved();
    rejects([&] { editor.setMode("flow_1", a, "first", BindingMode::Upstream); });
    assert(!editor.project().dirty);
    assert(node(editor, a).inputs.at("first").mode == BindingMode::Literal);
    assert((node(editor, a).inputs.at("first").source == SourcePort{b, "value"}));
    editor.markSaved();
    editor.setLiteral("flow_1", b, "first", std::int64_t{99});
    assert(editor.project().dirty);
    assert(node(editor, b).executionRevision == 1);
    assert(editor.project().flows.front().structureRevision == structure + 1);
    editor.markSaved();
    assert(!editor.setLiteral("flow_1", b, "first", std::int64_t{99}));
    assert(!editor.project().dirty);
    editor.unbind("flow_1", b, "first");
    assert(node(editor, b).inputs.at("first").mode == BindingMode::Upstream);
    assert(!node(editor, b).inputs.at("first").source);
    assert(std::get<std::int64_t>(node(editor, b).inputs.at("first").literal) == 99);
    editor.setMode("flow_1", b, "first", BindingMode::Literal);
    assert(node(editor, b).executionRevision == 3);
    editor.setInitParam("flow_1", b, "seed", std::int64_t{42});
    assert(node(editor, b).executionRevision == 4);
    editor.markSaved();
    rejects([&] { editor.setLiteral("flow_1", b, "first", true); });
    assert(!editor.project().dirty);
    assert(node(editor, b).executionRevision == 4);

    auto arrays                = integerTool();
    arrays.inputs.front().type = {Type::Array, DType::Int64, 1};
    const auto makeValue       = []
    {
        return Array(NestedVector<std::int64_t, 1>{2, 9});
    };
    arrays.inputs.front().defaultValue = makeValue();
    const auto arrayNode               = editor.addNode("flow_1", arrays, {2, 0});
    editor.markSaved();
    assert(!editor.setLiteral("flow_1", arrayNode, "first", makeValue()));
    assert(!editor.project().dirty);
    assert(node(editor, arrayNode).executionRevision == 0);
}

void copiesRemapInternalSourcesAndPreserveOriginals()
{
    ProjectEditor editor;
    auto          tool = integerTool();
    tool.initParams.push_back({"source_flow_id", "来源流程", {Type::String}, std::string("flow_1")});
    tool.initParams.push_back({"source_node_seq", "来源节点", {Type::String}, std::string("0")});
    const auto external   = editor.addNode("flow_1", tool, {0, 0});
    const auto a          = editor.addNode("flow_1", tool, {0, 1});
    const auto b          = editor.addNode("flow_1", tool, {0, 2});
    const auto downstream = editor.addNode("flow_1", tool, {0, 3});
    editor.setBinding("flow_1", a, "first",
                      {
                          BindingMode::Upstream, std::int64_t{71},
                           SourcePort{external, "value"}
    });
    editor.setBinding("flow_1", b, "first",
                      {
                          BindingMode::Upstream, std::int64_t{72},
                           SourcePort{a, "value"}
    });
    editor.setSource("flow_1", b, "second", SourcePort{external, "value"});
    editor.setBinding("flow_1", downstream, "first",
                      {
                          BindingMode::Upstream, std::int64_t{73},
                           SourcePort{b, "value"}
    });
    editor.setAlias("flow_1", a, "复制备注");
    editor.setInitParam("flow_1", a, "seed", std::int64_t{24});
    const auto snapshot = editor.copySelection("flow_1", std::array{a, b});
    editor.setAlias("flow_1", a, "已修改");
    editor.setLiteral("flow_1", a, "first", std::int64_t{99});
    const auto mapping = editor.pasteSelection("flow_1", snapshot, {0, 0});
    const auto newA    = mapping.at(a);
    const auto newB    = mapping.at(b);
    assert(newA != a && newB != b && newA != newB);
    assert(!node(editor, newA).inputs.at("first").source);
    assert(node(editor, newA).inputs.at("first").mode == BindingMode::Upstream);
    assert((node(editor, newB).inputs.at("first").source == SourcePort{newA, "value"}));
    assert(!node(editor, newB).inputs.at("second").source);
    assert((node(editor, downstream).inputs.at("first").source == SourcePort{b, "value"}));
    assert(node(editor, newA).alias == "复制备注");
    assert(std::get<std::int64_t>(node(editor, newA).initParams.at("seed")) == 24);
    assert(std::get<std::int64_t>(node(editor, newA).inputs.at("first").literal) == 71);
    assert(node(editor, newA).position.x == node(editor, newB).position.x);
    assert(node(editor, newB).position.y - node(editor, newA).position.y == 1);
    assert(node(editor, newA).position != node(editor, a).position);
    assert(node(editor, newA).executionRevision == 0);
    const auto  newFlow = editor.copyFlow("flow_1");
    const auto &copied  = editor.project().flows.back();
    assert(copied.id == newFlow && copied.id != "flow_1");
    assert(copied.name != editor.project().flows.front().name);
    assert(copied.runSelected);
    assert(copied.nodes.size() == editor.project().flows.front().nodes.size());
    assert((copied.nodes[1].inputs.at("first").source == SourcePort{copied.nodes[0].id, "value"}));
    assert(std::get<std::int64_t>(copied.nodes[1].initParams.at("seed")) == 24);
    assert(std::get<std::string>(copied.nodes[1].initParams.at("source_flow_id")) == "flow_1");
    assert(std::get<std::string>(copied.nodes[1].initParams.at("source_node_seq")) == "0");
}

void movementAndDeletionAreAtomic()
{
    ProjectEditor editor;
    const auto    tool = integerTool();
    const auto    a    = editor.addNode("flow_1", tool, {0, 0});
    const auto    b    = editor.addNode("flow_1", tool, {1, 0});
    const auto    blocker = editor.addNode("flow_1", tool, {2, 0});
    (void)blocker;
    const auto    near = editor.addNode("flow_1", tool, {2, 0});
    assert((node(editor, near).position == GridPoint{2, -1}));
    const auto    c    = editor.addNode("flow_1", tool, {2, 2});
    editor.setBinding("flow_1", c, "first",
                      {
                          BindingMode::Upstream, std::int64_t{10},
                           SourcePort{b, "value"}
    });
    editor.setSource("flow_1", near, "first", SourcePort{b, "value"});
    const auto structure = editor.project().flows.front().structureRevision;
    editor.markSaved();
    rejects([&] { editor.moveNodes("flow_1", std::array{a, b}, {1, 0}); });
    assert(!editor.project().dirty);
    assert((node(editor, a).position == GridPoint{0, 0}));
    assert((node(editor, b).position == GridPoint{1, 0}));
    editor.moveNodes("flow_1", std::array{a, b}, {0, 1});
    assert((node(editor, a).position == GridPoint{0, 1}));
    assert((node(editor, b).position == GridPoint{1, 1}));
    assert(node(editor, a).executionRevision == 0);
    assert(editor.project().flows.front().structureRevision == structure);
    editor.removeNodes("flow_1", std::array{a, b});
    assert(!node(editor, c).inputs.at("first").source);
    assert(!node(editor, near).inputs.at("first").source);
    assert(node(editor, c).executionRevision == 2);
    assert(node(editor, near).executionRevision == 0);
    assert(editor.project().flows.front().structureRevision == structure + 1);
    const auto next = editor.addNode("flow_1", tool, {0, 0});
    assert(next > near);
    editor.removeNodes("flow_1", std::array{next});
    assert(editor.addNode("flow_1", tool, {0, 0}) > next);
}

void flowsAndBusyGuard()
{
    ProjectEditor editor;
    const auto    tool   = integerTool();
    const auto    a      = editor.addNode("flow_1", tool, {0, 0});
    const auto    second = editor.addFlow("第二流程");
    editor.setRunSelected(second, false);
    editor.renameFlow(second, "改名");
    assert(!editor.project().flows.back().runSelected);
    editor.markSaved();
    rejects([&] { editor.renameFlow(second, "流程1"); });
    assert(!editor.project().dirty);
    assert(editor.removeFlow(second) == "flow_1");
    rejects([&] { editor.removeFlow("flow_1"); });
    const auto third = editor.addFlow("第三流程");
    assert(third != second);
    editor.markSaved();
    editor.setBusy(true);
    rejects([&] { editor.renameProject("busy"); }, "运行或管理期间不能修改工程配置");
    rejects([&] { editor.addFlow(); });
    rejects([&] { editor.renameFlow("flow_1", "busy"); });
    rejects([&] { editor.setRunSelected("flow_1", false); });
    rejects([&] { editor.removeFlow(third); });
    rejects([&] { editor.copyFlow("flow_1"); });
    rejects([&] { editor.addNode("flow_1", tool, {0, 0}); });
    rejects([&] { editor.removeNodes("flow_1", std::array{a}); });
    rejects([&] { editor.moveNodes("flow_1", std::array{a}, {1, 0}); });
    rejects([&] { (void)editor.copySelection("flow_1", std::array{a}); });
    rejects([&] { (void)editor.pasteSelection(third, CopiedSelection{}, {0, 0}); });
    rejects([&] { editor.setAlias("flow_1", a, "busy"); });
    rejects([&] { editor.setLiteral("flow_1", a, "first", std::int64_t{4}); });
    rejects([&] { editor.setInitParam("flow_1", a, "seed", std::int64_t{4}); });
    rejects([&] { editor.setMode("flow_1", a, "first", BindingMode::Upstream); });
    rejects([&] { editor.unbind("flow_1", a, "first"); });
    rejects([&] { editor.replaceProject({}); });
    assert(!editor.project().dirty);
    editor.setBusy(false);
    editor.setAlias("flow_1", a, "备注");
    assert(node(editor, a).title() == "备注-0");
    assert(node(editor, a).executionRevision == 0);
}

void exhaustedCountersAndCoordinateOverflowRejectWithoutChanges()
{
    ProjectEditor editor;
    const auto    tool = integerTool();
    const auto    a    = editor.addNode("flow_1", tool, {std::numeric_limits<int>::max(), 2});
    const auto    b    = editor.addNode("flow_1", tool, {0, 1});
    editor.markSaved();
    rejects([&] { editor.moveNodes("flow_1", std::array{a, b}, {1, 0}); });
    assert(!editor.project().dirty);
    assert((node(editor, b).position == GridPoint{0, 1}));
    const auto snapshot = editor.copySelection("flow_1", std::array{a, b});
    auto exhausted       = editor.project();
    exhausted.nextNodeId = std::numeric_limits<NodeId>::max();
    editor.replaceProject(exhausted);
    rejects([&] { editor.addNode("flow_1", tool, {4, 0}); });
    rejects([&] { (void)editor.pasteSelection("flow_1", snapshot, {0, 2}); });
    rejects([&] { editor.copyFlow("flow_1"); });
    assert(!editor.project().dirty);
    assert(editor.project().nextNodeId == std::numeric_limits<NodeId>::max());
    assert(editor.project().flows.size() == 1 && editor.project().flows.front().nodes.size() == 2);
    exhausted.nextNodeId = 2;
    exhausted.nextFlowId = std::numeric_limits<std::uint64_t>::max();
    editor.replaceProject(exhausted);
    rejects([&] { editor.addFlow(); });
    assert(!editor.project().dirty);
    exhausted.nextFlowId                                    = 2;
    exhausted.flows.front().nodes.front().executionRevision = std::numeric_limits<std::uint64_t>::max();
    editor.replaceProject(exhausted);
    rejects([&] { editor.setLiteral("flow_1", a, "first", std::int64_t{123}); });
    rejects([&] { editor.setInitParam("flow_1", a, "seed", std::int64_t{123}); });
    assert(!editor.project().dirty);
    assert(std::get<std::int64_t>(node(editor, a).inputs.at("first").literal) == 10);
    assert(std::get<std::int64_t>(node(editor, a).initParams.at("seed")) == 1);
    exhausted.flows.front().nodes.front().executionRevision = 0;
    exhausted.flows.front().structureRevision               = std::numeric_limits<std::uint64_t>::max();
    editor.replaceProject(exhausted);
    rejects(
        [&]
        {
            editor.setBinding("flow_1", a, "first",
                              {
                                  BindingMode::Upstream, std::int64_t{10},
                                   SourcePort{b, "value"}
            });
        });
    rejects([&] { editor.removeNodes("flow_1", std::array{a}); });
    assert(!editor.project().dirty);
    assert(!node(editor, a).inputs.at("first").source);
    // An inactive source is a persisted draft, not a graph/revision update.
    assert(editor.setSource("flow_1", a, "first", SourcePort{b, "value"}));
    assert(node(editor, a).executionRevision == 0);
}

void compatibilityAndRelationshipMenusUseEffectiveInputs()
{
    ProjectEditor editor;
    const auto    tool           = integerTool();
    const auto    a              = editor.addNode("flow_1", tool, {0, 0});
    const auto    b              = editor.addNode("flow_1", tool, {0, 2});
    auto          boolean        = integerTool();
    boolean.outputs.front().type = {Type::Bool};
    const auto incompatible      = editor.addNode("flow_1", boolean, {0, 1});
    const auto otherFlow         = editor.addFlow();
    const auto otherNode         = editor.addNode(otherFlow, tool, {0, 0});
    editor.markSaved();
    rejects([&] { editor.setSource("flow_1", b, "first", SourcePort{incompatible, "value"}); });
    rejects([&] { editor.setSource("flow_1", b, "first", SourcePort{otherNode, "value"}); });
    assert(!editor.project().dirty);
    const auto candidates = editor.compatibleSources("flow_1", b, "first");
    assert(std::find(candidates.begin(), candidates.end(), SourcePort{a, "value"}) != candidates.end());
    assert(std::find(candidates.begin(), candidates.end(), SourcePort{incompatible, "value"}) == candidates.end());
    assert(std::find(candidates.begin(), candidates.end(), SourcePort{otherNode, "value"}) == candidates.end());
    editor.setBinding("flow_1", b, "first",
                      {
                          BindingMode::Upstream, std::int64_t{10},
                           SourcePort{a, "value"}
    });
    const auto inputs = editor.compatibleInputs("flow_1", {a, "value"});
    assert(std::find(inputs.begin(), inputs.end(), InputPort{b, "first"}) == inputs.end());
    assert(std::find(inputs.begin(), inputs.end(), InputPort{b, "second"}) != inputs.end());
    const auto reverseInputs = editor.compatibleInputs("flow_1", {b, "value"});
    assert(std::find(reverseInputs.begin(), reverseInputs.end(), InputPort{a, "first"}) == reverseInputs.end());
    editor.setSource("flow_1", b, "second", SourcePort{a, "value"});
    assert(editor.referenceCount("flow_1", {a, "value"}) == 1);
    editor.setMode("flow_1", b, "second", BindingMode::Upstream);
    assert(editor.referenceCount("flow_1", {a, "value"}) == 2);
    assert((dependencyEdges(editor.project().flows.front())
            == std::vector<DependencyEdge>{
                {a, b}
    }));
}
void bindingEffectiveSourceDraftsAndOccupiedContract()
{
    ProjectEditor editor;
    const auto tool = integerTool();
    const auto a = editor.addNode("flow_1", tool, {0, 0});
    const auto c = editor.addNode("flow_1", tool, {1, 0});
    const auto b = editor.addNode("flow_1", tool, {0, 2});

    // 1. Literal with old draft source
    editor.setBinding("flow_1", b, "first",
                      {BindingMode::Literal, std::int64_t{42}, SourcePort{a, "value"}});
    assert(node(editor, b).inputs.at("first").mode == BindingMode::Literal);
    assert(node(editor, b).inputs.at("first").source == SourcePort(a, "value"));
    assert(!effectiveSource(node(editor, b).inputs.at("first")).has_value());

    // candidateInputsWithState must treat Literal with draft as Free (connected=false, occupied=false)
    const auto candsA = candidateInputsWithState(editor.project().flows.front(), {a, "value"});
    const auto itBFirstA = std::find_if(candsA.begin(), candsA.end(), [&](const auto &item) {
        return item.node == b && item.port == "first";
    });
    assert(itBFirstA != candsA.end());
    assert(!itBFirstA->connected && !itBFirstA->occupied);

    // compatibleInputs must include b.first
    const auto inputsC = editor.compatibleInputs("flow_1", {c, "value"});
    assert(std::find(inputsC.begin(), inputsC.end(), InputPort{b, "first"}) != inputsC.end());

    // bindFreeInput from c must succeed, overwriting old draft source and switching to Upstream
    assert(editor.bindFreeInput("flow_1", {c, "value"}, {b, "first"}));
    assert(node(editor, b).inputs.at("first").mode == BindingMode::Upstream);
    assert(node(editor, b).inputs.at("first").source == SourcePort(c, "value"));
    assert(effectiveSource(node(editor, b).inputs.at("first")).value() == SourcePort(c, "value"));

    // 2. Upstream occupied: candidateInputsWithState for a must report occupied=true
    const auto candsA_after = candidateInputsWithState(editor.project().flows.front(), {a, "value"});
    const auto itBFirstA_after = std::find_if(candsA_after.begin(), candsA_after.end(), [&](const auto &item) {
        return item.node == b && item.port == "first";
    });
    assert(itBFirstA_after != candsA_after.end());
    assert(!itBFirstA_after->connected && itBFirstA_after->occupied);

    // bindFreeInput from a while occupied must throw
    rejects([&] { editor.bindFreeInput("flow_1", {a, "value"}, {b, "first"}); }, "输入端口已关联上游");

    // Connected candidate: for c, it must be connected=true, occupied=false
    const auto candsC = candidateInputsWithState(editor.project().flows.front(), {c, "value"});
    const auto itBFirstC = std::find_if(candsC.begin(), candsC.end(), [&](const auto &item) {
        return item.node == b && item.port == "first";
    });
    assert(itBFirstC != candsC.end());
    assert(itBFirstC->connected && !itBFirstC->occupied);
}
} // namespace

void receiverOutputFollowsItsSource()
{
    const auto    receiver = parseToolDefinitionJson(R"json({
  "schema_version": 2,
  "tool_id": "visionrt.flow.receive",
  "display_name": "跨流程接收",
  "version": "1.0.0",
  "kind": "host",
  "entry": {"binding": "visionrt.receiver"},
  "init_params": [
    {"id": "source_flow_id", "name": "源流程", "type": "String", "default": ""},
    {"id": "source_node_seq", "name": "源节点", "type": "String", "default": ""},
    {"id": "source_port_id", "name": "源端口", "type": "String", "default": ""}
  ],
  "inputs": [],
  "outputs": [
    {"id": "value", "name": "数据", "type_from": {"flow": "source_flow_id", "node": "source_node_seq", "port": "source_port_id"}}
  ]
})json",
                                                     {}, {ToolOrigin::Builtin, {}});
    ProjectEditor editor;
    const auto sourceFlow = editor.addFlow("source");
    const auto sender = editor.addNode(sourceFlow, integerTool(), {});
    auto arrays = integerTool();
    arrays.id = "test.arrays";
    arrays.outputs.front().type = {Type::Array, DType::Float32, 2};
    const auto arraySender = editor.addNode(sourceFlow, arrays, {1, 0});
    const auto receive = editor.addNode("flow_1", receiver, {});
    assert(!node(editor, receive).signatures.outputs.contains("value"));
    editor.setInitParam("flow_1", receive, "source_flow_id", std::string(sourceFlow));
    editor.setInitParam("flow_1", receive, "source_node_seq", std::string(std::to_string(sender)));
    editor.setInitParam("flow_1", receive, "source_port_id", std::string("value"));
    assert(node(editor, receive).signatures.outputs.at("value") == TypeSpec{Type::Int});
    const auto sink = editor.addNode("flow_1", integerTool(), {0, 10});
    editor.setSource("flow_1", sink, "first", SourcePort{receive, "value"});
    editor.setMode("flow_1", sink, "first", BindingMode::Upstream);
    assert(dependencyEdges(editor.project().flows.front()).size() == 1);
    const auto otherSink = editor.addNode("flow_1", integerTool(), {10, 10});
    const auto replacement = editor.addNode("flow_1", integerTool(), {10, 0});
    editor.setBinding("flow_1", otherSink, "first",
                      {BindingMode::Upstream, Missing{}, SourcePort{receive, "value"}});
    const auto revision = node(editor, receive).executionRevision;
    for (const auto &value :
         {std::string("0") + std::to_string(sender), "+" + std::to_string(sender), " " + std::to_string(sender),
          std::to_string(sender) + " ", std::string("18446744073709551616")})
    {
        editor.setInitParam("flow_1", receive, "source_node_seq", value);
        assert(!node(editor, receive).signatures.outputs.contains("value"));
        assert(node(editor, receive).outputTypeErrors.at("value").find("源节点序号") != std::string::npos);
    }
    editor.setInitParam("flow_1", receive, "source_node_seq", std::string(std::to_string(arraySender)));
    assert(node(editor, receive).signatures.outputs.at("value") == arrays.outputs.front().type);
    assert(node(editor, receive).executionRevision > revision);
    rejects([&] { dependencyEdges(editor.project().flows.front()); });
    const auto sources = editor.compatibleSources("flow_1", sink, "first");
    assert(std::find(sources.begin(), sources.end(), SourcePort{replacement, "value"}) != sources.end());
    const auto inputs = editor.compatibleInputs("flow_1", {replacement, "value"});
    assert(std::find(inputs.begin(), inputs.end(), InputPort{sink, "second"}) != inputs.end());
    assert(std::find(inputs.begin(), inputs.end(), InputPort{sink, "first"}) == inputs.end());
    assert(editor.setSource("flow_1", sink, "first", SourcePort{replacement, "value"}));
    assert(portRelations(editor.project().flows.front()).errors.size() == 1);
    rejects([&] { dependencyEdges(editor.project().flows.front()); });
    const auto beforeCycle = node(editor, replacement).inputs.at("first").source;
    rejects([&] { editor.setBinding("flow_1", replacement, "first",
                    {BindingMode::Upstream, Missing{}, SourcePort{sink, "value"}}); });
    assert(node(editor, replacement).inputs.at("first").source == beforeCycle);
    assert(editor.unbind("flow_1", otherSink, "first"));
    assert(portRelations(editor.project().flows.front()).errors.empty());
    assert(dependencyEdges(editor.project().flows.front()).size() == 1);
    editor.setInitParam("flow_1", receive, "source_port_id", std::string("missing"));
    assert(!node(editor, receive).signatures.outputs.contains("value"));
    editor.setInitParam("flow_1", receive, "source_port_id", std::string("value"));
    assert(node(editor, receive).signatures.outputs.at("value") == arrays.outputs.front().type);
    editor.renameFlow(sourceFlow, "renamed");
    assert(node(editor, receive).signatures.outputs.at("value") == arrays.outputs.front().type);
    editor.setAlias(sourceFlow, arraySender, "new source");
    assert(node(editor, receive).signatures.outputs.at("value") == arrays.outputs.front().type);
    const auto copied = editor.pasteSelection(
        sourceFlow, editor.copySelection("flow_1", std::array{receive}), {4, 0});
    assert(!node(editor, copied.at(receive)).signatures.outputs.contains("value"));
    const auto chainFlow = editor.addFlow("chain");
    const auto chain = editor.addNode(chainFlow, receiver, {});
    editor.setInitParam(chainFlow, chain, "source_flow_id", std::string("flow_1"));
    editor.setInitParam(chainFlow, chain, "source_node_seq", std::string(std::to_string(receive)));
    editor.setInitParam(chainFlow, chain, "source_port_id", std::string("value"));
    assert(node(editor, chain).signatures.outputs.at("value") == arrays.outputs.front().type);
    auto exhausted = editor.project();
    for (auto &flow : exhausted.flows)
        for (auto &item : flow.nodes)
            if (item.id == receive) item.executionRevision = std::numeric_limits<std::uint64_t>::max();
    ProjectEditor atomic(std::move(exhausted));
    rejects([&] { atomic.removeNodes(sourceFlow, std::array{arraySender}); });
    assert(node(atomic, arraySender).id == arraySender);
    assert(node(atomic, receive).signatures.outputs.at("value") == arrays.outputs.front().type);
    editor.removeNodes(sourceFlow, std::array{arraySender});
    assert(!node(editor, receive).signatures.outputs.contains("value"));
    assert(!node(editor, chain).signatures.outputs.contains("value"));
    editor.setInitParam("flow_1", receive, "source_flow_id", std::string(chainFlow));
    editor.setInitParam("flow_1", receive, "source_node_seq", std::string(std::to_string(chain)));
    assert(!node(editor, receive).signatures.outputs.contains("value"));
    assert(!node(editor, chain).signatures.outputs.contains("value"));
}

int main()
{
    try
    {
        receiverOutputFollowsItsSource();
    }
    catch (const std::exception &error)
    {
        std::cerr << error.what() << std::endl;
        return 1;
    }
    portProjectionRetainsIdentityAndRejectsInvalidReferences();








    cycleRejectionIsAtomic();
    inactiveDraftsOnlyDirty();
    copiesRemapInternalSourcesAndPreserveOriginals();
    movementAndDeletionAreAtomic();
    flowsAndBusyGuard();
    exhaustedCountersAndCoordinateOverflowRejectWithoutChanges();
    compatibilityAndRelationshipMenusUseEffectiveInputs();
    bindingEffectiveSourceDraftsAndOccupiedContract();
}
