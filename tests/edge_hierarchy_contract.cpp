#include "core/ProjectEditor.h"
#include "core/ProjectStorage.h"
#include "runtime/RuntimeEngine.h"
#include <QTemporaryDir>
#include <cassert>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <string_view>
#include <thread>

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
            if (std::string_view(error.what()).find(reason) == std::string_view::npos)
            {
                std::cerr << "Expected reason: " << reason << ", got: " << error.what() << std::endl;
            }
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

class PassTool final : public IToolInstance
{
public:
    ToolStatus initialize(const Values &) override { return {}; }
    ToolResult execute(const Values &inputs) override { return toolResult(inputs, {}); }
    ToolStatus release() override { return {}; }
};
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

    // E11: Hand-crafted JSON with same-row or reverse-row binding:
    // - Loads successfully without throw
    // - Binding is preserved for repair (not discarded)
    // - Diagnostics report the hierarchy violation
    // - Flow and single-node execution are strictly rejected
    // - Repairing via node move restores execution ability
    {
        ToolRegistry registry;
        registry.addDefinition(toolA);
        registry.addDefinition(toolB);
        registry.registerCpp(toolA, [](const auto &) { return std::make_unique<PassTool>(); });
        registry.registerCpp(toolB, [](const auto &) { return std::make_unique<PassTool>(); });

        QTemporaryDir tmpDir;
        assert(tmpDir.isValid());
        const auto jsonPath = std::filesystem::path(tmpDir.path().toStdWString()) / "peer_test.vrt.json";

        // Create a valid project with node 1 at (0, 0) and node 2 at (5, 2), bound 1 -> 2
        ProjectEditor validEditor;
        const auto n1 = validEditor.addNode("flow_1", toolA, {0, 0});
        const auto n2 = validEditor.addNode("flow_1", toolB, {5, 2});
        validEditor.setBinding("flow_1", n2, "in_val", {BindingMode::Upstream, Missing{}, SourcePort{n1, "out_val"}});

        auto projectToSave = validEditor.project();
        ProjectStorage storage(registry);
        storage.save(projectToSave, jsonPath);

        // Modify node 2 position in the saved file to (5, 0), making it on the SAME row as node 1
        std::ifstream in(jsonPath);
        std::string jsonContent((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
        in.close();
        // Replace node 2's position Y=2 with Y=0
        auto posIdx = jsonContent.find("2\r\n                    ]");
        if (posIdx != std::string::npos)
        {
            jsonContent.replace(posIdx, 1, "0");
        }
        else
        {
            posIdx = jsonContent.find("2\n                    ]");
            assert(posIdx != std::string::npos);
            jsonContent.replace(posIdx, 1, "0");
        }
        std::ofstream out(jsonPath);
        out << jsonContent;
        out.close();

        const auto loadResult = storage.load(jsonPath);

        // 1. Diagnostics should report the hierarchy violation
        bool foundDiagnostic = false;
        for (const auto &diag : loadResult.diagnostics)
        {
            if (diag.message.find("层级约束") != std::string::npos || diag.message.find("上下层") != std::string::npos)
                foundDiagnostic = true;
        }
        assert(foundDiagnostic);

        // 2. Binding must be PRESERVED for repair (not quietly removed)
        const auto &loadedFlow = loadResult.project.flows.front();
        const auto &loadedNode2 = *std::find_if(loadedFlow.nodes.begin(), loadedFlow.nodes.end(), [&](const auto &n) { return n.id == n2; });
        assert(loadedNode2.inputs.at("in_val").source.has_value());
        assert(loadedNode2.inputs.at("in_val").source->node == n1);

        // 3. portRelations has error, dependencyEdges throws
        const auto relations = portRelations(loadedFlow);
        assert(!relations.errors.empty());
        rejects([&] {
            dependencyEdges(loadedFlow);
        }, "层级约束");

        // 4. RuntimeEngine execution rejected for flow and single node
        RuntimeEngine engine(2);

        // Flow run rejected
        const auto flowStatus = engine.start(loadResult.project, registry, RunMode::Once);
        assert(!flowStatus);
        assert(std::string_view(flowStatus.message).find("层级约束") != std::string_view::npos);

        // Single node run for node 2 rejected
        const auto nodeStatus = engine.start(loadResult.project, registry, RunMode::Node, n2);
        assert(!nodeStatus);
        assert(std::string_view(nodeStatus.message).find("层级约束") != std::string_view::npos);

        // 5. Repair: Move node 2 to (5, 4) so that source.y(0) < target.y(4)
        ProjectEditor repairEditor(loadResult.project);
        bool moved = repairEditor.moveNodes("flow_1", std::array{n2}, {0, 4});
        assert(moved);

        const auto &repairedFlow = repairEditor.project().flows.front();
        const auto repairedRelations = portRelations(repairedFlow);
        assert(repairedRelations.errors.empty());
        assert(repairedRelations.active.size() == 1);

        const auto edges = dependencyEdges(repairedFlow);
        assert(edges.size() == 1);
        assert(edges.front() == std::make_pair(n1, n2));

        // Now single node run for node 1 starts without hierarchy rejection
        const auto started = engine.start(repairEditor.project(), registry, RunMode::Node, n1);
        assert(started);
        const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(5);
        while (std::chrono::steady_clock::now() < deadline)
        {
            auto u = engine.pollLatest();
            if (u.completion)
            {
                engine.acknowledgeCompletion(*u.completion);
                break;
            }
            std::this_thread::sleep_for(std::chrono::milliseconds(2));
        }
    }

    std::cout << "[PASS] All edge hierarchy contract tests passed successfully!" << std::endl;
    return 0;
}
