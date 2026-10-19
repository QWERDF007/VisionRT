#include "core/ProjectEditor.h"

#include <algorithm>
#include <array>
#include <cassert>
#include <limits>

using namespace visionrt;

namespace {
template<class F>
void rejects(F &&operation)
{
    bool rejected = false;
    try
    {
        operation();
    }
    catch (const ContractError &)
    {
        rejected = true;
    }
    assert(rejected);
}

ToolDefinition integerTool()
{
    ToolDefinition tool;
    tool.id          = "test.integer";
    tool.displayName = "整数";
    tool.kind        = ToolKind::CppFunction;
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

void cycleRejectionIsAtomic()
{
    ProjectEditor editor;
    const auto    tool  = integerTool();
    const auto    a     = editor.addNode("flow_1", tool, {0, 0});
    const auto    b     = editor.addNode("flow_1", tool, {0, 1});
    const auto    c     = editor.addNode("flow_1", tool, {0, 2});
    const auto    spare = editor.addNode("flow_1", tool, {1, 0});
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
    const auto    b         = editor.addNode("flow_1", tool, {1, 0});
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
    arrays.inputs.front().type = {Type::NumericArray, DType::Int64, 1, Layout::Dense};
    const std::array<std::int64_t, 2> values{2, 9};
    const auto                        makeValue = [&]
    {
        return NumericArray(DType::Int64, 1, Layout::Dense,
                            {NumericBuffer::copy(DType::Int64, {2}, std::as_bytes(std::span(values)))});
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
    tool.initParams.push_back({"source_flow_name", "来源流程", {Type::String}, Utf8("流程1")});
    tool.initParams.push_back({"source_node_name", "来源节点", {Type::String}, Utf8("整数-0")});
    const auto external   = editor.addNode("flow_1", tool, {-1, 0});
    const auto a          = editor.addNode("flow_1", tool, {0, 0});
    const auto b          = editor.addNode("flow_1", tool, {0, 1});
    const auto downstream = editor.addNode("flow_1", tool, {0, 2});
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
    const auto mapping = editor.copyNodes("flow_1", std::array{a, b}, "flow_1", {0, 0});
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
    assert(std::get<Utf8>(copied.nodes[1].initParams.at("source_flow_name")).str() == "流程1");
    assert(std::get<Utf8>(copied.nodes[1].initParams.at("source_node_name")).str() == "整数-0");
}

void movementAndDeletionAreAtomic()
{
    ProjectEditor editor;
    const auto    tool = integerTool();
    const auto    a    = editor.addNode("flow_1", tool, {0, 0});
    const auto    b    = editor.addNode("flow_1", tool, {1, 0});
    const auto    c    = editor.addNode("flow_1", tool, {2, 0});
    const auto    near = editor.addNode("flow_1", tool, {2, 0});
    assert((node(editor, near).position == GridPoint{2, -1}));
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
    rejects([&] { editor.renameProject("busy"); });
    rejects([&] { editor.addFlow(); });
    rejects([&] { editor.renameFlow("flow_1", "busy"); });
    rejects([&] { editor.setRunSelected("flow_1", false); });
    rejects([&] { editor.removeFlow(third); });
    rejects([&] { editor.copyFlow("flow_1"); });
    rejects([&] { editor.addNode("flow_1", tool, {0, 0}); });
    rejects([&] { editor.removeNodes("flow_1", std::array{a}); });
    rejects([&] { editor.moveNodes("flow_1", std::array{a}, {1, 0}); });
    rejects([&] { editor.copyNodes("flow_1", std::array{a}, third, {0, 0}); });
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
    const auto    a    = editor.addNode("flow_1", tool, {std::numeric_limits<int>::max(), 0});
    const auto    b    = editor.addNode("flow_1", tool, {0, 1});
    editor.markSaved();
    rejects([&] { editor.moveNodes("flow_1", std::array{a, b}, {1, 0}); });
    assert(!editor.project().dirty);
    assert((node(editor, b).position == GridPoint{0, 1}));
    auto exhausted       = editor.project();
    exhausted.nextNodeId = std::numeric_limits<NodeId>::max();
    editor.replaceProject(exhausted);
    rejects([&] { editor.addNode("flow_1", tool, {4, 0}); });
    rejects([&] { editor.copyNodes("flow_1", std::array{a, b}, "flow_1", {0, 2}); });
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
    const auto    b              = editor.addNode("flow_1", tool, {1, 0});
    auto          boolean        = integerTool();
    boolean.outputs.front().type = {Type::Bool};
    const auto incompatible      = editor.addNode("flow_1", boolean, {2, 0});
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
} // namespace

int main()
{
    cycleRejectionIsAtomic();
    inactiveDraftsOnlyDirty();
    copiesRemapInternalSourcesAndPreserveOriginals();
    movementAndDeletionAreAtomic();
    flowsAndBusyGuard();
    exhaustedCountersAndCoordinateOverflowRejectWithoutChanges();
    compatibilityAndRelationshipMenusUseEffectiveInputs();
}
