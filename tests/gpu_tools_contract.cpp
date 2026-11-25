#include "ExecutionEvidence.h"
#include "ToolResultAssertions.h"
#include "core/ProjectStorage.h"
#include "plugins/PluginLoader.h"
#include "runtime/RuntimeEngine.h"

#include <core/ToolPackage.h>

#include <QByteArray>
#include <QCoreApplication>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QTemporaryDir>
#include <array>
#include <chrono>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <iterator>
#include <map>
#include <stdexcept>
#include <string_view>
#include <thread>

#if defined(_WIN32)
#include <windows.h>
#endif

using namespace visionrt;
using namespace std::chrono_literals;

namespace {

// A machine without the CUDA driver (or without a device behind it) cannot run this contract. The
// probe goes through the driver API at runtime, so the test binary stays free of a CUDA link
// dependency and the result is an explicit NOT RUN instead of a pass on a filter that never ran.
int cudaDeviceCount()
{
#if defined(_WIN32)
    const HMODULE driver = LoadLibraryW(L"nvcuda.dll");
    if (!driver)
        return 0;
    using InitFunction    = int (*)(unsigned int);
    using CountFunction   = int (*)(int *);
    const auto init       = reinterpret_cast<InitFunction>(GetProcAddress(driver, "cuInit"));
    const auto count      = reinterpret_cast<CountFunction>(GetProcAddress(driver, "cuDeviceGetCount"));
    int        devices    = 0;
    if (!init || !count || init(0) != 0 || count(&devices) != 0)
        devices = 0;
    FreeLibrary(driver);
    return devices;
#else
    return 0;
#endif
}

} // namespace

