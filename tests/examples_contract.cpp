#include "core/ProjectStorage.h"
#include "core/ToolRegistry.h"
#include "runtime/RuntimeEngine.h"
#include "tools/CpuTools.h"
#include "python/PythonRuntime.h"
#include "plugins/PluginLoader.h"

#include <QCoreApplication>
#include <QDir>
#include <QTemporaryDir>
#include <cassert>
#include <chrono>
#include <fstream>
#include <iostream>
#include <thread>

using namespace visionrt;
using namespace std::chrono_literals;

namespace {

RuntimeUpdate finish(RuntimeEngine& engine) {
    const auto deadline = std::chrono::steady_clock::now() + 15s;
    while (std::chrono::steady_clock::now() < deadline) {
        auto update = engine.pollLatest();
        if (update.completion) {
            assert(engine.acknowledgeCompletion(*update.completion));
            return update;
        }
        std::this_thread::sleep_for(2ms);
    }
    throw std::runtime_error("runtime completion deadline exceeded");
}

ResultSlot::Snapshot snapshot(const RuntimeUpdate& update, NodeId id) {
    for (const auto& item : update.nodes) {
        if (item.node == id) return item.snapshot;
    }
    throw std::runtime_error("node update absent: " + std::to_string(id));
}

void setupPython(ToolRegistry& registry, PythonRuntime& runtime) {
    std::vector<ToolDefinition> pyDefs;
    for (const auto& d : registry.definitions()) {
        if (d.kind == ToolKind::PythonClass || d.kind == ToolKind::PythonFunction) {
            pyDefs.push_back(d);
        }
    }
    const auto loadedList = runtime.refresh(pyDefs);
    for (const auto& loaded : loadedList) {
        registry.setPythonImplementation(loaded.toolId, runtime.factory(*registry.find(loaded.toolId)), loaded.status);
    }
}

void testCpuPythonPipeline(const std::filesystem::path& repoRoot, PythonRuntime& python) {
    QTemporaryDir tmpDir;
    assert(tmpDir.isValid());
    const auto tmpPath = std::filesystem::path(tmpDir.path().toStdString());
    const auto tempProjectFile = tmpPath / "pipeline_cpu_python.vrt";
    std::filesystem::copy_file(repoRoot / "examples" / "pipeline_cpu_python.vrt", tempProjectFile);
    std::filesystem::copy(repoRoot / "examples" / "images", tmpPath / "images", std::filesystem::copy_options::recursive);

    ToolRegistry registry;
    registry.scan(repoRoot / "assets" / "tools");
    registerCpuTools(registry);
    setupPython(registry, python);

    ProjectStorage storage(registry);
    auto loaded = storage.load(tempProjectFile);
    assert(loaded.diagnostics.empty());
    assert(loaded.project.flows.size() == 1);
    assert(loaded.project.flows[0].nodes.size() == 5);

    RuntimeEngine engine(4);
    assert(engine.start(loaded.project, registry, RunMode::Once).code == 0);
    auto update = finish(engine);

    // Node 1: image read
    auto s1 = snapshot(update, 1);
    assert(s1 && s1->status.code == 0);
    const auto& images1 = std::get<ImageBatch>(s1->outputs.at("images"));
    assert(images1.images().size() >= 2);

    // Node 2: roi projection
    auto s2 = snapshot(update, 2);
    assert(s2 && s2->status.code == 0);
    const auto& rois2 = std::get<RoiBatch>(s2->outputs.at("rois"));
    assert(rois2.images().size() == images1.images().size());

    // Node 3: crop
    auto s3 = snapshot(update, 3);
    assert(s3 && s3->status.code == 0);
    const auto& croppedImages = std::get<ImageBatch>(s3->outputs.at("images"));
    assert(!croppedImages.images().empty());

    // Node 4: preprocess (threshold)
    auto s4 = snapshot(update, 4);
    assert(s4 && s4->status.code == 0);
    const auto& preprocessed = std::get<ImageBatch>(s4->outputs.at("images"));
    assert(preprocessed.images().size() == croppedImages.images().size());

    // Node 5: python mean
    auto s5 = snapshot(update, 5);
    assert(s5 && s5->status.code == 0);
    const auto& means = std::get<NumericArray>(s5->outputs.at("means"));
    assert(means.buffers().size() == 1);
    assert(means.buffers()[0].shape()[0] == static_cast<std::int64_t>(images1.images().size()));
    const auto values = means.buffers()[0].values<double>();
    assert(values[0] > 0.0);
    assert(values[1] > 0.0);

    // Test Save As / Reopen consistency & re-execution output equality
    const auto saveTarget = std::filesystem::path(tmpDir.path().toStdString()) / "reopened.vrt";
    storage.save(loaded.project, saveTarget);
    auto reloaded = storage.load(saveTarget);
    assert(reloaded.diagnostics.empty());
    assert(reloaded.project.flows[0].nodes.size() == 5);
    for (size_t i = 0; i < 5; ++i) {
        assert(reloaded.project.flows[0].nodes[i].toolId == loaded.project.flows[0].nodes[i].toolId);
    }
    assert(engine.start(reloaded.project, registry, RunMode::Once).code == 0);
    auto reloadedUpdate = finish(engine);
    auto reloadedS5 = snapshot(reloadedUpdate, 5);
    assert(reloadedS5 && reloadedS5->status.code == 0);
    const auto& reloadedMeans = std::get<NumericArray>(reloadedS5->outputs.at("means"));
    const auto reloadedValues = reloadedMeans.buffers()[0].values<double>();
    assert(reloadedValues[0] == values[0]);
    assert(reloadedValues[1] == values[1]);

    // Test corrupted input on a temp copy: must fail (exception or diagnostics) and not corrupt source file
    const auto corruptTarget = std::filesystem::path(tmpDir.path().toStdString()) / "corrupted.vrt";
    std::filesystem::copy_file(tempProjectFile, corruptTarget);
    {
        std::ofstream corruptOut(corruptTarget, std::ios::app);
        corruptOut << "\n!@#$%^&*()INVALID_YAML_TAIL:\n";
    }
    bool corruptFailed = false;
    try {
        auto corruptLoaded = storage.load(corruptTarget);
        corruptFailed = !corruptLoaded.diagnostics.empty();
    } catch (const std::exception& e) {
        std::cout << "Corrupted project loading correctly rejected with exception: " << e.what() << std::endl;
        corruptFailed = true;
    }
    assert(corruptFailed);
    std::cout << "Corrupted input failure test passed." << std::endl;
}

void testDiagnosticPipeline(const std::filesystem::path& repoRoot, PythonRuntime& python) {
    QTemporaryDir tmpDir;
    assert(tmpDir.isValid());
    const auto tmpPath = std::filesystem::path(tmpDir.path().toStdString());
    const auto tempProjectFile = tmpPath / "pipeline_error_diagnostic.vrt";
    std::filesystem::copy_file(repoRoot / "examples" / "pipeline_error_diagnostic.vrt", tempProjectFile);
    std::filesystem::copy(repoRoot / "examples" / "images", tmpPath / "images", std::filesystem::copy_options::recursive);

    ToolRegistry registry;
    registry.scan(repoRoot / "assets" / "tools");
    registerCpuTools(registry);
    setupPython(registry, python);

    ProjectStorage storage(registry);
    auto loaded = storage.load(tempProjectFile);
    // Node 4 is DefinitionMissing
    bool foundMissingDef = false;
    for (const auto& d : loaded.diagnostics) {
        if (d.node == 4 && d.code == ProjectStorageCode::DefinitionMissing) {
            foundMissingDef = true;
            break;
        }
    }
    assert(foundMissingDef);

    RuntimeEngine engine(2);
    assert(engine.start(loaded.project, registry, RunMode::Once));
    auto update = finish(engine);

    // Node 1: read succeeds
    auto s1 = snapshot(update, 1);
    assert(s1 && s1->status.code == 0);

    // Node 2: crop fails because rois input is Missing
    auto s2 = snapshot(update, 2);
    assert(s2);
    std::cout << "s2 status: code=" << s2->status.code << " msg=" << s2->status.message << std::endl;
    assert(!s2->status);
    assert(isMissing(s2->outputs.at("images")));

    // Node 3: yolo fails because nonexistent model weights
    auto s3 = snapshot(update, 3);
    assert(s3);
    assert(s3->status.code != 0); // Not 0! Must not report success!
    assert(!s3->status.message.empty());

    // Try starting the flow containing Node 4 (definition missing)
    loaded.project.flows[0].runSelected = false;
    loaded.project.flows[1].runSelected = true;
    const auto missingStartStatus = engine.start(loaded.project, registry, RunMode::Once);
    std::cout << "missingStartStatus: code=" << missingStartStatus.code << " msg=" << missingStartStatus.message << std::endl;
    assert(!missingStartStatus);
    assert(missingStartStatus.code == 1);
}

void testRealPluginModulePipeline(const std::filesystem::path& repoRoot, PythonRuntime& python) {
    const auto appDir = std::filesystem::path(QCoreApplication::applicationDirPath().toStdString());
    auto pluginDll = appDir / "test_plugin_module.dll";
    if (!std::filesystem::exists(pluginDll)) {
        pluginDll = appDir / "bin" / "test_plugin_module.dll";
    }
    assert(std::filesystem::exists(pluginDll));

    auto loader = std::make_shared<PluginLoader>();
    loader->registerModule("test.plugin.counter", pluginDll);
    loader->registerModule("test.plugin.function", pluginDll);
    loader->registerModule("test.plugin.fail_init", pluginDll);

    ToolRegistry registry;
    registry.setPluginLoader(loader);
    registry.scan(repoRoot / "assets" / "tools");
    setupPython(registry, python);

    // Register plugin counter tool
    ToolDefinition counterDef;
    counterDef.id = "test.plugin.counter";
    counterDef.displayName = "Plugin Counter";
    counterDef.version = "1.0.0";
    counterDef.kind = ToolKind::CppClass;
    counterDef.registryName = "test.plugin.counter";
    PortDefinition initParam;
    initParam.id = "initial"; initParam.name = "Initial"; initParam.type = {Type::Int}; initParam.defaultValue = std::int64_t{10};
    counterDef.initParams.push_back(initParam);
    PortDefinition outCount;
    outCount.id = "count"; outCount.name = "Count"; outCount.type = {Type::Int};
    counterDef.outputs.push_back(outCount);
    registry.addDefinition(counterDef);

    // Register plugin function tool
    ToolDefinition funcDef;
    funcDef.id = "test.plugin.function";
    funcDef.displayName = "Plugin Function";
    funcDef.version = "1.0.0";
    funcDef.kind = ToolKind::CppFunction;
    funcDef.registryName = "test.plugin.function";
    PortDefinition inX;
    inX.id = "x"; inX.name = "X"; inX.type = {Type::Int}; inX.defaultValue = std::int64_t{25};
    funcDef.inputs.push_back(inX);
    PortDefinition outY;
    outY.id = "y"; outY.name = "Y"; outY.type = {Type::Int};
    funcDef.outputs.push_back(outY);
    registry.addDefinition(funcDef);

    // Build test pipeline: Node 1 (function, x=25 -> y=50), Node 2 (counter, initial=10 -> count=11)
    ProjectDefinition project;
    FlowDefinition flow;
    flow.id = "flow_1";
    flow.name = "Real Plugin Flow";

    NodeDefinition n1;
    n1.id = 1;
    n1.toolId = "test.plugin.function";
    n1.toolNameSnapshot = "Plugin Function";
    n1.position = {100, 100};
    InputBinding b1;
    b1.mode = BindingMode::Literal;
    b1.literal = std::int64_t{25};
    n1.inputs.emplace("x", b1);
    n1.signatures.inputs["x"] = {Type::Int};
    n1.signatures.outputs["y"] = {Type::Int};
    flow.nodes.push_back(n1);

    NodeDefinition n2;
    n2.id = 2;
    n2.toolId = "test.plugin.counter";
    n2.toolNameSnapshot = "Plugin Counter";
    n2.position = {300, 100};
    n2.initParams.emplace("initial", std::int64_t{10});
    n2.signatures.initParams["initial"] = {Type::Int};
    n2.signatures.outputs["count"] = {Type::Int};
    flow.nodes.push_back(n2);

    project.flows.clear();
    project.flows.push_back(flow);
    project.nextNodeId = 3;

    RuntimeEngine engine(2);
    const auto startRes = engine.start(project, registry, RunMode::Once);
    if (startRes.code != 0) {
        std::cerr << "engine.start failed: code=" << startRes.code << " msg=" << startRes.message << std::endl;
    }
    assert(startRes.code == 0);
    const auto update = finish(engine);

    auto s1 = snapshot(update, 1);
    assert(s1 && s1->status.code == 0);
    assert(std::get<std::int64_t>(s1->outputs.at("y")) == 50);

    auto s2 = snapshot(update, 2);
    assert(s2 && s2->status.code == 0);
    assert(std::get<std::int64_t>(s2->outputs.at("count")) == 11);

    // Test Save / Reopen consistency
    QTemporaryDir tmpDir;
    assert(tmpDir.isValid());
    const auto savePath = std::filesystem::path(tmpDir.path().toStdString()) / "plugin_project.vrt";
    ProjectStorage storage(registry);
    auto reconciled = storage.reconcile(project);
    assert(reconciled.diagnostics.empty());
    project = std::move(reconciled.project);
    storage.save(project, savePath);
    const auto reloaded = storage.load(savePath);
    assert(reloaded.diagnostics.empty());
    assert(engine.start(reloaded.project, registry, RunMode::Once).code == 0);
    const auto reloadedUpdate = finish(engine);
    auto rs1 = snapshot(reloadedUpdate, 1);
    assert(rs1 && std::get<std::int64_t>(rs1->outputs.at("y")) == std::get<std::int64_t>(s1->outputs.at("y")));
    auto rs2 = snapshot(reloadedUpdate, 2);
    assert(rs2 && std::get<std::int64_t>(rs2->outputs.at("count")) == std::get<std::int64_t>(s2->outputs.at("count")));

    std::cout << "[INFO] GPU execution: unverified (host environment has no CUDA/TensorRT)" << std::endl;
}

} // namespace

int main(int argc, char** argv) {
    QCoreApplication app(argc, argv);
    assert(argc >= 2);
    const std::filesystem::path repoRoot = std::filesystem::path(argv[1]);

    PythonRuntime python;

    std::cout << "[TEST] Running CPU + Python pipeline example..." << std::endl;
    testCpuPythonPipeline(repoRoot, python);
    std::cout << "[PASS] CPU + Python pipeline passed." << std::endl;

    std::cout << "[TEST] Running error diagnostic pipeline example..." << std::endl;
    testDiagnosticPipeline(repoRoot, python);
    std::cout << "[PASS] Error diagnostic pipeline passed." << std::endl;

    std::cout << "[TEST] Running real plugin module pipeline (VR2-07)..." << std::endl;
    try {
        testRealPluginModulePipeline(repoRoot, python);
    } catch (const std::exception& e) {
        std::cerr << "FAILED testRealPluginModulePipeline with exception: " << e.what() << std::endl;
        throw;
    }
    std::cout << "[PASS] Real plugin module pipeline passed." << std::endl;

    std::cout << "All example contract tests passed successfully!" << std::endl;
    return 0;
}
