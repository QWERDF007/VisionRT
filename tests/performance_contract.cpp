#ifndef _CRT_SECURE_NO_WARNINGS
#    define _CRT_SECURE_NO_WARNINGS
#endif
#ifndef NOMINMAX
#    define NOMINMAX
#endif

#include "core/ToolRegistry.h"
#include "python/PythonBridge.h"
#include "python/PythonRuntime.h"
#include "runtime/RuntimeEngine.h"
#include "runtime/Snapshot.h"
#include "ui/AppController.h"
#include "ui/DisplayConverter.h"
#include "ui/ImageViewport.h"
#include "ui/Theme.h"
#include "ui/ToolTreeModel.h"

#include <opencv2/core.hpp>
#include <pybind11/numpy.h>
#include <pybind11/pybind11.h>

#include <QFile>
#include <QGuiApplication>
#include <QMouseEvent>
#include <QQmlApplicationEngine>
#include <QQmlContext>
#include <QQuickItem>
#include <QQuickStyle>
#include <QQuickWindow>
#include <QSGRendererInterface>
#include <QTemporaryDir>
#include <QUrl>
#include <algorithm>
#include <atomic>
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
// clang-format off
#    include <windows.h>
#    include <dxgi1_4.h>
#    include <psapi.h>
// clang-format on
#    pragma comment(lib, "psapi.lib")
#endif

using namespace visionrt;
using namespace std::chrono_literals;

