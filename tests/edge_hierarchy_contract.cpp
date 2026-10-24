#include "core/ProjectEditor.h"
#include <cassert>
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
        if (!reason.empty())
        {
            assert(std::string_view(error.what()).find(reason) != std::string_view::npos);
        }
        rejected = true;
    }
    assert(rejected);
}

ToolDefinition makeTool(std::string id)
{
    ToolDefinition tool;
    tool.id          = std::move(id);
    tool.displayName = "测试工具";
    tool.kind        = ToolKind::Cpp;
    tool.inputs      = {
        {"in_val", "输入", {Type::Int}, std::int64_t{0}}
    };
    tool.outputs = {
        {"out_val", "输出", {Type::Int}}
    };
    return tool;
}
} // namespace

int main()
{
    std::cout << "[TEST] Running edge_hierarchy_contract..." << std::endl;
    ProjectEditor editor;

    auto toolA = makeTool("tool.a");
    auto toolB = makeTool("tool.b");
    auto toolC = makeTool("tool.c");

    // Add node A at (0, 0)
    const auto nodeA = editor.addNode("flow_1", toolA, {0, 0});
    // Add node B at (2, 0) - same row
    const auto nodeB = editor.addNode("flow_1", toolB, {2, 0});
    // Add node C at (0, 3) - downstream
    const auto nodeC = editor.addNode("flow_1", toolC, {0, 3});

    const auto &flowDef = editor.project().flows.front();
    const auto &defA = *std::find_if(flowDef.nodes.begin(), flowDef.nodes.end(), [&](const auto &n) { return n.id == nodeA; });
    const auto &defB = *std::find_if(flowDef.nodes.begin(), flowDef.nodes.end(), [&](const auto &n) { return n.id == nodeB; });
    const auto &defC = *std::find_if(flowDef.nodes.begin(), flowDef.nodes.end(), [&](const auto &n) { return n.id == nodeC; });

    assert(defA.position.y == 0);
    assert(defB.position.y == 0);
    assert(defC.position.y == 3);

    // E01: Same row A and B cannot connect
    {
        auto sourcesForB = editor.compatibleSources("flow_1", nodeB, "in_val");
        assert(std::none_of(sourcesForB.begin(), sourcesForB.end(), [&](const auto &s) { return s.node == nodeA; }));

        auto inputsForA = editor.compatibleInputs("flow_1", {nodeA, "out_val"});
        assert(std::none_of(inputsForA.begin(), inputsForA.end(), [&](const auto &i) { return i.node == nodeB; }));

        rejects([&] {
            editor.setBinding("flow_1", nodeB, "in_val", {BindingMode::Upstream, Missing{}, SourcePort{nodeA, "out_val"}});
        }, "只能连接上级节点的输出");

        rejects([&] {
            editor.bindFreeInput("flow_1", {nodeA, "out_val"}, {nodeB, "in_val"});
        }, "只能连接上级节点的输出");
    }

    // E02 & E04: A -> C (upstream to downstream across rows) is allowed
    {
        auto sourcesForC = editor.compatibleSources("flow_1", nodeC, "in_val");
        assert(std::any_of(sourcesForC.begin(), sourcesForC.end(), [&](const auto &s) { return s.node == nodeA; }));

        auto inputsForA = editor.compatibleInputs("flow_1", {nodeA, "out_val"});
        assert(std::any_of(inputsForA.begin(), inputsForA.end(), [&](const auto &i) { return i.node == nodeC; }));

        bool bound = editor.setBinding("flow_1", nodeC, "in_val", {BindingMode::Upstream, Missing{}, SourcePort{nodeA, "out_val"}});
        assert(bound);
    }

    // E03: Reverse connection C -> A is rejected
    {
        auto sourcesForA = editor.compatibleSources("flow_1", nodeA, "in_val");
        assert(std::none_of(sourcesForA.begin(), sourcesForA.end(), [&](const auto &s) { return s.node == nodeC; }));

        rejects([&] {
            editor.setBinding("flow_1", nodeA, "in_val", {BindingMode::Upstream, Missing{}, SourcePort{nodeC, "out_val"}});
        }, "只能连接上级节点的输出");
    }

    // E10: Move nodes with hierarchy validation
    {
        // Moving A downwards by (0, 4) puts A at y=4, which is lower than C (y=3) -> must reject
        rejects([&] {
            editor.moveNodes("flow_1", std::array{nodeA}, {0, 4});
        }, "移动后连接不再满足上下层关系");

        // A should still be at y=0
        const auto &currentFlow = editor.project().flows.front();
        const auto &curA = *std::find_if(currentFlow.nodes.begin(), currentFlow.nodes.end(), [&](const auto &n) { return n.id == nodeA; });
        assert(curA.position.y == 0);

        // Moving both A and C downwards by (1, 2) preserves A.y(2) < C.y(5) -> should succeed
        bool movedBoth = editor.moveNodes("flow_1", std::array{nodeA, nodeC}, {1, 2});
        assert(movedBoth);

        const auto &afterFlow = editor.project().flows.front();
        const auto &afterA = *std::find_if(afterFlow.nodes.begin(), afterFlow.nodes.end(), [&](const auto &n) { return n.id == nodeA; });
        const auto &afterC = *std::find_if(afterFlow.nodes.begin(), afterFlow.nodes.end(), [&](const auto &n) { return n.id == nodeC; });
        assert(afterA.position.x == 1 && afterA.position.y == 2);
        assert(afterC.position.x == 1 && afterC.position.y == 5);
    }

    std::cout << "[PASS] All edge hierarchy contract tests passed successfully!" << std::endl;
    return 0;
}
