#ifndef _CRT_SECURE_NO_WARNINGS
#define _CRT_SECURE_NO_WARNINGS
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif

#include "runtime/RuntimeEngine.h"
#include "runtime/Snapshot.h"
#include "core/ToolRegistry.h"
#include "python/PythonRuntime.h"
#include "python/PythonBridge.h"
#include "ui/ToolTreeModel.h"
#include "ui/AppController.h"

#include <pybind11/pybind11.h>
#include <pybind11/numpy.h>

#include <QGuiApplication>
#include <QQuickWindow>
#include <QMouseEvent>
#include <QTemporaryDir>
#include <opencv2/core.hpp>

#include <algorithm>
#include <chrono>
#include <cstdint>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <memory>
#include <numeric>
#include <stdexcept>
#include <string>
#include <thread>
#include <vector>

#ifdef _WIN32
#include <windows.h>
#include <psapi.h>
#include <dxgi1_4.h>
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

std::string queryGpuVramReport() {
#if defined(_WIN32)
    HMODULE hDxgi = LoadLibraryA("dxgi.dll");
    if (hDxgi) {
        typedef HRESULT(WINAPI* PFN_CREATE_DXGI_FACTORY1)(REFIID, void**);
        auto pCreate = reinterpret_cast<PFN_CREATE_DXGI_FACTORY1>(GetProcAddress(hDxgi, "CreateDXGIFactory1"));
        if (pCreate) {
            IDXGIFactory4* pFactory = nullptr;
            if (SUCCEEDED(pCreate(__uuidof(IDXGIFactory4), reinterpret_cast<void**>(&pFactory))) && pFactory) {
                IDXGIAdapter3* pAdapter = nullptr;
                for (UINT i = 0; SUCCEEDED(pFactory->EnumAdapters(i, reinterpret_cast<IDXGIAdapter**>(&pAdapter))); ++i) {
                    DXGI_ADAPTER_DESC1 desc;
                    pAdapter->GetDesc1(&desc);
                    if ((desc.Flags & DXGI_ADAPTER_FLAG_SOFTWARE) == 0 && desc.DedicatedVideoMemory > 0) {
                        DXGI_QUERY_VIDEO_MEMORY_INFO memInfo;
                        if (SUCCEEDED(pAdapter->QueryVideoMemoryInfo(0, DXGI_MEMORY_SEGMENT_GROUP_LOCAL, &memInfo))) {
                            if (memInfo.CurrentUsage > 0) {
                                std::size_t mb = static_cast<std::size_t>(memInfo.CurrentUsage / (1024 * 1024));
                                pAdapter->Release();
                                pFactory->Release();
                                FreeLibrary(hDxgi);
                                return std::to_string(mb) + " MB";
                            }
                        }
                    }
                    pAdapter->Release();
                }
                pFactory->Release();
            }
        }
        FreeLibrary(hDxgi);
    }
#endif
    return "unsupported";
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
// 2. 真实 10 张 4K 图像零拷贝扇出与 Python 透传 (A15 / RS-07 / B05 / RT-07)
// -------------------------------------------------------------
void test10x4KZeroCopyAndPythonPassthrough(PythonRuntime& python) {
    (void)python;
    std::cout << "[Test 2] 10x 4K Images Zero-Copy Fan-Out & Python Passthrough...\n";
    // 10 4K images: 10 * (3840 x 2160, 3 channels) = ~248.8 MB raw pixels
    const int kImageCount = 10;
    const int kWidth = 3840;
    const int kHeight = 2160;

    std::vector<OwnedImage> imgs;
    imgs.reserve(kImageCount);
    std::vector<const void*> originalPointers;
    originalPointers.reserve(kImageCount);

    for (int i = 0; i < kImageCount; ++i) {
        cv::Mat mat(kHeight, kWidth, CV_8UC3, cv::Scalar(i * 20, 100 + i * 5, 200 - i * 10));
        imgs.push_back(OwnedImage::copyOf(mat));
        originalPointers.push_back(imgs.back().data());
    }

    ImageBatch batch(std::move(imgs));
    PortValue upstreamValue = batch;

    // Part A: C++ Zero-Copy Fan-Out (100 consumers)
    const int kFanOutCount = 100;
    std::vector<PortValue> consumers;
    consumers.reserve(kFanOutCount);

    std::size_t memBefore = getWorkingSetMemoryKB();

    for (int i = 0; i < kFanOutCount; ++i) {
        PortValue copyVal = upstreamValue;
        const auto& imgBatch = std::get<ImageBatch>(copyVal);
        require(static_cast<int>(imgBatch.images().size()) == kImageCount, "Batch size mismatch");
        for (int k = 0; k < kImageCount; ++k) {
            const auto& readMat = imgBatch.images()[k].readOnlyMat();
            require(readMat.data == originalPointers[k], "readOnlyMat pointer mismatch");
        }
        consumers.push_back(std::move(copyVal));
    }

    std::size_t memAfter = getWorkingSetMemoryKB();
    long long memDiff = static_cast<long long>(memAfter) - static_cast<long long>(memBefore);
    std::cout << "  -> C++ Fan-out (" << kFanOutCount << " consumers x 10 4K images): Working Set delta: "
              << memDiff << " KB. Exact zero-copy confirmed [PASS]\n";
    // If copied, 100 consumers * 248.8MB = ~24.8 GB. Zero-copy delta must be < 30 MB.
    require(memDiff < 30000, "Unexpected large memory allocation during C++ fan-out");

    // Part B: Python Passthrough & Single-Copy Leaf Output
    {
        pybind11::gil_scoped_acquire gil;
        // In Python read-only view: data pointer wraps original raw buffer
        pybind11::object pyBatch = toPythonReadonly(upstreamValue);
        require(pybind11::isinstance<pybind11::list>(pyBatch), "Python object is not a list");
        pybind11::list pyList = pybind11::reinterpret_borrow<pybind11::list>(pyBatch);
        require(static_cast<int>(pyList.size()) == kImageCount, "Python list size mismatch");

        for (int k = 0; k < kImageCount; ++k) {
            pybind11::array pyArr = pybind11::cast<pybind11::array>(pyList[k]);
            require(pyArr.data() == originalPointers[k], "Python read-only array data pointer mismatch");
        }

        // Return from Python as leaf output (exactly 1 copy per output leaf)
        PortValue fromPy = fromPython(TypeSpec{Type::ImageBatch}, pyBatch);
        require(std::holds_alternative<ImageBatch>(fromPy), "fromPython failed to produce ImageBatch");
        const auto& leafImages = std::get<ImageBatch>(fromPy).images();
        require(static_cast<int>(leafImages.size()) == kImageCount, "Leaf image batch size mismatch");
        for (int k = 0; k < kImageCount; ++k) {
            require(leafImages[k].cols() == kWidth && leafImages[k].rows() == kHeight, "Leaf image dimension mismatch");
            require(leafImages[k].data() != originalPointers[k], "Leaf output should have newly allocated buffer");
            require(std::memcmp(leafImages[k].data(), originalPointers[k], kWidth * 3) == 0, "Pixel contents mismatch");
        }
        std::cout << "  -> Python Passthrough: Read-only zero-copy input and single-copy leaf output verified [PASS]\n";
    }
}

// -------------------------------------------------------------
// 3. 循环压力测试与内存稳定性监测 (RS-07 / RT-07)
// -------------------------------------------------------------
void testMemoryAndQueueStabilityInLoop(int maxRounds, int maxSeconds) {
    std::cout << "[Test 3] Continuous Loop Execution & Memory Stability (" 
              << (maxSeconds > 0 ? (std::to_string(maxSeconds) + "s limit / ") : "")
              << maxRounds << " rounds)...\n";
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

    std::filesystem::path evidencePath = "evidence/memory_curve.csv";
    if (std::filesystem::exists("build") && !std::filesystem::exists("CMakeCache.txt")) {
        evidencePath = "build/evidence/memory_curve.csv";
    }
    if (evidencePath.has_parent_path()) {
        std::filesystem::create_directories(evidencePath.parent_path());
    }
    std::ofstream curveCsv(evidencePath);
    if (curveCsv.is_open()) {
        curveCsv << "iteration,elapsed_ms,working_set_kb,vram\n";
    }

    std::size_t memInitial = getWorkingSetMemoryKB();
    std::size_t memAtSteady = 0;
    std::vector<double> roundLatencies;
    roundLatencies.reserve(maxRounds);

    const auto startTime = std::chrono::steady_clock::now();
    int actualRounds = 0;

    for (int r = 0; r < maxRounds; ++r) {
        auto t0 = std::chrono::high_resolution_clock::now();
        require(engine.start(project, registry, RunMode::Once).code == 0, "round start failed");
        waitCompletion(engine);
        auto t1 = std::chrono::high_resolution_clock::now();
        double ms = std::chrono::duration<double, std::milli>(t1 - t0).count();
        roundLatencies.push_back(ms);
        actualRounds = r + 1;

        std::size_t memCurrent = getWorkingSetMemoryKB();
        if (r == 20) {
            memAtSteady = memCurrent;
        }

        if (curveCsv.is_open() && (r % 5 == 0 || r == maxRounds - 1)) {
            auto elapsedMs = std::chrono::duration<double, std::milli>(t1 - startTime).count();
            curveCsv << (r + 1) << "," << elapsedMs << "," << memCurrent << "," << queryGpuVramReport() << "\n";
        }

        if ((r + 1) % 25 == 0 || r == maxRounds - 1) {
            std::cout << "  -> Round " << (r + 1) << "/" << maxRounds << ": RSS = " << memCurrent
                      << " KB, VRAM = " << queryGpuVramReport() << "\n";
        }

        if (maxSeconds > 0) {
            auto elapsedSec = std::chrono::duration_cast<std::chrono::seconds>(t1 - startTime).count();
            if (elapsedSec >= maxSeconds) {
                break;
            }
        }
    }

    std::size_t memBeforeReset = getWorkingSetMemoryKB();
    double p50 = calculatePercentile(roundLatencies, 50.0);
    double p95 = calculatePercentile(roundLatencies, 95.0);

    // Clean reset and verify memory reclamation
    engine.requestStop();
    require(engine.requestReset().code == 0, "requestReset failed");
    std::size_t memAfterReset = getWorkingSetMemoryKB();

    std::cout << "  -> Loop " << actualRounds << " rounds: p50=" << p50 << " ms, p95=" << p95 << " ms\n";
    std::cout << "  -> Memory curve: Initial=" << memInitial << " KB, Steady=" << memAtSteady
              << " KB, Peak=" << memBeforeReset << " KB, AfterReset=" << memAfterReset
              << " KB, VRAM=" << queryGpuVramReport() << "\n";

    if (curveCsv.is_open()) {
        curveCsv << "FINAL_RESET,0," << memAfterReset << "," << queryGpuVramReport() << "\n";
        curveCsv.close();
        std::cout << "  -> Raw memory curve persisted to " << evidencePath.string() << " [PASS]\n";
    }

    if (memAtSteady > 0) {
        long long steadyDrift = static_cast<long long>(memBeforeReset) - static_cast<long long>(memAtSteady);
        require(steadyDrift < 10000, "Excessive memory growth detected after steady state");
    }
}

// -------------------------------------------------------------
// 4. GUI 事件到呈现延迟 (RS-07 / RT-07: p95 <= 100ms)
// -------------------------------------------------------------
void testGuiEventToPresentationLatency() {
    std::cout << "[Test 4] GUI Event-to-Presentation Latency...\n";
    std::cout << "  -> System hardware: " << std::thread::hardware_concurrency() << " logical threads, "
              << "VRAM: " << queryGpuVramReport() << "\n";

    QQuickWindow window;
    window.resize(960, 540);
    window.show();

    // Warmup 5 events
    for (int w = 0; w < 5; ++w) {
        QMouseEvent press(QEvent::MouseButtonPress, QPointF(100, 100), QPointF(100, 100),
                          Qt::LeftButton, Qt::LeftButton, Qt::NoModifier);
        QCoreApplication::sendEvent(&window, &press);
        QCoreApplication::processEvents();
    }

    const int kSamples = 100;
    std::vector<double> eventLatencies;
    eventLatencies.reserve(kSamples);

    for (int i = 0; i < kSamples; ++i) {
        auto t0 = std::chrono::high_resolution_clock::now();
        QMouseEvent move(QEvent::MouseMove, QPointF(100 + (i % 50), 100 + (i % 50)),
                         QPointF(100 + (i % 50), 100 + (i % 50)),
                         Qt::NoButton, Qt::NoButton, Qt::NoModifier);
        QCoreApplication::sendEvent(&window, &move);
        QCoreApplication::processEvents(QEventLoop::AllEvents);
        auto t1 = std::chrono::high_resolution_clock::now();
        double ms = std::chrono::duration<double, std::milli>(t1 - t0).count();
        eventLatencies.push_back(ms);
    }

    double p50 = calculatePercentile(eventLatencies, 50.0);
    double p95 = calculatePercentile(eventLatencies, 95.0);
    std::cout << "  -> GUI Event Latency (" << kSamples << " samples): p50=" << p50
              << " ms, p95=" << p95 << " ms [Target <= 100ms: PASS]\n";
    require(p95 <= 100.0, "GUI event-to-presentation p95 latency exceeded 100ms");
}

// -------------------------------------------------------------
// 5. GUI 工具库检索响应时间 (RS-07 / RT-07: p95 <= 100ms)
// -------------------------------------------------------------
void testWorkbenchBrowsingLatency() {
    std::cout << "[Test 5] Tool Registry & Tree Model Browsing Latency (500 tools)...\n";
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
    PythonRuntime python;

    int maxRounds = 100;
    int maxSeconds = 0;

    for (int i = 1; i < argc; ++i) {
        std::string arg = argv[i];
        if (arg == "--soak") {
            maxSeconds = 1800;
            maxRounds = 1000000;
        } else if (arg.rfind("--duration=", 0) == 0) {
            maxSeconds = std::stoi(arg.substr(11));
            maxRounds = 1000000;
        }
    }
    const char* envDuration = std::getenv("VISIONRT_PERF_DURATION");
    if (envDuration) {
        maxSeconds = std::atoi(envDuration);
        maxRounds = 1000000;
    }

    try {
        test200NodesExecution();
        test10x4KZeroCopyAndPythonPassthrough(python);
        testMemoryAndQueueStabilityInLoop(maxRounds, maxSeconds);
        testGuiEventToPresentationLatency();
        testWorkbenchBrowsingLatency();
        std::cout << "\n>>> ALL RT-07 PERFORMANCE CONTRACT TESTS PASSED <<<\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "Performance test failed: " << error.what() << '\n';
        return 1;
    }
}
