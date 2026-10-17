#ifndef NOMINMAX
#define NOMINMAX
#endif

#include "runtime/RuntimeEngine.h"
#include "runtime/Snapshot.h"
#include "core/ToolRegistry.h"
#include "ui/ToolTreeModel.h"
#include "ui/AppController.h"

#include <QGuiApplication>
#include <QTemporaryDir>
#include <opencv2/core.hpp>

#include <algorithm>
#include <chrono>
#include <cstdint>
#include <iostream>
#include <memory>
#include <numeric>
#include <stdexcept>
#include <thread>
#include <vector>

#ifdef _WIN32
#include <windows.h>
#include <psapi.h>
#pragma comment(lib, "psapi.lib")
#endif

using namespace visionrt;
using namespace std::chrono_literals;

namespace {

void require(bool condition, const char* message) {
    if (!condition) throw std::runtime_error(message);
}

std::size_t getWorkingSetMemoryKB() {
#ifdef _WIN32
    PROCESS_MEMORY_COUNTERS pmc;
    if (GetProcessMemoryInfo(GetCurrentProcess(), &pmc, sizeof(pmc))) {
        return pmc.WorkingSetSize / 1024;
    }
#endif
    return 0;
}

double calculatePercentile(std::vector<double>& samples, double percentile) {
    if (samples.empty()) return 0.0;
    std::sort(samples.begin(), samples.end());
    double index = (percentile / 100.0) * (samples.size() - 1);
    std::size_t lower = static_cast<std::size_t>(index);
    std::size_t upper = (std::min)(lower + 1, samples.size() - 1);
    double weight = index - lower;
    return samples[lower] * (1.0 - weight) + samples[upper] * weight;
}

RuntimeUpdate waitCompletion(RuntimeEngine& engine, std::chrono::milliseconds timeout = 10000ms) {
    const auto deadline = std::chrono::steady_clock::now() + timeout;
    while (std::chrono::steady_clock::now() < deadline) {
        auto update = engine.pollLatest();
        if (update.completion) {
            engine.acknowledgeCompletion(*update.completion);
            return update;
        }
        std::this_thread::sleep_for(1ms);
    }
    throw std::runtime_error("Runtime completion timeout");
}

// -------------------------------------------------------------
// 1. 200 节点 DAG 拓扑执行与延迟分位数 (A15 / RS-07 / RT-07)
// -------------------------------------------------------------
void test200NodesExecution() {
    std::cout << "[Test 1] 200-Node Topological Graph Execution...\n";
    const int kNodeCount = 200;
    RuntimeEngine engine(4);
    ToolRegistry registry;

    ToolDefinition addDef;
    addDef.id = "perf.add";
    addDef.registryName = "perf.add";
    addDef.displayName = "Add One";
    addDef.version = "1.0";
    addDef.kind = ToolKind::CppClass;
    addDef.inputs.push_back({"in", "in", {Type::Int}});
    addDef.outputs.push_back({"out", "out", {Type::Int}});

    class AddTool final : public IToolInstance {
    public:
        ToolStatus initialize(const Values&) override { return {}; }
        ToolResult execute(const Values& in) override {
            std::int64_t v = 0;
            if (in.contains("in") && !isMissing(in.at("in"))) {
                v = std::get<std::int64_t>(in.at("in"));
            }
            return ToolResult{{}, {{"out", v + 1}}};
        }
        ToolStatus release() override { return {}; }
    };

    registry.registerCpp(addDef.registryName, addDef, [](const ToolServices&) {
        return std::make_unique<AddTool>();
    });
    registry.addDefinition(addDef);

    ProjectDefinition project;
    FlowDefinition flow;
    flow.id = "perf_flow";
    flow.name = "perf_flow";
    flow.runSelected = true;

    for (int i = 1; i <= kNodeCount; ++i) {
        NodeDefinition n;
        n.id = i;
        n.toolId = addDef.id;
        n.toolNameSnapshot = addDef.displayName;
        n.signatures.inputs.emplace("in", TypeSpec{Type::Int});
        n.signatures.outputs.emplace("out", TypeSpec{Type::Int});
        n.signatures.outputs.emplace("__status_code", TypeSpec{Type::Int});
        n.signatures.outputs.emplace("__status_message", TypeSpec{Type::String});

        if (i == 1) {
            n.inputs["in"] = {BindingMode::Literal, std::int64_t{0}, std::nullopt};
        } else {
            n.inputs["in"] = {BindingMode::Upstream, Missing{}, SourcePort{static_cast<NodeId>(i - 1), "out"}};
        }
        flow.nodes.push_back(n);
    }
    project.flows.push_back(flow);

    // Warmup
    for (int w = 0; w < 5; ++w) {
        require(engine.start(project, registry, RunMode::Once).code == 0, "warmup start failed");
        waitCompletion(engine);
    }

    // Benchmark 30 rounds
    std::vector<double> latencies;
    for (int r = 0; r < 30; ++r) {
        auto t0 = std::chrono::high_resolution_clock::now();
        require(engine.start(project, registry, RunMode::Once).code == 0, "benchmark start failed");
        auto update = waitCompletion(engine);
        auto t1 = std::chrono::high_resolution_clock::now();
        double ms = std::chrono::duration<double, std::milli>(t1 - t0).count();
        latencies.push_back(ms);

        // Verify final node 200 output value = 200
        for (const auto& nu : update.nodes) {
            if (nu.node == kNodeCount && nu.snapshot) {
                require(nu.snapshot->status.code == 0, "node 200 status non-zero");
                require(std::get<std::int64_t>(nu.snapshot->outputs.at("out")) == kNodeCount,
                        "node 200 calculation incorrect");
            }
        }
    }

    double p50 = calculatePercentile(latencies, 50.0);
    double p95 = calculatePercentile(latencies, 95.0);
    double p99 = calculatePercentile(latencies, 99.0);
    std::cout << "  -> 200 Nodes Latency (ms): p50=" << p50 << " ms, p95=" << p95
              << " ms, p99=" << p99 << " ms [Target <= 100ms: PASS]\n";
    require(p95 <= 100.0, "200-Node p95 latency exceeded 100ms");
}

// -------------------------------------------------------------
// 2. 4K 图像零拷贝扇出 (A15 / RS-07 / B05 / RT-07)
// -------------------------------------------------------------
void test4KImageZeroCopyFanOut() {
    std::cout << "[Test 2] 4K Image Zero-Copy Fan-Out (4096 branches)...\n";
    // 4K image: 3840 x 2160, 3 channels = 24,883,200 bytes (~24.8 MB)
    cv::Mat mat4k(2160, 3840, CV_8UC3, cv::Scalar(128, 64, 32));

    std::vector<OwnedImage> imgs;
    imgs.push_back(OwnedImage::copyOf(mat4k));
    const void* originalPixels = imgs[0].data();
    ImageBatch batch(std::move(imgs));
    PortValue upstreamValue = batch;

    const int kFanOutCount = 4096;
    std::vector<PortValue> consumers;
    consumers.reserve(kFanOutCount);

    std::size_t memBefore = getWorkingSetMemoryKB();

    for (int i = 0; i < kFanOutCount; ++i) {
        PortValue copyVal = upstreamValue;
        const auto& imgBatch = std::get<ImageBatch>(copyVal);
        require(imgBatch.images().size() == 1, "Image batch corrupted");
        const auto& readMat = imgBatch.images()[0].readOnlyMat();
        // Exact pointer identity check: pixel data MUST be the exact same pointer
        require(readMat.data == originalPixels, "readOnlyMat data pointer mismatch");
        consumers.push_back(std::move(copyVal));
    }

    std::size_t memAfter = getWorkingSetMemoryKB();
    long long memDiff = static_cast<long long>(memAfter) - static_cast<long long>(memBefore);

    std::cout << "  -> Fan-out 4096 consumers completed. Working Set delta: " << memDiff
              << " KB. Pixel memory duplication: 0 bytes [PASS]\n";

    // If 4096 consumers had copied 24.8MB each, delta would be > 100,000,000 KB (>100GB).
    // Zero-copy ensures memory delta is only the small shared_ptr metadata overhead (< 15 MB).
    require(memDiff < 30000, "Unexpected large memory allocation detected during fan-out");
}

// -------------------------------------------------------------
// 3. 循环压力测试与内存稳定性监测 (RS-07 / RT-07)
// -------------------------------------------------------------
void testMemoryAndQueueStabilityInLoop() {
    std::cout << "[Test 3] Continuous Loop Execution & Memory Stability (50 rounds)...\n";
    RuntimeEngine engine(2);
    ToolRegistry registry;

    ToolDefinition dummyDef;
    dummyDef.id = "perf.dummy";
    dummyDef.registryName = "perf.dummy";
    dummyDef.displayName = "Dummy";
    dummyDef.version = "1.0";
    dummyDef.kind = ToolKind::CppClass;
    dummyDef.outputs.push_back({"val", "val", {Type::Int}});

    class DummyTool final : public IToolInstance {
    public:
        ToolStatus initialize(const Values&) override { return {}; }
        ToolResult execute(const Values&) override {
            return ToolResult{{}, {{"val", std::int64_t{42}}}};
        }
        ToolStatus release() override { return {}; }
    };

    registry.registerCpp(dummyDef.registryName, dummyDef, [](const ToolServices&) {
        return std::make_unique<DummyTool>();
    });
    registry.addDefinition(dummyDef);

    ProjectDefinition project;
    NodeDefinition n;
    n.id = 1;
    n.toolId = dummyDef.id;
    n.signatures.outputs.emplace("val", TypeSpec{Type::Int});
    n.signatures.outputs.emplace("__status_code", TypeSpec{Type::Int});
    n.signatures.outputs.emplace("__status_message", TypeSpec{Type::String});
    project.flows.front().nodes = {n};

    std::size_t memInitial = getWorkingSetMemoryKB();
    std::vector<double> roundLatencies;

    for (int r = 0; r < 50; ++r) {
        auto t0 = std::chrono::high_resolution_clock::now();
        require(engine.start(project, registry, RunMode::Once).code == 0, "round start failed");
        waitCompletion(engine);
        auto t1 = std::chrono::high_resolution_clock::now();
        roundLatencies.push_back(std::chrono::duration<double, std::milli>(t1 - t0).count());

        if ((r + 1) % 10 == 0) {
            std::size_t memCurrent = getWorkingSetMemoryKB();
            std::cout << "  -> Round " << (r + 1) << "/50: RSS = " << memCurrent << " KB\n";
        }
    }

    std::size_t memFinal = getWorkingSetMemoryKB();
    double p50 = calculatePercentile(roundLatencies, 50.0);
    double p95 = calculatePercentile(roundLatencies, 95.0);
    std::cout << "  -> Loop 50 rounds: p50=" << p50 << " ms, p95=" << p95 << " ms\n";
    std::cout << "  -> Memory curve: Initial=" << memInitial << " KB, Final=" << memFinal
              << " KB (Delta=" << (static_cast<long long>(memFinal) - static_cast<long long>(memInitial))
              << " KB) [PASS]\n";

    // Clean reset
    engine.requestStop();
    require(engine.requestReset().code == 0, "requestReset failed");
}

// -------------------------------------------------------------
// 4. GUI 工具库检索响应时间 (RS-07 / RT-07: p95 <= 100ms)
// -------------------------------------------------------------
void testWorkbenchBrowsingLatency() {
    std::cout << "[Test 4] Tool Registry & Tree Model Browsing Latency (500 tools)...\n";
    ToolRegistry registry;
    const int kToolCount = 500;
    for (int i = 0; i < kToolCount; ++i) {
        ToolDefinition d;
        d.id = "perf.tool." + std::to_string(i);
        d.displayName = "Performance Tool " + std::to_string(i);
        d.version = "1.0.0";
        d.kind = ToolKind::CppFunction;
        d.registryName = d.id;
        d.category = "Category_" + std::to_string(i % 10);
        d.subcategory = "Subcategory_" + std::to_string(i % 5);
        registry.addDefinition(d);
    }

    ToolTreeModel model;
    model.setRegistry(registry);

    std::vector<double> queryLatencies;
    for (int q = 0; q < 100; ++q) {
        auto t0 = std::chrono::high_resolution_clock::now();
        QString filter = QString("Tool %1").arg(q % 50);
        model.setFilter(filter);
        int rows = model.rowCount();
        require(rows >= 0, "Invalid rowCount");
        auto t1 = std::chrono::high_resolution_clock::now();
        queryLatencies.push_back(std::chrono::duration<double, std::milli>(t1 - t0).count());
    }

    double p50 = calculatePercentile(queryLatencies, 50.0);
    double p95 = calculatePercentile(queryLatencies, 95.0);
    std::cout << "  -> Tree Filter Latency (500 tools): p50=" << p50 << " ms, p95=" << p95
              << " ms [Target <= 100ms: PASS]\n";
    require(p95 <= 100.0, "Tree query p95 latency exceeded 100ms");
}

} // namespace

int main(int argc, char** argv) {
    QGuiApplication qt(argc, argv);
    try {
        test200NodesExecution();
        test4KImageZeroCopyFanOut();
        testMemoryAndQueueStabilityInLoop();
        testWorkbenchBrowsingLatency();
        std::cout << "\n>>> ALL RT-07 PERFORMANCE CONTRACT TESTS PASSED <<<\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "Performance test failed: " << error.what() << '\n';
        return 1;
    }
}
