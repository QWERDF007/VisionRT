#include "core/DefinitionJson.h"
#include "core/ToolRegistry.h"
#include "python/PythonRuntime.h"
#include "runtime/RuntimeEngine.h"

#include <QCoreApplication>
#include <QTemporaryDir>
#include <cassert>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <stdexcept>
#include <thread>

using namespace visionrt;

namespace {
using namespace std::chrono_literals;

RuntimeUpdate complete(RuntimeEngine &engine)
{
    const auto deadline = std::chrono::steady_clock::now() + 10s;
    while (std::chrono::steady_clock::now() < deadline)
    {
        auto update = engine.pollLatest();
        if (update.completion)
        {
            assert(engine.acknowledgeCompletion(*update.completion));
            return update;
        }
        std::this_thread::sleep_for(1ms);
    }
    throw std::runtime_error("runtime completion deadline exceeded");
}

ResultSlot::Snapshot snapshot(const RuntimeUpdate &update, NodeId id)
{
    for (const auto &item : update.nodes)
        if (item.node == id)
            return item.snapshot;
    throw std::runtime_error("node update absent");
}
}

int main(int argc, char **argv)
try
{
    QCoreApplication application(argc, argv);
    QTemporaryDir    temporary;
    assert(temporary.isValid());
    const auto root = std::filesystem::path(temporary.path().toStdWString());
    const auto package = root / "python/custom/lazy";
    std::filesystem::create_directories(package);
    {
        std::ofstream script(package / "tool.py");
        script << "from pathlib import Path\n"
                  "Path(__file__).with_name('imported.txt').write_text('imported')\n"
                  "def execute(inputs):\n"
                  "    return {'__status_code':0, '__status_message':'运行成功', 'value':7,\n"
                  "            'rois':[[[2.0, 10.5, 20.5, 30.0, 40.0, 15.0]]]}\n";
    }
    const auto definition = parseToolDefinitionJson(R"({
        "schema_version":4,"tool_id":"python.lazy.runtime","display_name":"Lazy runtime","version":"1",
        "kind":"python_function","entry":{"package":"lazy","file":"tool.py","symbol":"execute"},
        "init_params":[],"inputs":[],"outputs":[
            {"id":"value","name":"Value","type":"Int"},
            {"id":"rois","name":"ROIs","type":"ROI"}]
    })",
                                                      root / "tools/custom/lazy/tool.json",
                                                      {
                                                          ToolOrigin::Custom,
                                                          {root / "tools/custom", root / "python/custom"}
    });
    ToolRegistry registry;
    registry.addDefinition(definition);
    PythonRuntime python;
    const auto    refreshed = python.refresh(registry.definitions());
    assert(refreshed.size() == 1 && refreshed.front().status.code == 0);
    registry.setPythonImplementation(definition.id, python.factory(definition));

    NodeDefinition node;
    node.id               = 1;
    node.toolId           = definition.id;
    node.toolNameSnapshot = definition.displayName;
    node.signatures.outputs = {
        {"value",            {Type::Int}},
        {"rois",             {Type::ROI}},
        {"__status_code",    {Type::Int}},
        {"__status_message", {Type::String}}
    };
    ProjectDefinition project;
    project.flows.front().nodes = {node};

    const auto marker = package / "imported.txt";
    {
        RuntimeEngine engine(1);
        assert(engine.requestSynchronize(project, registry));
        complete(engine);
        assert(engine.state() == RuntimeState::Idle);
        assert(!std::filesystem::exists(marker));
        assert(engine.start(project, registry, RunMode::Once));
        const auto result = complete(engine);
        const auto value  = snapshot(result, 1);
        if (!value || value->status.code != 0)
            std::cerr << "status=" << (value ? value->status.code : -1) << " message="
                      << (value ? value->status.message : "missing") << " detail="
                      << (value ? value->status.detail : "") << '\n';
        assert(value && value->status.code == 0);
        assert(std::get<std::int64_t>(value->outputs.at("value")) == 7);
        const auto &rois = std::get<DataRef<ROI>>(value->outputs.at("rois")).get();
        assert(rois.size() == 1 && rois.front().size() == 1);
        assert((rois.front().front() == std::vector<double>{2, 10.5, 20.5, 30.0, 40.0, 15.0}));
        assert(value->timing.initMs > 0.0);
        assert(std::filesystem::exists(marker));
    }
    python.refresh({});
    std::cout << "Runtime configuration does not import Python; first execution loads the declared code root\n";
    return 0;
}
catch (const std::exception &error)
{
    std::cerr << error.what() << '\n';
    return 1;
}