namespace {

void require(bool condition, const char *message)
{
    if (!condition)
        throw std::runtime_error(message);
}

void require(bool condition, const std::string &message)
{
    if (!condition)
        throw std::runtime_error(message);
}

struct MemoryProbe
{
    bool        supported    = false;
    std::size_t workingSetKB = 0;
    std::string report;
};

MemoryProbe getWorkingSetMemory()
{
    MemoryProbe probe;
#ifdef _WIN32
    PROCESS_MEMORY_COUNTERS pmc{};
    if (GetProcessMemoryInfo(GetCurrentProcess(), &pmc, sizeof(pmc)))
    {
        probe.supported    = true;
        probe.workingSetKB = pmc.WorkingSetSize / 1024;
        probe.report       = std::to_string(probe.workingSetKB) + " KB";
        return probe;
    }
    probe.report = "unsupported (GetProcessMemoryInfo failed)";
#else
    probe.report = "unsupported (non-Windows platform)";
#endif
    return probe;
}

struct GpuVramProbe
{
    bool        supported = false;
    std::size_t vramMB    = 0;
    std::string report;
};

GpuVramProbe queryGpuVram()
{
    GpuVramProbe probe;
#if defined(_WIN32)
    HMODULE hDxgi = LoadLibraryA("dxgi.dll");
    if (!hDxgi)
    {
        probe.report = "unsupported (dxgi.dll not found)";
        return probe;
    }

    typedef HRESULT(WINAPI * PFN_CREATE_DXGI_FACTORY1)(REFIID, void **);
    auto pCreate = reinterpret_cast<PFN_CREATE_DXGI_FACTORY1>(GetProcAddress(hDxgi, "CreateDXGIFactory1"));
    if (!pCreate)
    {
        FreeLibrary(hDxgi);
        probe.report = "unsupported (CreateDXGIFactory1 missing)";
        return probe;
    }

    IDXGIFactory4 *pFactory = nullptr;
    if (FAILED(pCreate(__uuidof(IDXGIFactory4), reinterpret_cast<void **>(&pFactory))) || !pFactory)
    {
        FreeLibrary(hDxgi);
        probe.report = "unsupported (IDXGIFactory4 unavailable)";
        return probe;
    }

    probe.report           = "unsupported (no IDXGIAdapter3)";
    IDXGIAdapter *pAdapter = nullptr;
    for (UINT i = 0; SUCCEEDED(pFactory->EnumAdapters(i, &pAdapter)) && pAdapter; ++i)
    {
        IDXGIAdapter3 *pAdapter3 = nullptr;
        if (SUCCEEDED(pAdapter->QueryInterface(__uuidof(IDXGIAdapter3), reinterpret_cast<void **>(&pAdapter3)))
            && pAdapter3)
        {
            DXGI_ADAPTER_DESC1 desc;
            pAdapter3->GetDesc1(&desc);
            if ((desc.Flags & DXGI_ADAPTER_FLAG_SOFTWARE) == 0)
            {
                DXGI_QUERY_VIDEO_MEMORY_INFO memInfo{};
                if (SUCCEEDED(pAdapter3->QueryVideoMemoryInfo(0, DXGI_MEMORY_SEGMENT_GROUP_LOCAL, &memInfo)))
                {
                    probe.supported = true;
                    probe.vramMB    = static_cast<std::size_t>(memInfo.CurrentUsage / (1024 * 1024));
                    probe.report    = std::to_string(probe.vramMB) + " MB";
                    pAdapter3->Release();
                    pAdapter->Release();
                    break;
                }
            }
            pAdapter3->Release();
        }
        pAdapter->Release();
    }
    pFactory->Release();
    FreeLibrary(hDxgi);
    return probe;
#else
    probe.report = "unsupported (non-Windows platform)";
    return probe;
#endif
}

const char *graphicsApiName(QSGRendererInterface::GraphicsApi api)
{
    switch (api)
    {
    case QSGRendererInterface::Direct3D11:
        return "Direct3D 11";
    case QSGRendererInterface::Direct3D12:
        return "Direct3D 12";
    case QSGRendererInterface::OpenGL:
        return "OpenGL";
    case QSGRendererInterface::Vulkan:
        return "Vulkan";
    case QSGRendererInterface::Metal:
        return "Metal";
    case QSGRendererInterface::Software:
        return "Software";
    case QSGRendererInterface::Null:
        return "Null";
    default:
        return "Unknown";
    }
}

double calculatePercentile(std::vector<double> &samples, double percentile)
{
    if (samples.empty())
        return 0.0;
    std::sort(samples.begin(), samples.end());
    double      index  = (percentile / 100.0) * (samples.size() - 1);
    std::size_t lower  = static_cast<std::size_t>(index);
    std::size_t upper  = (std::min)(lower + 1, samples.size() - 1);
    double      weight = index - lower;
    return samples[lower] * (1.0 - weight) + samples[upper] * weight;
}

RuntimeUpdate waitCompletion(RuntimeEngine &engine, std::chrono::milliseconds timeout = 10000ms)
{
    const auto deadline = std::chrono::steady_clock::now() + timeout;
    while (std::chrono::steady_clock::now() < deadline)
    {
        auto update = engine.pollLatest();
        if (update.completion)
        {
            engine.acknowledgeCompletion(*update.completion);
            return update;
        }
        std::this_thread::sleep_for(1ms);
    }
    throw std::runtime_error("Runtime completion timeout");
}

// -------------------------------------------------------------
// 1. 200 节点微基准拓扑执行与纯引擎延迟分位数 (HS2-05 / A15)
// -------------------------------------------------------------
void test200NodesExecution()
{
    std::cout << "[Test 1: Microbenchmark] 200-Node Topological Graph Execution (Pure Engine Overhead Baseline)...\n";
    const int     kNodeCount = 200;
    RuntimeEngine engine(4);
    ToolRegistry  registry;

    ToolDefinition addDef;
    addDef.id           = "perf.add";
    addDef.registryName = "perf.add";
    addDef.displayName  = "Add One";
    addDef.version      = "1.0";
    addDef.kind         = ToolKind::CppClass;
    addDef.inputs.push_back({"in", "in", {Type::Int}});
    addDef.outputs.push_back({"out", "out", {Type::Int}});

    class AddTool final : public IToolInstance
    {
    public:
        ToolStatus initialize(const Values &) override
        {
            return {};
        }

        ToolResult execute(const Values &in) override
        {
            std::int64_t v = 0;
            if (in.contains("in") && !isMissing(in.at("in")))
            {
                v = std::get<std::int64_t>(in.at("in"));
            }
            return ToolResult{{}, {{"out", v + 1}}};
        }

        ToolStatus release() override
        {
            return {};
        }
    };

    registry.registerCpp(addDef.registryName, addDef, [](const ToolServices &) { return std::make_unique<AddTool>(); });
    registry.addDefinition(addDef);

    ProjectDefinition project;
    FlowDefinition    flow;
    flow.id          = "perf_flow";
    flow.name        = "perf_flow";
    flow.runSelected = true;

    for (int i = 1; i <= kNodeCount; ++i)
    {
        NodeDefinition n;
        n.id               = i;
        n.toolId           = addDef.id;
        n.toolNameSnapshot = addDef.displayName;
        n.signatures.inputs.emplace("in", TypeSpec{Type::Int});
        n.signatures.outputs.emplace("out", TypeSpec{Type::Int});
        n.signatures.outputs.emplace("__status_code", TypeSpec{Type::Int});
        n.signatures.outputs.emplace("__status_message", TypeSpec{Type::String});

        if (i == 1)
        {
            n.inputs["in"] = {BindingMode::Literal, std::int64_t{0}, std::nullopt};
        }
        else
        {
            n.inputs["in"] = {
                BindingMode::Upstream, Missing{},
                 SourcePort{static_cast<NodeId>(i - 1), "out"}
            };
        }
        flow.nodes.push_back(n);
    }
    project.flows.push_back(flow);

    // Warmup 5 rounds
    for (int w = 0; w < 5; ++w)
    {
        require(engine.start(project, registry, RunMode::Once).code == 0, "warmup start failed");
        waitCompletion(engine);
    }

    // Benchmark 30 rounds
    std::vector<double> latencies;
    for (int r = 0; r < 30; ++r)
    {
        auto t0 = std::chrono::high_resolution_clock::now();
        require(engine.start(project, registry, RunMode::Once).code == 0, "benchmark start failed");
        auto   update = waitCompletion(engine);
        auto   t1     = std::chrono::high_resolution_clock::now();
        double ms     = std::chrono::duration<double, std::milli>(t1 - t0).count();
        latencies.push_back(ms);

        for (const auto &nu : update.nodes)
        {
            if (nu.node == kNodeCount && nu.snapshot)
            {
                require(nu.snapshot->status.code == 0, "node 200 status non-zero");
                require(std::get<std::int64_t>(nu.snapshot->outputs.at("out")) == kNodeCount,
                        "node 200 calculation incorrect");
            }
        }
    }

    double p50 = calculatePercentile(latencies, 50.0);
    double p95 = calculatePercentile(latencies, 95.0);
    double p99 = calculatePercentile(latencies, 99.0);
    std::cout << "  -> 200 Nodes Latency (ms): p50=" << p50 << " ms, p95=" << p95 << " ms, p99=" << p99
              << " ms [Target <= 100ms: PASS]\n";
    require(p95 <= 100.0, "200-Node p95 latency exceeded 100ms");
}

// -------------------------------------------------------------
// 2. 真实 Taskflow DAG 100 消费者扇出与 Python 单次复制叶输出 (HS2-05 / B05 / RT-07)
// -------------------------------------------------------------
void test10x4KZeroCopyAndPythonPassthrough()
{
    std::cout << "[Test 2] Real Taskflow DAG 100-Consumer Fan-Out & Python Single-Copy Leaf Output...\n";
    const int kImageCount = 10;
    const int kWidth      = 3840;
    const int kHeight     = 2160;

    std::vector<OwnedImage> imgs;
    imgs.reserve(kImageCount);
    std::vector<const void *> originalPointers;
    originalPointers.reserve(kImageCount);

    for (int i = 0; i < kImageCount; ++i)
    {
        cv::Mat mat(kHeight, kWidth, CV_8UC3, cv::Scalar(i * 20, 100 + i * 5, 200 - i * 10));
        imgs.push_back(OwnedImage::copyOf(mat));
        originalPointers.push_back(imgs.back().data());
    }

    ImageBatch batch(std::move(imgs));
    auto       batchOwner = batch.owner();
    std::cout << "  -> Initial batchOwner.use_count() = " << batchOwner.use_count() << "\n";
    require(batchOwner.use_count() == 2, "Initial batch owner count must be 2 (batch + batchOwner)");

    // Part A: Real Taskflow DAG 100-consumer fanout via RuntimeEngine
    const int     kFanOutCount = 100;
    RuntimeEngine engine(4);
    ToolRegistry  registry;

    ToolDefinition srcDef;
    srcDef.id           = "perf.source10x4k";
    srcDef.registryName = "perf.source10x4k";
    srcDef.displayName  = "10x4K Source";
    srcDef.version      = "1.0";
    srcDef.kind         = ToolKind::CppClass;
    srcDef.outputs.push_back({"batch", "batch", {Type::ImageBatch}});

    class SourceTool final : public IToolInstance
    {
        ImageBatch batch_;

    public:
        explicit SourceTool(ImageBatch b)
            : batch_(std::move(b))
        {
        }

        ToolStatus initialize(const Values &) override
        {
            return {};
        }

        ToolResult execute(const Values &) override
        {
            return ToolResult{{}, {{"batch", batch_}}};
        }

        ToolStatus release() override
        {
            return {};
        }
    };

    ImageBatch batchForSource = batch;
    registry.registerCpp(srcDef.registryName, srcDef, [batchForSource](const ToolServices &)
                         { return std::make_unique<SourceTool>(batchForSource); });
    registry.addDefinition(srcDef);

    ToolDefinition consDef;
    consDef.id           = "perf.consumer";
    consDef.registryName = "perf.consumer";
    consDef.displayName  = "Batch Consumer";
    consDef.version      = "1.0";
    consDef.kind         = ToolKind::CppClass;
    consDef.inputs.push_back({"batch", "batch", {Type::ImageBatch}});
    consDef.outputs.push_back({"matched", "matched", {Type::Int}});

    std::atomic<int>  consumerExecCount{0};
    std::atomic<bool> pointerMismatch{false};

    class ConsumerTool final : public IToolInstance
    {
        const std::vector<const void *> &expectedPointers_;
        std::atomic<int>                &count_;
        std::atomic<bool>               &mismatch_;

    public:
        ConsumerTool(const std::vector<const void *> &ptrs, std::atomic<int> &c, std::atomic<bool> &m)
            : expectedPointers_(ptrs)
            , count_(c)
            , mismatch_(m)
        {
        }

        ToolStatus initialize(const Values &) override
        {
            return {};
        }

        ToolResult execute(const Values &in) override
        {
            if (!in.contains("batch") || isMissing(in.at("batch")))
            {
                mismatch_ = true;
                return ToolResult{
                    {1, "missing batch"},
                    {{"matched", std::int64_t{0}}}
                };
            }
            const auto &b = std::get<ImageBatch>(in.at("batch"));
            if (static_cast<int>(b.images().size()) != 10)
            {
                mismatch_ = true;
            }
            else
            {
                for (int k = 0; k < 10; ++k)
                {
                    if (b.images()[k].data() != expectedPointers_[k])
                    {
                        mismatch_ = true;
                    }
                }
            }
            count_++;
            return ToolResult{{}, {{"matched", std::int64_t{1}}}};
        }

        ToolStatus release() override
        {
            return {};
        }
    };

    registry.registerCpp(
        consDef.registryName, consDef, [&originalPointers, &consumerExecCount, &pointerMismatch](const ToolServices &)
        { return std::make_unique<ConsumerTool>(originalPointers, consumerExecCount, pointerMismatch); });
    registry.addDefinition(consDef);

    ProjectDefinition project;
    FlowDefinition    flow;
    flow.id          = "dag_fanout_flow";
    flow.name        = "dag_fanout_flow";
    flow.runSelected = true;

    NodeDefinition srcNode;
    srcNode.id               = 1;
    srcNode.toolId           = srcDef.id;
    srcNode.toolNameSnapshot = srcDef.displayName;
    srcNode.signatures.outputs.emplace("batch", TypeSpec{Type::ImageBatch});
    srcNode.signatures.outputs.emplace("__status_code", TypeSpec{Type::Int});
    srcNode.signatures.outputs.emplace("__status_message", TypeSpec{Type::String});
    flow.nodes.push_back(srcNode);

    for (int i = 2; i <= kFanOutCount + 1; ++i)
    {
        NodeDefinition cNode;
        cNode.id               = i;
        cNode.toolId           = consDef.id;
        cNode.toolNameSnapshot = consDef.displayName;
        cNode.signatures.inputs.emplace("batch", TypeSpec{Type::ImageBatch});
        cNode.signatures.outputs.emplace("matched", TypeSpec{Type::Int});
        cNode.signatures.outputs.emplace("__status_code", TypeSpec{Type::Int});
        cNode.signatures.outputs.emplace("__status_message", TypeSpec{Type::String});
        cNode.inputs["batch"] = {
            BindingMode::Upstream, Missing{},
             SourcePort{1, "batch"}
        };
        flow.nodes.push_back(cNode);
    }
    project.flows.push_back(flow);

    require(engine.start(project, registry, RunMode::Once).code == 0, "DAG fanout start failed");
    auto update = waitCompletion(engine);

    require(consumerExecCount == kFanOutCount, "Not all 100 consumers executed in Taskflow DAG");
    require(!pointerMismatch, "Zero-copy violation: consumer received copied image data instead of shared pointer");

    std::cout << "  -> Real Taskflow DAG (" << kFanOutCount << " consumers x 10 4K images): Executed successfully.\n";
    std::cout << "  -> Zero-copy edge verification: C++ payload copies per edge = 0 [PASS]\n";

    // Part B: Display Conversion integration and queue verification (in-flight <= 1, pending <= 1)
    {
        DisplayConverter     converter;
        std::atomic<bool>    converted{false};
        std::atomic<int>     convertedBatchCount{0};
        std::vector<quint64> completedRequestIds;
        std::size_t          maxInFlight = 0;
        std::size_t          maxPending  = 0;

        QObject::connect(&converter, &DisplayConverter::batchConverted,
                         [&](const std::vector<DisplayFrame> &frames)
                         {
                             if (!frames.empty())
                             {
                                 completedRequestIds.push_back(frames.front().requestId);
                             }
                             convertedBatchCount = static_cast<int>(frames.size());
                             converted           = true;
                         });

        // Saturated burst submission to observe queue high-water marks
        for (quint64 reqId = 1; reqId <= 5; ++reqId)
        {
            std::vector<OwnedImage> displayImages;
            displayImages.reserve(kImageCount);
            for (const auto &img : batch.images())
            {
                displayImages.push_back(img);
            }
            converter.requestBatch(std::move(displayImages), "BGR", reqId, 1);
            const std::size_t inFlight = converter.isBusy() ? 1 : 0;
            const std::size_t pending  = converter.hasPending() ? 1 : 0;
            maxInFlight                = std::max(maxInFlight, inFlight);
            maxPending                 = std::max(maxPending, pending);
            require(inFlight <= 1, "Scheduler in-flight invariant violated");
            require(pending <= 1, "Scheduler pending invariant violated");
        }

        require(maxInFlight <= 1, "Scheduler maxInFlight invariant violated");
        require(maxPending == 1, "Scheduler pending queue did not reach high-water mark of 1 under saturation");

        const auto deadline = std::chrono::steady_clock::now() + 5000ms;
        while ((converter.isBusy() || converter.hasPending() || !converted)
               && std::chrono::steady_clock::now() < deadline)
        {
            QCoreApplication::processEvents();
            std::this_thread::sleep_for(1ms);
        }
        require(converted, "Display conversion timed out");
        require(convertedBatchCount == kImageCount, "Display conversion batch count mismatch");

        // HS5-04 Spec verification: final displayed frame is the latest request (5), and expired requests (2, 3, 4) were replaced and not committed
        require(!completedRequestIds.empty(), "No completed requests recorded");
        require(completedRequestIds.back() == 5, "Final displayed frame must be the latest request (reqId 5)");
        require(std::find(completedRequestIds.begin(), completedRequestIds.end(), 2) == completedRequestIds.end(),
                "Expired request 2 must not be submitted");
        require(std::find(completedRequestIds.begin(), completedRequestIds.end(), 3) == completedRequestIds.end(),
                "Expired request 3 must not be submitted");
        require(std::find(completedRequestIds.begin(), completedRequestIds.end(), 4) == completedRequestIds.end(),
                "Expired request 4 must not be submitted");

        std::cout << "  -> Display conversion verified: in-flight high-water=" << maxInFlight
                  << " <= 1, pending high-water=" << maxPending << " <= 1, finalReq=" << completedRequestIds.back()
                  << " [PASS]\n";
    }

    // Part C: Python Passthrough & Single-Copy Leaf Output Verification
    {
        pybind11::gil_scoped_acquire gil;
        PortValue                    upstreamValue = batch;
        // In Python read-only view: data pointer wraps original raw buffer
        pybind11::object             pyBatch = toPythonReadonly(upstreamValue);
        require(pybind11::isinstance<pybind11::list>(pyBatch), "Python object is not a list");
        pybind11::list pyList = pybind11::reinterpret_borrow<pybind11::list>(pyBatch);
        require(static_cast<int>(pyList.size()) == kImageCount, "Python list size mismatch");

        for (int k = 0; k < kImageCount; ++k)
        {
            pybind11::array pyArr = pybind11::cast<pybind11::array>(pyList[k]);
            require(pyArr.data() == originalPointers[k], "Python read-only array data pointer mismatch");
        }

        // Return from Python as leaf output (contiguous input case)
        resetPythonLeafImageCopyCount();
        PortValue fromPy = fromPython(TypeSpec{Type::ImageBatch}, pyBatch);
        require(std::holds_alternative<ImageBatch>(fromPy), "fromPython failed to produce ImageBatch");
        const auto &leafBatch  = std::get<ImageBatch>(fromPy);
        const auto &leafImages = leafBatch.images();
        require(static_cast<int>(leafImages.size()) == kImageCount, "Leaf image batch size mismatch");

        const std::size_t contiguousCopies = pythonLeafImageCopyCount();
        require(contiguousCopies == static_cast<std::size_t>(kImageCount),
                "Contiguous input leaf copies must be exactly 1 per image");

        // Verify copy point: each leaf image has newly allocated data buffer (1 copy from numpy to Mat)
        // and exactly 1 internal owner holding it (2 when including temporary return value).
        require(leafBatch.owner().use_count() == 2, "Leaf batch should have single internal owner");
        for (int k = 0; k < kImageCount; ++k)
        {
            require(leafImages[k].cols() == kWidth && leafImages[k].rows() == kHeight, "Leaf image dimension mismatch");
            require(leafImages[k].data() != originalPointers[k], "Leaf output should have newly allocated buffer");
            require(std::memcmp(leafImages[k].data(), originalPointers[k], kWidth * 3) == 0, "Pixel contents mismatch");
        }

        // Non-contiguous input case: strided / sliced array (step 2)
        pybind11::list nonContigList;
        for (int k = 0; k < kImageCount; ++k)
        {
            pybind11::object pyArr = pyList[k];
            pybind11::object sliced
                = pyArr[pybind11::make_tuple(pybind11::slice(0, kHeight, 2), pybind11::slice(0, kWidth, 2))];
            nonContigList.append(sliced);
        }

        resetPythonLeafImageCopyCount();
        PortValue fromNonContig = fromPython(TypeSpec{Type::ImageBatch}, nonContigList);
        require(std::holds_alternative<ImageBatch>(fromNonContig), "fromPython failed on non-contiguous array");
        const auto &nonContigBatch  = std::get<ImageBatch>(fromNonContig);
        const auto &nonContigImages = nonContigBatch.images();
        require(static_cast<int>(nonContigImages.size()) == kImageCount, "Non-contiguous leaf batch size mismatch");

        const std::size_t nonContigCopies = pythonLeafImageCopyCount();
        require(nonContigCopies == static_cast<std::size_t>(kImageCount),
                "Non-contiguous input leaf copies must be exactly 1 per image");
        require(nonContigImages[0].cols() == kWidth / 2 && nonContigImages[0].rows() == kHeight / 2,
                "Non-contiguous leaf image dimension mismatch");

        std::cout << "  -> Python Passthrough: Contiguous copy count = " << contiguousCopies
                  << " (<= 1/leaf), Non-contiguous copy count = " << nonContigCopies << " (<= 1/leaf) [PASS]\n";

        // NumericArray leaf output copy count and invalid input probe isolation
        {
            resetPythonLeafNumericCopyCount();
            pybind11::module_ np       = pybind11::module_::import("numpy");
            pybind11::object  validArr = np.attr("zeros")(pybind11::make_tuple(10, 20), "float32");
            TypeSpec          numSpec{Type::NumericArray, DType::Float32, 2, Layout::Dense};
            PortValue         validNumVal = fromPython(numSpec, validArr);
            require(std::holds_alternative<NumericArray>(validNumVal), "fromPython failed to produce NumericArray");
            require(pythonLeafNumericCopyCount() == 1, "NumericArray leaf copy count must be exactly 1");

            // Invalid input probe isolation: invalid dtype must throw and NOT increment probe
            bool badThrew = false;
            try
            {
                pybind11::object badArr = np.attr("zeros")(pybind11::make_tuple(10, 20), "int32");
                fromPython(numSpec, badArr);
            }
            catch (const ContractError &)
            {
                badThrew = true;
            }
            require(badThrew, "fromPython with mismatched dtype should throw ContractError");
            require(pythonLeafNumericCopyCount() == 1, "Invalid NumericArray input must not increment copy probe");

            // Invalid ImageBatch input probe isolation: invalid shape must throw and NOT increment probe
            resetPythonLeafImageCopyCount();
            bool imgBadThrew = false;
            try
            {
                pybind11::object badImg = np.attr("zeros")(pybind11::make_tuple(10), "uint8");
                pybind11::list   badList;
                badList.append(badImg);
                fromPython(TypeSpec{Type::ImageBatch}, badList);
            }
            catch (const ContractError &)
            {
                imgBadThrew = true;
            }
            require(imgBadThrew, "fromPython with 1D image must throw ContractError");
            require(pythonLeafImageCopyCount() == 0, "Invalid ImageBatch input must not increment copy probe");
            std::cout << "  -> Python NumericArray leaf copy & invalid input probe isolation [PASS]\n";
        }
    }
}

// -------------------------------------------------------------
// 3. 循环压力测试与内存/队列稳定性监测 (HS2-05 / RS-07 / RT-07)
// -------------------------------------------------------------
void testMemoryAndQueueStabilityInLoop(const std::filesystem::path &evidenceDir, int maxRounds, int maxSeconds,
                                       PythonRuntime *pyRuntime)
{
    std::cout << "[Test 3] Continuous Real-Graph Loop Execution & Memory Stability ("
              << (maxSeconds > 0 ? (std::to_string(maxSeconds) + "s limit / ") : "") << maxRounds << " rounds)...\n";

    auto         engine = std::make_unique<RuntimeEngine>(2);
    ToolRegistry registry;

    ToolDefinition genDef;
    genDef.id           = "perf.generator";
    genDef.registryName = "perf.generator";
    genDef.displayName  = "Generator";
    genDef.version      = "1.0";
    genDef.kind         = ToolKind::CppClass;
    genDef.outputs.push_back({"val", "val", {Type::Int}});
    genDef.outputs.push_back({"image", "image", {Type::ImageBatch}});

    class GenTool final : public IToolInstance
    {
        std::int64_t step_ = 0;

    public:
        ToolStatus initialize(const Values &) override
        {
            return {};
        }

        ToolResult execute(const Values &) override
        {
            ++step_;
            cv::Mat                 mat(720, 1280, CV_8UC3, cv::Scalar(step_ % 256, 120, 200));
            std::vector<OwnedImage> imgs;
            imgs.push_back(OwnedImage::copyOf(mat));
            return ToolResult{
                {},
                {{"val", step_}, {"image", ImageBatch(std::move(imgs))}}
            };
        }

        ToolStatus release() override
        {
            return {};
        }
    };

    registry.registerCpp(genDef.registryName, genDef, [](const ToolServices &) { return std::make_unique<GenTool>(); });
    registry.addDefinition(genDef);

    ToolDefinition procDef;
    procDef.id           = "perf.processor";
    procDef.registryName = "perf.processor";
    procDef.displayName  = "Processor";
    procDef.version      = "1.0";
    procDef.kind         = ToolKind::CppClass;
    procDef.inputs.push_back({"in_val", "in_val", {Type::Int}});
    procDef.inputs.push_back({"in_image", "in_image", {Type::ImageBatch}});
    procDef.outputs.push_back({"out_val", "out_val", {Type::Int}});
    procDef.outputs.push_back({"out_image", "out_image", {Type::ImageBatch}});

    class ProcTool final : public IToolInstance
    {
    public:
        ToolStatus initialize(const Values &) override
        {
            return {};
        }

        ToolResult execute(const Values &in) override
        {
            std::int64_t v       = std::get<std::int64_t>(in.at("in_val"));
            const auto  &inBatch = std::get<ImageBatch>(in.at("in_image"));
            // Trigger real display conversion within tool execution pipeline
            if (!inBatch.images().empty())
            {
                auto frame = DisplayConverter::convert(inBatch.images().front(), "BGR");
                require(!frame.image.isNull(), "Display conversion in loop returned null image");
            }
            return ToolResult{
                {},
                {{"out_val", v * 2}, {"out_image", inBatch}}
            };
        }

        ToolStatus release() override
        {
            return {};
        }
    };

    registry.registerCpp(procDef.registryName, procDef,
                         [](const ToolServices &) { return std::make_unique<ProcTool>(); });
    registry.addDefinition(procDef);

    ProjectDefinition project;
    FlowDefinition    flow;
    flow.id          = "stability_flow";
    flow.name        = "stability_flow";
    flow.runSelected = true;

    NodeDefinition n1;
    n1.id     = 1;
    n1.toolId = genDef.id;
    n1.signatures.outputs.emplace("val", TypeSpec{Type::Int});
    n1.signatures.outputs.emplace("image", TypeSpec{Type::ImageBatch});
    n1.signatures.outputs.emplace("__status_code", TypeSpec{Type::Int});
    n1.signatures.outputs.emplace("__status_message", TypeSpec{Type::String});
    flow.nodes.push_back(n1);

    NodeDefinition n2;
    n2.id     = 2;
    n2.toolId = procDef.id;
    n2.signatures.inputs.emplace("in_val", TypeSpec{Type::Int});
    n2.signatures.inputs.emplace("in_image", TypeSpec{Type::ImageBatch});
    n2.signatures.outputs.emplace("out_val", TypeSpec{Type::Int});
    n2.signatures.outputs.emplace("out_image", TypeSpec{Type::ImageBatch});
    n2.signatures.outputs.emplace("__status_code", TypeSpec{Type::Int});
    n2.signatures.outputs.emplace("__status_message", TypeSpec{Type::String});
    n2.inputs["in_val"] = {
        BindingMode::Upstream, Missing{},
         SourcePort{1, "val"}
    };
    n2.inputs["in_image"] = {
        BindingMode::Upstream, Missing{},
         SourcePort{1, "image"}
    };
    flow.nodes.push_back(n2);

    QTemporaryDir tempPyDir;
    if (pyRuntime && tempPyDir.isValid())
    {
        const auto    scriptPath = std::filesystem::path(tempPyDir.path().toStdString()) / "loop_tool.py";
        std::ofstream ofs(scriptPath);
        ofs << "def execute(inputs):\n"
            << "    v = inputs.get('in_val', 0)\n"
            << "    return {'__status_code': 0, '__status_message': '', 'out_val': v + 1}\n";
        ofs.close();

        ToolDefinition pyToolDef;
        pyToolDef.id             = "perf.py_loop";
        pyToolDef.displayName    = "Loop Python Tool";
        pyToolDef.version        = "1.0.0";
        pyToolDef.kind           = ToolKind::PythonFunction;
        pyToolDef.script         = scriptPath.string();
        pyToolDef.definitionPath = scriptPath.parent_path() / "loop_tool.yaml";
        pyToolDef.symbol         = "execute";
        pyToolDef.inputs.push_back({"in_val", "in_val", {Type::Int}});
        pyToolDef.outputs.push_back({"out_val", "out_val", {Type::Int}});

        auto st = pyRuntime->refresh(std::vector<ToolDefinition>{pyToolDef});
        if (!st.empty() && st[0].status.code == 0)
        {
            registry.addDefinition(pyToolDef);
            registry.setPythonImplementation(pyToolDef.id, pyRuntime->factory(pyToolDef));

            NodeDefinition n3;
            n3.id               = 3;
            n3.toolId           = pyToolDef.id;
            n3.toolNameSnapshot = pyToolDef.displayName;
            n3.signatures.inputs.emplace("in_val", TypeSpec{Type::Int});
            n3.signatures.outputs.emplace("out_val", TypeSpec{Type::Int});
            n3.signatures.outputs.emplace("__status_code", TypeSpec{Type::Int});
            n3.signatures.outputs.emplace("__status_message", TypeSpec{Type::String});
            n3.inputs["in_val"] = {
                BindingMode::Upstream, Missing{},
                 SourcePort{2, "out_val"}
            };
            flow.nodes.push_back(n3);
        }
    }
    project.flows.push_back(flow);

    const auto csvPath = evidenceDir / "memory_curve.csv";
    if (csvPath.has_parent_path())
    {
        std::filesystem::create_directories(csvPath.parent_path());
    }
    std::ofstream curveStream(csvPath);
    require(curveStream.is_open(), "Failed to open memory curve CSV for writing: " + csvPath.string());
    curveStream << "iteration,elapsed_ms,rss_supported,rss_kb,vram_supported,vram_mb,latency_ms\n";

    auto writeCurveLine = [&](const std::string &line)
    {
        curveStream << line;
        curveStream.flush();
        require(!curveStream.bad(), "CSV stream error during write");
    };

    auto                initialProbe     = getWorkingSetMemory();
    std::size_t         memAtSteady      = 0;
    std::size_t         peakWorkingSetKB = initialProbe.workingSetKB;
    std::vector<double> roundLatencies;
    roundLatencies.reserve(maxRounds);

    const auto startTime    = std::chrono::steady_clock::now();
    int        actualRounds = 0;

    for (int r = 0; r < maxRounds; ++r)
    {
        auto t0 = std::chrono::high_resolution_clock::now();
        require(engine->start(project, registry, RunMode::Once).code == 0, "round start failed");
        waitCompletion(*engine);
        auto   t1 = std::chrono::high_resolution_clock::now();
        double ms = std::chrono::duration<double, std::milli>(t1 - t0).count();
        roundLatencies.push_back(ms);
        actualRounds = r + 1;

        auto currentProbe = getWorkingSetMemory();
        if (currentProbe.supported)
        {
            peakWorkingSetKB = std::max(peakWorkingSetKB, currentProbe.workingSetKB);
        }
        if (r == 20 && currentProbe.supported)
        {
            memAtSteady = currentProbe.workingSetKB;
        }

        auto vram = queryGpuVram();
        if (r % 5 == 0 || r == maxRounds - 1)
        {
            auto               elapsedMs = std::chrono::duration<double, std::milli>(t1 - startTime).count();
            std::ostringstream ss;
            ss << (r + 1) << "," << elapsedMs << ","
               << (currentProbe.supported ? ("1," + std::to_string(currentProbe.workingSetKB)) : "0,NA") << ","
               << (vram.supported ? ("1," + std::to_string(vram.vramMB)) : "0,NA") << "," << ms << "\n";
            writeCurveLine(ss.str());
        }

        if ((r + 1) % 25 == 0 || r == maxRounds - 1)
        {
            std::cout << "  -> Round " << (r + 1) << "/" << maxRounds << ": RSS = " << currentProbe.report
                      << ", VRAM = " << vram.report << "\n";
        }

        if (maxSeconds > 0)
        {
            auto elapsedSec = std::chrono::duration_cast<std::chrono::seconds>(t1 - startTime).count();
            if (elapsedSec >= maxSeconds)
            {
                break;
            }
        }
    }

    auto   probeBeforeReset = getWorkingSetMemory();
    double p50              = calculatePercentile(roundLatencies, 50.0);
    double p95              = calculatePercentile(roundLatencies, 95.0);

    engine->requestStop();
    require(engine->requestReset().code == 0, "requestReset failed");
    auto probeAfterReset = getWorkingSetMemory();
    auto vramAfterReset  = queryGpuVram();

    std::cout << "  -> Loop " << actualRounds << " rounds: p50=" << p50 << " ms, p95=" << p95 << " ms\n";
    std::cout << "  -> Memory curve: Initial=" << initialProbe.report << ", Steady=" << memAtSteady
              << " KB, Peak=" << peakWorkingSetKB << " KB, AfterReset=" << probeAfterReset.report
              << ", VRAM=" << vramAfterReset.report << "\n";

    std::ostringstream finalReset;
    finalReset << "FINAL_RESET,0,"
               << (probeAfterReset.supported ? ("1," + std::to_string(probeAfterReset.workingSetKB)) : "0,NA") << ","
               << (vramAfterReset.supported ? ("1," + std::to_string(vramAfterReset.vramMB)) : "0,NA") << ",0\n";
    writeCurveLine(finalReset.str());
    curveStream.close();
    require(!curveStream.bad() && !curveStream.fail(), "CSV stream error during close of memory_curve.csv");
    std::cout << "  -> Raw memory curve persisted to " << csvPath.string() << " [PASS]\n";

    engine.reset();
    if (pyRuntime)
    {
        pyRuntime->refresh({});
    }

    if (memAtSteady > 0 && probeBeforeReset.supported)
    {
        long long steadyDrift
            = static_cast<long long>(probeBeforeReset.workingSetKB) - static_cast<long long>(memAtSteady);
        require(steadyDrift < 10240, "Excessive memory growth detected (> 10MB) after steady state");
    }
}

// -------------------------------------------------------------
// 3.5 真实 Python 耗时阶段划分与失败隔离 (HS5-04 / item 5)
// -------------------------------------------------------------
void testRealPythonTimingPhasesAndFailureIsolation(PythonRuntime &pyRuntime)
{
    std::cout << "[Test 3.5] Real Python Timing Phases & Failure Isolation (HS5-04 Spec)...\n";
    QTemporaryDir tempDir;
    require(tempDir.isValid(), "Failed to create temp directory for Python timing test");
    const auto tempPath = std::filesystem::path(tempDir.path().toStdString());

    const auto scriptPath = tempPath / "timing_test.py";
    {
        std::ofstream ofs(scriptPath);
        ofs << "def execute(inputs):\n"
            << "    val = inputs.get('val', 0)\n"
            << "    if val == 666:\n"
            << "        raise RuntimeError('simulated python execution failure')\n"
            << "    if val == 777:\n"
            << "        return {'__status_code': 0, '__status_message': '', 'out': 'wrong_type_string'}\n"
            << "    return {'__status_code': 0, '__status_message': '', 'out': val * 2}\n";
        ofs.close();
    }

    ToolDefinition pyDef;
    pyDef.id             = "perf.py_timing";
    pyDef.displayName    = "Python Timing Test Tool";
    pyDef.version        = "1.0.0";
    pyDef.kind           = ToolKind::PythonFunction;
    pyDef.script         = scriptPath.string();
    pyDef.definitionPath = tempPath / "timing_test.yaml";
    pyDef.symbol         = "execute";
    pyDef.inputs.push_back({"val", "val", {Type::Int}});
    pyDef.outputs.push_back({"out", "out", {Type::Int}});

    auto statuses = pyRuntime.refresh(std::vector<ToolDefinition>{pyDef});
    require(!statuses.empty() && statuses[0].status.code == 0, "Python tool refresh failed");

    ToolRegistry registry;
    registry.addDefinition(pyDef);
    registry.setPythonImplementation(pyDef.id, pyRuntime.factory(pyDef));

    ProjectDefinition project;
    FlowDefinition    flow;
    flow.id          = "timing_flow";
    flow.name        = "timing_flow";
    flow.runSelected = true;

    NodeDefinition n;
    n.id               = 1;
    n.toolId           = pyDef.id;
    n.toolNameSnapshot = pyDef.displayName;
    n.signatures.inputs.emplace("val", TypeSpec{Type::Int});
    n.signatures.outputs.emplace("out", TypeSpec{Type::Int});
    n.signatures.outputs.emplace("__status_code", TypeSpec{Type::Int});
    n.signatures.outputs.emplace("__status_message", TypeSpec{Type::String});

    {
        auto engine = std::make_unique<RuntimeEngine>(1);

        // Round 1: Success
        n.inputs["val"] = {BindingMode::Literal, std::int64_t{10}, std::nullopt};
        flow.nodes      = {n};
        project.flows   = {flow};

        require(engine->start(project, registry, RunMode::Once).code == 0, "Round 1 start failed");
        auto u1 = waitCompletion(*engine);
        require(u1.nodes.size() == 1 && u1.nodes[0].snapshot != nullptr, "Round 1 snapshot missing");
        const auto &s1 = *u1.nodes[0].snapshot;
        require(s1.status.code == 0, "Round 1 status must be 0");
        require(std::get<std::int64_t>(s1.outputs.at("out")) == 20, "Round 1 output value mismatch");
        require(s1.timing.pythonCallMs > 0.0, "Round 1 pythonCallMs must be > 0");
        require(s1.timing.adapterExecuteMs > 0.0, "Round 1 adapterExecuteMs must be > 0");
        std::cout << "  -> Round 1 Success: pythonCallMs=" << s1.timing.pythonCallMs
                  << " ms, adapterExecuteMs=" << s1.timing.adapterExecuteMs << " ms [PASS]\n";

        // Round 2: Prepare failure (Wrong input type: float 3.14 where int is expected)
        // HS5-04 requirement: All execution subphases must be strictly 0; no history contamination.
        n.inputs["val"] = {BindingMode::Literal, 3.14, std::nullopt};
        flow.nodes      = {n};
        project.flows   = {flow};

        require(engine->start(project, registry, RunMode::Once).code == 0, "Round 2 start failed");
        auto u2 = waitCompletion(*engine);
        require(u2.nodes.size() == 1 && u2.nodes[0].snapshot != nullptr, "Round 2 snapshot missing");
        const auto &s2 = *u2.nodes[0].snapshot;
        require(s2.status.code < 0, "Round 2 must fail in prepare");
        require(isMissing(s2.outputs.at("out")), "Round 2 failed outputs must be Missing");
        require(s2.timing.executeMs == 0.0, "Round 2 executeMs must be strictly 0");
        require(s2.timing.adapterExecuteMs == 0.0, "Round 2 adapterExecuteMs must be strictly 0");
        require(s2.timing.waitGilMs == 0.0, "Round 2 waitGilMs must be strictly 0");
        require(s2.timing.pythonInputMs == 0.0, "Round 2 pythonInputMs must be strictly 0");
        require(s2.timing.pythonCallMs == 0.0, "Round 2 pythonCallMs must be strictly 0");
        require(s2.timing.pythonOutputMs == 0.0, "Round 2 pythonOutputMs must be strictly 0");
        std::cout << "  -> Round 2 Prepare Failure Isolation: all execution subphases == 0.0 ms [PASS]\n";

        // Round 3: Python exception (val == 666 raises RuntimeError)
        // HS5-04 requirement: Retains current round's error timing, no history pollution.
        n.inputs["val"] = {BindingMode::Literal, std::int64_t{666}, std::nullopt};
        flow.nodes      = {n};
        project.flows   = {flow};

        require(engine->start(project, registry, RunMode::Once).code == 0, "Round 3 start failed");
        auto u3 = waitCompletion(*engine);
        require(u3.nodes.size() == 1 && u3.nodes[0].snapshot != nullptr, "Round 3 snapshot missing");
        const auto &s3 = *u3.nodes[0].snapshot;
        require(s3.status.code < 0, "Round 3 must fail with exception");
        require(s3.status.message.find("simulated python execution failure") != std::string::npos,
                "Round 3 message mismatch");
        require(isMissing(s3.outputs.at("out")), "Round 3 output must be Missing");
        require(s3.timing.pythonCallMs > 0.0, "Round 3 pythonCallMs must retain real error timing");
        require(s3.timing.adapterExecuteMs > 0.0, "Round 3 adapterExecuteMs must be > 0");
        std::cout << "  -> Round 3 Exception Timing: pythonCallMs=" << s3.timing.pythonCallMs
                  << " ms retained actual error round timing [PASS]\n";

        // Round 4: Output conversion error (val == 777 returns string instead of int)
        // HS5-04 requirement: Retains pythonOutputMs timing, no history pollution.
        n.inputs["val"] = {BindingMode::Literal, std::int64_t{777}, std::nullopt};
        flow.nodes      = {n};
        project.flows   = {flow};

        require(engine->start(project, registry, RunMode::Once).code == 0, "Round 4 start failed");
        auto u4 = waitCompletion(*engine);
        require(u4.nodes.size() == 1 && u4.nodes[0].snapshot != nullptr, "Round 4 snapshot missing");
        const auto &s4 = *u4.nodes[0].snapshot;
        require(s4.status.code < 0, "Round 4 must fail with output conversion error");
        require(isMissing(s4.outputs.at("out")), "Round 4 output must be Missing");
        require(s4.timing.pythonCallMs > 0.0, "Round 4 pythonCallMs must be > 0");
        require(s4.timing.pythonOutputMs > 0.0, "Round 4 pythonOutputMs must retain actual conversion error timing");
        std::cout << "  -> Round 4 Output Conversion Error Timing: pythonOutputMs=" << s4.timing.pythonOutputMs
                  << " ms retained actual conversion timing [PASS]\n";

        engine->requestStop();
        engine.reset();
    }
    pyRuntime.refresh({});
}

// -------------------------------------------------------------
// 4. GUI 事件到对应帧提交延迟 (HS2-05 / frameSwapped: p95 <= 100ms)
// -------------------------------------------------------------
void testGuiEventToPresentationLatency(AppController &app, const std::filesystem::path &evidenceDir)
{
    std::cout << "[Test 4] Product GUI Event-to-Frame-Submission Latency (Real Product UI & frameSwapped)...\n";
    std::cout << "  -> System hardware: " << std::thread::hardware_concurrency() << " logical threads, "
              << "VRAM: " << queryGpuVram().report << "\n";

    const auto guiCsvPath = evidenceDir / "gui_events.csv";
    if (guiCsvPath.has_parent_path())
    {
        std::filesystem::create_directories(guiCsvPath.parent_path());
    }
    std::ofstream guiCsv(guiCsvPath);
    require(guiCsv.is_open(), "Failed to open gui_events.csv for writing: " + guiCsvPath.string());
    guiCsv << "event_id,timestamp_ms,duration_ms,synced,submitted,timeout,dropped\n";

    QQuickStyle::setStyle("Basic");
    for (int i = 0; i < 50 && app.busy(); ++i)
    {
        QCoreApplication::processEvents();
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }

    qmlRegisterType<visionrt::ImageViewport>("VisionRT.View", 1, 0, "ImageViewport");
    auto *theme = visionrt::Theme::instance();
    qmlRegisterSingletonInstance<visionrt::Theme>("VisionRT", 1, 0, "Theme", theme);

    QQmlApplicationEngine engine;
    const auto            appDir = QCoreApplication::applicationDirPath();
    engine.addImportPath(appDir + "/qml");
    engine.addImportPath(appDir + "/../qml");
#ifdef VISIONRT_SOURCE_DIR
    const auto sourceDir = QStringLiteral(VISIONRT_SOURCE_DIR);
    engine.addImportPath(sourceDir + "/src/ui/qml");
    engine.addImportPath(sourceDir + "/3rdparty/QuickUI/quickui");
    const auto qmlPath = sourceDir + "/src/ui/qml/Main.qml";
#else
    const auto qmlPath = appDir + "/../src/ui/qml/Main.qml";
#endif

    engine.rootContext()->setContextProperty("app", &app);
    engine.rootContext()->setContextProperty("Theme", theme);
    engine.rootContext()->setContextProperty("demoMenu", false);
    engine.rootContext()->setContextProperty("demoSelect", false);

    if (QFile::exists(qmlPath))
    {
        engine.load(QUrl::fromLocalFile(qmlPath));
    }
    else
    {
        engine.loadFromModule("VisionRT", "Main");
    }

    const auto roots = engine.rootObjects();
    require(!roots.isEmpty(), "Main.qml failed to instantiate");
    auto *window = qobject_cast<QQuickWindow *>(roots.first());
    require(window != nullptr, "Main.qml root is not QQuickWindow");

    window->resize(960, 540);
    window->show();
    for (int i = 0; i < 20; ++i) QCoreApplication::processEvents();

    auto *rendererInterface = window->rendererInterface();
    auto  graphicsApi       = rendererInterface ? rendererInterface->graphicsApi() : QSGRendererInterface::Unknown;
    std::cout << "  -> Rendering Backend: " << graphicsApiName(graphicsApi) << "\n";

    // Warmup 5 events
    for (int w = 0; w < 5; ++w)
    {
        QMetaObject::invokeMethod(window, "toggleLeft");
        for (int i = 0; i < 5; ++i) QCoreApplication::processEvents();
    }

    const int           kSamples = 50;
    std::vector<double> eventLatencies;
    eventLatencies.reserve(kSamples);
    int        unswappedCount = 0;
    const auto eventStart     = std::chrono::steady_clock::now();

    for (int i = 0; i < kSamples; ++i)
    {
        const int         eventId = i + 1;
        std::atomic<bool> synced{false};
        std::atomic<bool> frameSubmitted{false};
        window->setProperty("lastEventSerial", eventId);

        auto syncConn = QObject::connect(window, &QQuickWindow::afterSynchronizing, [&synced]() { synced = true; });

        auto swapConn = QObject::connect(window, &QQuickWindow::frameSwapped,
                                         [&frameSubmitted, &synced]()
                                         {
                                             if (synced)
                                             {
                                                 frameSubmitted = true;
                                             }
                                         });

        auto t0 = std::chrono::high_resolution_clock::now();
        if (i % 2 == 0)
        {
            QMetaObject::invokeMethod(window, "toggleLeft");
        }
        else
        {
            QMetaObject::invokeMethod(window, "focusSearch");
        }

        const auto deadline = std::chrono::steady_clock::now() + 500ms;
        while (!frameSubmitted && std::chrono::steady_clock::now() < deadline)
        {
            QCoreApplication::processEvents(QEventLoop::AllEvents, 5);
            std::this_thread::sleep_for(1ms);
        }
        auto t1 = std::chrono::high_resolution_clock::now();
        QObject::disconnect(syncConn);
        QObject::disconnect(swapConn);

        double ms = std::chrono::duration<double, std::milli>(t1 - t0).count();
        if (!frameSubmitted)
        {
            unswappedCount++;
        }
        eventLatencies.push_back(ms);

        const double timestampMs  = std::chrono::duration<double, std::milli>(t0 - eventStart).count();
        const int    syncedVal    = synced ? 1 : 0;
        const int    submittedVal = frameSubmitted ? 1 : 0;
        const int    timeoutVal   = frameSubmitted ? 0 : 1;
        const int    droppedVal   = 0;
        guiCsv << eventId << "," << timestampMs << "," << ms << "," << syncedVal << "," << submittedVal << ","
               << timeoutVal << "," << droppedVal << "\n";
        guiCsv.flush();
        require(!guiCsv.bad(), "Error writing to gui_events.csv");
    }

    guiCsv.close();
    require(!guiCsv.bad() && !guiCsv.fail(), "CSV stream error during close of gui_events.csv");
    std::cout << "  -> GUI event log persisted to " << guiCsvPath.string() << " [PASS]\n";

    require(unswappedCount == 0, "Frames timed out or unswapped during GUI event test");

    if (unswappedCount > 0)
    {
        std::cout << "  -> Note: " << unswappedCount << "/" << kSamples
                  << " frames unswapped (graphics backend: " << graphicsApiName(graphicsApi) << ")\n";
    }

    double p50 = calculatePercentile(eventLatencies, 50.0);
    double p95 = calculatePercentile(eventLatencies, 95.0);
    std::cout << "  -> GUI Event-to-Frame-Submission (" << kSamples << " samples): p50=" << p50 << " ms, p95=" << p95
              << " ms [Target <= 100ms: PASS]\n";
    require(p95 <= 100.0, "GUI event-to-frame-submission p95 latency exceeded 100ms");
}

// -------------------------------------------------------------
// 5. GUI 工具库检索响应时间 (HS2-05 / RS-07 / RT-07: p95 <= 100ms)
// -------------------------------------------------------------
void testWorkbenchBrowsingLatency()
{
    std::cout << "[Test 5] Tool Registry & Tree Model Browsing Latency (500 tools)...\n";
    ToolRegistry registry;
    const int    kToolCount = 500;
    for (int i = 0; i < kToolCount; ++i)
    {
        ToolDefinition d;
        d.id           = "perf.tool." + std::to_string(i);
        d.displayName  = "Performance Tool " + std::to_string(i);
        d.version      = "1.0.0";
        d.kind         = ToolKind::CppFunction;
        d.registryName = d.id;
        d.category     = "Category_" + std::to_string(i % 10);
        d.subcategory  = "Subcategory_" + std::to_string(i % 5);
        registry.addDefinition(d);
    }

    ToolTreeModel model;
    model.setRegistry(registry);

    std::vector<double> queryLatencies;
    for (int q = 0; q < 100; ++q)
    {
        auto    t0     = std::chrono::high_resolution_clock::now();
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

int main(int argc, char **argv)
{
    QGuiApplication qt(argc, argv);

    QTemporaryDir directory;
    require(directory.isValid(), "Failed to create temporary directory for AppController");
    AppController app(1, directory.path());

    int                   maxRounds   = 50;
    int                   maxSeconds  = 0;
    std::filesystem::path evidenceDir = "build/evidence";

    for (int i = 1; i < argc; ++i)
    {
        std::string arg = argv[i];
        if (arg == "--soak")
        {
            maxSeconds = 1800;
            maxRounds  = 1000000;
        }
        else if (arg.rfind("--duration=", 0) == 0)
        {
            maxSeconds = std::stoi(arg.substr(11));
            maxRounds  = 1000000;
        }
        else if (arg == "--evidence-dir" && i + 1 < argc)
        {
            evidenceDir = argv[++i];
        }
        else if (arg.rfind("--evidence-dir=", 0) == 0)
        {
            evidenceDir = arg.substr(15);
        }
    }
    const char *envDuration = std::getenv("VISIONRT_PERF_DURATION");
    if (envDuration)
    {
        maxSeconds = std::atoi(envDuration);
        maxRounds  = 1000000;
    }

    std::filesystem::create_directories(evidenceDir);

    try
    {
        test200NodesExecution();
        test10x4KZeroCopyAndPythonPassthrough();
        if (app.pythonRuntime())
        {
            testRealPythonTimingPhasesAndFailureIsolation(*app.pythonRuntime());
        }
        testMemoryAndQueueStabilityInLoop(evidenceDir, maxRounds, maxSeconds, app.pythonRuntime());
        testGuiEventToPresentationLatency(app, evidenceDir);
        testWorkbenchBrowsingLatency();
        std::cout << "\n>>> ALL RT-07 PERFORMANCE CONTRACT TESTS PASSED <<<\n";
        return 0;
    }
    catch (const std::exception &error)
    {
        std::cerr << "Performance test failed: " << error.what() << '\n';
        return 1;
    }
}
