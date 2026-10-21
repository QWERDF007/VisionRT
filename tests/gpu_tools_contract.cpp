#include "core/ProjectStorage.h"
#include "plugins/PluginLoader.h"
#include "runtime/RuntimeEngine.h"
#include <visionrt/sdk/Package.h>

#include <QCoreApplication>
#include <QTemporaryDir>
#include <array>
#include <chrono>
#include <cmath>
#include <filesystem>
#include <iostream>
#include <map>
#include <stdexcept>
#include <string_view>
#include <thread>

using namespace visionrt;
using namespace std::chrono_literals;

int main(int argc, char **argv)
{
    QCoreApplication application(argc, argv);
    try
    {
        if ((argc != 4 && argc != 5) || (argc == 5 && std::string_view(argv[4]) != "--engine-only"))
            throw std::runtime_error("Usage: gpu_tools_contract <tools-package> <example-project> <fixture-directory> [--engine-only]");
        const bool buildWeights = argc == 4;
        const auto fixtures = std::filesystem::absolute(std::filesystem::u8path(argv[3]));
        const auto weights = fixtures / "yolov8n.wts";
        const auto image = fixtures / "bus.jpg";
        const auto enginePath = fixtures / "yolov8n.engine";
        if (!std::filesystem::is_regular_file(image)
            || (buildWeights && !std::filesystem::is_regular_file(weights)))
            throw std::runtime_error("YOLO fixture requires Ultralytics bus.jpg and exported COCO yolov8n.wts for model build");
        ToolRegistry registry;
        registry.setPluginLoader(std::make_shared<PluginLoader>());
        registry.registerPackage(readToolsPackage(std::filesystem::u8path(argv[1])));
        ProjectStorage storage(registry);
        auto loaded = storage.load(std::filesystem::u8path(argv[2]));
        for (const auto &diagnostic : loaded.diagnostics)
            std::cerr << "GPU project diagnostic: " << diagnostic.field << ": " << diagnostic.message << '\n';
        if (!loaded.diagnostics.empty() || loaded.project.flows.size() != 1
            || loaded.project.flows[0].nodes.size() != 3)
            throw std::runtime_error("GPU example is not resolved by the installed tool package");
        auto &nodes = loaded.project.flows[0].nodes;
        auto &reader = nodes[0];
        auto &detector = nodes[1];
        if (std::get<Utf8>(detector.initParams.at("load_mode")).str() != "build_wts")
            throw std::runtime_error("GPU example must build its native WTS model before loading the saved engine");
        QTemporaryDir temporary;
        if (!temporary.isValid())
            throw std::runtime_error("Cannot create the GPU project directory");
        loaded.project.path = std::filesystem::u8path(temporary.path().toUtf8().toStdString()) / "gpu.vrt";
        reader.inputs.at("mode").literal = Utf8("file");
        reader.inputs.at("path").literal = Utf8(image.generic_string());
        detector.initParams.at("model_path") = Utf8(weights.generic_string());
        detector.initParams.at("engine_output_path") = Utf8(enginePath.generic_string());
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
        for (int pass = buildWeights ? 0 : 1; pass < 2; ++pass)
        {
            if (pass == 1)
            {
                if (!std::filesystem::is_regular_file(enginePath) || std::filesystem::file_size(enginePath) == 0)
                    throw std::runtime_error("Native model build did not save an engine");
                detector.initParams.at("load_mode") = Utf8("engine");
                detector.initParams.at("model_path") = Utf8(enginePath.generic_string());
            }
            const auto phase = pass == 0 ? "build_wts" : "engine";
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
                    completed = true;
                    break;
                }
                std::this_thread::sleep_for(2ms);
            }
            if (!completed)
                throw std::runtime_error(std::string("GPU ") + phase + " completion deadline exceeded");
            for (const auto &node : nodes)
                if (!results.contains(node.id) || !results.at(node.id)->status)
                    throw std::runtime_error("GPU node " + std::to_string(node.id) + ": "
                                             + (results.contains(node.id) ? results.at(node.id)->status.message
                                                                         : "missing result"));
            const auto &outputs = results.at(detector.id)->outputs;
            const auto &regions = std::get<RoiBatch>(outputs.at("rois")).images();
            const auto &scores = std::get<NumericArray>(outputs.at("scores"));
            const auto &classes = std::get<NumericArray>(outputs.at("classes"));
            const auto &names = std::get<StringArray>(outputs.at("class_names"));
            if (regions.size() != 1 || regions[0].size() != reference.size()
                || scores.rank() != 2 || scores.layout() != Layout::BatchRagged
                || scores.buffers().size() != 1 || classes.buffers().size() != 1 || names.buffers().size() != 1)
                throw std::runtime_error("YOLO detection batch/shape differs from the independent PyTorch reference");
            const auto actualScores = scores.buffers()[0].values<float>();
            const auto actualClasses = classes.buffers()[0].values<std::int64_t>();
            const auto actualNames = names.buffers()[0].values();
            if (actualScores.size() != reference.size() || actualClasses.size() != reference.size()
                || actualNames.size() != reference.size())
                throw std::runtime_error("YOLO fields do not share the same ragged detection count");
            std::array<bool, 5> matched{};
            for (std::size_t index = 0; index < regions[0].size(); ++index)
            {
                const auto encoded = regions[0][index].encoded();
                if (regions[0][index].kind() != RoiKind::Rectangle || encoded.size() != 6)
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
            if (std::get<RoiBatch>(filtered.at("rois")).images()[0].size() != 4)
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