int main(int argc, char **argv)
{
    QCoreApplication application(argc, argv);
    const auto arguments = QCoreApplication::arguments();
    try
    {
        if ((argc != 4 && argc != 5) || (argc == 5 && std::string_view(argv[4]) != "--engine-only"
                                         && std::string_view(argv[4]) != "--engine-output-error"))
            throw std::runtime_error("Usage: gpu_tools_contract <tools-package> <example-project> <fixture-directory> [--engine-only]");
        const bool outputError = argc == 5 && std::string_view(argv[4]) == "--engine-output-error";
        const bool buildWeights = argc == 4 || outputError;
        const auto fixtures = std::filesystem::absolute(std::filesystem::path(arguments.at(3).toStdU16String()));
        const auto weights = fixtures / "yolov8n.wts";
        const auto image = fixtures / "bus.jpg";
        const auto enginePath = fixtures / "yolov8n.engine";
        if (cudaDeviceCount() == 0)
        {
            std::cout << "[NOT RUN] no CUDA device on this machine\n";
            return 77;
        }
        if (!std::filesystem::is_regular_file(image)
            || (buildWeights && !std::filesystem::is_regular_file(weights)))
        {
            std::cout << "[NOT RUN] YOLO fixture is missing: " << fixtures << '\n';
            return 77;
        }
        // The engine-only lane reuses a previously built engine; a machine that never built one has
        // nothing to load, which is a missing fixture rather than a failed conversion.
        if (!buildWeights && !std::filesystem::is_regular_file(enginePath))
        {
            std::cout << "[NOT RUN] YOLO engine is missing: " << enginePath << '\n';
            return 77;
        }
        // A package built from a profile without inference carries no infer module: the contract
        // has nothing to measure there, and reporting it as a failure would blame the wrong thing.
        {
            const auto indexPath = std::filesystem::path(arguments.at(1).toStdU16String()) / "module_index.json";
            std::ifstream indexStream(indexPath, std::ios::binary);
            const auto    indexDocument = QJsonDocument::fromJson(QByteArray::fromStdString(
                std::string(std::istreambuf_iterator<char>(indexStream), std::istreambuf_iterator<char>())));
            const auto    modules       = indexDocument.object().value("modules").toArray();
            const bool    hasInfer = std::any_of(modules.begin(), modules.end(), [](const QJsonValue &module) {
                return module.toObject().value("id").toString().contains("infer", Qt::CaseInsensitive);
            });
            if (!hasInfer)
            {
                std::cout << "[NOT RUN] the tools package has no infer module: " << argv[1] << '\n';
                return 77;
            }
        }
        ToolRegistry registry;
        registry.setNativeProvider(std::make_shared<PluginLoader>());
        registry.registerPackage(readToolsPackage(std::filesystem::path(arguments.at(1).toStdU16String())));
        registry.loadNativeImplementations();
        ProjectStorage storage(registry);
        auto loaded = storage.load(std::filesystem::path(arguments.at(2).toStdU16String()));
        for (const auto &diagnostic : loaded.diagnostics)
            std::cerr << "GPU project diagnostic: " << diagnostic.field << ": " << diagnostic.message << '\n';
        if (!loaded.diagnostics.empty() || loaded.project.flows.size() != 1
            || loaded.project.flows[0].nodes.size() != 3)
            throw std::runtime_error("GPU example is not resolved by the installed tool package");
        auto &nodes = loaded.project.flows[0].nodes;
        auto &reader = nodes[0];
        auto &detector = nodes[1];
        if (std::get<std::string>(detector.initParams.at(1)) != "build_wts")
            throw std::runtime_error("GPU example must build its native WTS model before loading the saved engine");
        QTemporaryDir temporary;
        if (!temporary.isValid())
            throw std::runtime_error("Cannot create the GPU project directory");
        loaded.project.path = std::filesystem::u8path(temporary.path().toUtf8().toStdString()) / "gpu.json";
        reader.inputs.at(1).literal             = std::string(image.generic_string());
        reader.inputs.at(5).literal            = std::int64_t{1}; // single-image detection reference
        detector.initParams.at(2)         = std::string(weights.generic_string());
        detector.initParams.at(3) = std::string(enginePath.generic_string());
        if (outputError)
        {
            const auto parentFile = fixtures / "engine-output-parent-file";
            {
                std::ofstream barrier(parentFile, std::ios::binary);
                barrier << "A regular file cannot contain an output engine.";
                if (!barrier)
                    throw std::runtime_error("Cannot create the engine output failure fixture");
            }
            const auto output = parentFile / "output.engine";
            detector.initParams.at(3) = output.generic_string();
            auto tool = registry.create(detector.toolId);
            const auto status = tool->initialize(detector.initParams);
            tool->release();
            auto message = status.message;
            std::replace(message.begin(), message.end(), '\\', '/');
            if (status || message.find("引擎输出") == std::string::npos
                || message.find(output.generic_string()) == std::string::npos || status.detail.empty())
                throw std::runtime_error("Engine write failure lost its role, target or cause: " + status.message);
            std::cout << "[PASS] " << status.message << ": " << status.detail << '\n';
            return 0;
        }
        struct Reference
        {
            cv::Rect2f rectangle;
            float score;
            std::int64_t category;
        };
        const std::array<Reference, 5> reference{{
            {{670.449F, 380.558F, 139.470F, 499.093F}, 0.890207F, 0},
            {{221.676F, 407.420F, 122.107F, 448.766F}, 0.883279F, 0},
            {{50.682F, 397.299F, 193.716F, 508.077F}, 0.878442F, 0},
            {{31.489F, 230.744F, 769.925F, 545.036F}, 0.843369F, 5},
            {{0.450F, 549.838F, 57.320F, 318.462F}, 0.436308F, 0}
        }};
        for (int pass = -1; pass < 2; ++pass)
        {
            if (pass == 0 && !buildWeights)
                continue;
            if (pass < 0)
            {
                detector.initParams.at(1) = std::string("engine");
                detector.initParams.at(2) = std::string{};
            }
            else if (pass == 0)
            {
                detector.initParams.at(1) = std::string("build_wts");
                detector.initParams.at(2) = std::string(weights.generic_string());
            }
            if (pass == 1)
            {
                if (!std::filesystem::is_regular_file(enginePath) || std::filesystem::file_size(enginePath) == 0)
                    throw std::runtime_error("Native model build did not save an engine");
                detector.initParams.at(1)  = std::string("engine");
                detector.initParams.at(2) = std::string(enginePath.generic_string());
            }
            const auto phase = pass < 0 ? "missing-model-path" : pass == 0 ? "build_wts" : "engine";
            std::cout << "[YOLO] " << phase << std::endl;
            RuntimeEngine runtime(2);
            const auto phaseStarted = std::chrono::steady_clock::now();
            const auto started = runtime.start(loaded.project, registry, RunMode::Once);
            if (!started)
                throw std::runtime_error(started.message);
            std::map<NodeId, ResultSlot::Snapshot> results;
            const auto deadline = phaseStarted + (pass == 0 ? 10min : 180s);
            bool completed = false;
            while (std::chrono::steady_clock::now() < deadline)
            {
                const auto update = runtime.pollLatest();
                for (const auto &node : update.nodes)
                    if (node.snapshot)
                        results[node.node] = node.snapshot;
                if (update.completion)
                {
                    if (!runtime.acknowledgeCompletion(*update.completion))
                        throw std::runtime_error("GPU completion acknowledgement failed");
                    testsupport::printExecutionEvidence(std::filesystem::path(arguments.at(2).toStdU16String()),
                                                        loaded.project, update);
                    completed = true;
                    break;
                }
                std::this_thread::sleep_for(2ms);
            }
            if (!completed)
                throw std::runtime_error(std::string("GPU ") + phase + " completion deadline exceeded");
            if (pass < 0)
            {
                const auto detected = results.at(detector.id);
                if (detected->phase != NodePhase::Initialize || detected->status.code == 0
                    || std::get<std::int64_t>(detected->outputs.at(kStatusCodePort)) != detected->status.code
                    || std::get<std::string>(detected->outputs.at(kStatusMessagePort)) != detected->status.message
                    || detected->timing.toolCallMeasured)
                    throw std::runtime_error("YOLO initialization failure did not publish its node status");
                std::cout << "[YOLO] Missing model path publishes initialization status without entering API\n";
                continue;
            }
            for (const auto &node : nodes)
                if (!results.contains(node.id) || !results.at(node.id)->status)
                    throw std::runtime_error("GPU node " + std::to_string(node.id) + ": "
                                             + (results.contains(node.id) ? results.at(node.id)->status.message
                                                                         : "missing result"));
            const auto &outputs = results.at(detector.id)->outputs;
            const auto &regions = std::get<visionrt::Rectangle>(outputs.at(3)).get();
            const auto &scores  = std::get<Array>(outputs.at(4));
            const auto &classes = std::get<Array>(outputs.at(5));
            const auto &names   = std::get<Array>(outputs.at(6));
            if (regions.size() != 1 || regions[0].size() != reference.size()
                || scores.type() != TypeSpec{Type::Array, DType::Float32, 2}
                || nestedOf<NestedVector<float, 2>>(scores).size() != 1
                || nestedOf<NestedVector<std::int64_t, 2>>(classes).size() != 1
                || nestedOf<NestedVector<std::string, 2>>(names).size() != 1)
                throw std::runtime_error("YOLO detection batch/shape differs from the independent PyTorch reference");
            const auto actualScores  = nestedOf<NestedVector<float, 2>>(scores)[0];
            const auto actualClasses = nestedOf<NestedVector<std::int64_t, 2>>(classes)[0];
            const auto actualNames   = nestedOf<NestedVector<std::string, 2>>(names)[0];
            if (actualScores.size() != reference.size() || actualClasses.size() != reference.size()
                || actualNames.size() != reference.size())
                throw std::runtime_error("YOLO fields do not share the same ragged detection count");
            std::array<bool, 5> matched{};
            for (std::size_t index = 0; index < regions[0].size(); ++index)
            {
                const auto encoded = regions[0][index];
                if (roiKind(regions[0][index]) != RoiKind::Rectangle || encoded.size() != 6)
                    throw std::runtime_error("YOLO did not produce rectangle regions");
                const cv::Rect2f rectangle(encoded[1], encoded[2], encoded[3], encoded[4]);
                bool accepted = false;
                for (std::size_t expected = 0; expected < reference.size(); ++expected)
                {
                    const auto intersection = (rectangle & reference[expected].rectangle).area();
                    const auto overlap = intersection / (rectangle.area() + reference[expected].rectangle.area() - intersection);
                    if (!matched[expected] && actualClasses[index] == reference[expected].category && overlap > 0.90F
                        && std::abs(actualScores[index] - reference[expected].score) < 0.03F)
                    {
                        matched[expected] = accepted = true;
                        break;
                    }
                }
                if (!accepted || actualNames[index] != std::to_string(actualClasses[index]))
                    throw std::runtime_error("YOLO result differs from independent PyTorch class/box/score reference");
            }
            const auto &filtered = results.at(nodes[2].id)->outputs;
            if (std::get<visionrt::Rectangle>(filtered.at(3)).get()[0].size() != 4)
                throw std::runtime_error("Downstream filter did not remove the low-confidence person");
            std::cout << "[PASS] Native YOLO matches five PyTorch detections; downstream filter retains four." << std::endl;
            std::cout << "[YOLO] " << phase << " completed in "
                      << std::chrono::duration<double>(std::chrono::steady_clock::now() - phaseStarted).count()
                      << " seconds." << std::endl;
        }
        return 0;
    }
    catch (const std::exception &error)
    {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
