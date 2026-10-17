#include "core/ProjectStorage.h"
#include "core/ToolRegistry.h"
#include "runtime/RuntimeEngine.h"
#include "tools/CpuTools.h"
#include "python/PythonRuntime.h"

#include <QCoreApplication>
#include <QDir>
#include <QTemporaryDir>
#include <cassert>
#include <chrono>
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
    ToolRegistry registry;
    registry.scan(repoRoot / "assets" / "tools");
    registerCpuTools(registry);
    setupPython(registry, python);

    ProjectStorage storage(registry);
    const auto projectFile = repoRoot / "examples" / "pipeline_cpu_python.vrt";
    auto loaded = storage.load(projectFile);
    if (!loaded.diagnostics.empty()) {
        storage.save(loaded.project, projectFile);
        loaded = storage.load(projectFile);
    }
    for (const auto& d : loaded.diagnostics) {
        std::cerr << "Diagnostic: node=" << (d.node ? std::to_string(*d.node) : "null")
                  << " code=" << static_cast<int>(d.code)
                  << " field=" << d.field
                  << " msg=" << d.message << std::endl;
    }
    assert(loaded.diagnostics.empty());
    assert(loaded.project.flows.size() == 1);
    assert(loaded.project.flows[0].nodes.size() == 5);

    RuntimeEngine engine(4);
    assert(engine.start(loaded.project, registry, RunMode::Once));
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

    // Test Save As / Reopen consistency
    QTemporaryDir tmpDir;
    assert(tmpDir.isValid());
    const auto saveTarget = std::filesystem::path(tmpDir.path().toStdString()) / "reopened.vrt";
    storage.save(loaded.project, saveTarget);
    auto reloaded = storage.load(saveTarget);
    assert(reloaded.diagnostics.empty());
    assert(reloaded.project.flows[0].nodes.size() == 5);
    for (size_t i = 0; i < 5; ++i) {
        assert(reloaded.project.flows[0].nodes[i].toolId == loaded.project.flows[0].nodes[i].toolId);
    }
}

void testDiagnosticPipeline(const std::filesystem::path& repoRoot, PythonRuntime& python) {
    ToolRegistry registry;
    registry.scan(repoRoot / "assets" / "tools");
    registerCpuTools(registry);
    setupPython(registry, python);

    ProjectStorage storage(registry);
    const auto projectFile = repoRoot / "examples" / "pipeline_error_diagnostic.vrt";
    auto loaded = storage.load(projectFile);
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

    std::cout << "All example contract tests passed successfully!" << std::endl;
    return 0;
}
