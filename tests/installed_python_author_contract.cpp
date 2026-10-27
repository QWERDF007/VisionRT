#include "core/ProjectEditor.h"
#include "result_view.h"
#include "core/ProjectStorage.h"
#include "core/ToolPackage.h"
#include "core/ToolRegistry.h"
#include "plugins/PluginLoader.h"
#include "python/PythonRuntime.h"
#include "runtime/RuntimeEngine.h"

#include <QCoreApplication>
#include <QTemporaryDir>
#include <opencv2/imgcodecs.hpp>
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

RuntimeUpdate finish(RuntimeEngine &engine)
{
    const auto deadline = std::chrono::steady_clock::now() + 15s;
    while (std::chrono::steady_clock::now() < deadline)
    {
        auto update = engine.pollLatest();
        if (update.completion)
        {
            assert(engine.acknowledgeCompletion(*update.completion));
            return update;
        }
        std::this_thread::sleep_for(2ms);
    }
    throw std::runtime_error("installed Python author completion deadline exceeded");
}

ResultSlot::Snapshot snapshot(const RuntimeUpdate &update, NodeId id)
{
    for (const auto &item : update.nodes)
        if (item.node == id)
            return item.snapshot;
    throw std::runtime_error("installed Python author node update absent");
}
}

int main(int argc, char **argv)
try
{
    QCoreApplication application(argc, argv);
    assert(argc == 2);
    const auto packageRoot = std::filesystem::absolute(std::filesystem::path(argv[1]));
    const auto package     = readToolsPackage(packageRoot);
    ToolRegistry registry;
    registry.setPluginLoader(std::make_shared<PluginLoader>());
    registry.registerPackage(package);
    registry.loadNativeImplementations();

    PythonRuntime python;
    for (const auto &loaded : python.refresh(registry.definitions()))
    {
        assert(testview::status(loaded).code == 0);
        registry.setPythonImplementation(loaded.toolId, python.factory(*registry.find(loaded.toolId)), loaded.status);
    }
    const auto *read = registry.find("visionrt.image.read");
    const auto *roi  = registry.find("custom.python.roi");
    const auto *add  = registry.find("custom.python.image_add");
    assert(read && roi && add);

    QTemporaryDir temporary;
    assert(temporary.isValid());
    const auto imagePath = std::filesystem::path(temporary.path().toStdWString()) / "author.png";
    {
        const cv::Mat           image(4, 5, CV_8UC1, cv::Scalar(10));
        std::vector<uchar> bytes;
        assert(cv::imencode(".png", image, bytes));
        std::ofstream output(imagePath, std::ios::binary);
        output.write(reinterpret_cast<const char *>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
        assert(output.good());
    }

    ProjectEditor editor;
    const auto    flow = editor.project().flows.front().id;
    const auto    reader = editor.addNode(flow, *read, {0, 0});
    editor.setLiteral(flow, reader, "path", std::string(imagePath.string()));
    editor.setLiteral(flow, reader, "reload", true);
    const auto roiNode = editor.addNode(flow, *roi, {0, 10});
    editor.bindFreeInput(flow, {reader, "images"}, {roiNode, "images"});
    const auto addNode = editor.addNode(flow, *add, {0, 20});
    editor.bindFreeInput(flow, {reader, "images"}, {addNode, "images"});
    editor.setLiteral(flow, addNode, "mode", std::string("inplace"));

    auto project  = editor.project();
    project.path  = std::filesystem::path(temporary.path().toStdWString()) / "author.vrt.json";
    ProjectStorage storage(registry);
    assert(storage.save(project).empty());
    const auto reopened = storage.load(project.path);
    assert(reopened.diagnostics.empty());

    RuntimeEngine engine(2);
    for (const auto &candidate : {project, reopened.project})
    {
        assert(engine.start(candidate, registry, RunMode::Once).code == 0);
        const auto update = finish(engine);

        const auto readResult = snapshot(update, reader);
        const auto roiResult  = snapshot(update, roiNode);
        const auto addResult  = snapshot(update, addNode);
        assert(readResult && roiResult && addResult);
        assert(readResult->status.code == 0 && roiResult->status.code == 0 && addResult->status.code == 0);

        const auto &rois = std::get<DataRef<ROI>>(roiResult->outputs.at("rois")).get();
        assert(rois.size() == 1 && rois.front().size() == 1);
        assert((rois.front().front() == std::vector<double>{2, 0, 0, 5, 4, 0}));
        const auto &added = std::get<DataRef<Image>>(addResult->outputs.at("images")).get();
        assert(added.size() == 1 && added.front().at<std::uint8_t>(0, 0) == 11);
        const auto &readImages = std::get<DataRef<Image>>(readResult->outputs.at("images")).get();
        assert(readImages.size() == 1 && readImages.front().at<std::uint8_t>(0, 0) == 11);
    }

    std::cout << "Installed Python ROI and image-edit author tools executed through RuntimeEngine\n";
    return 0;
}
catch (const std::exception &error)
{
    std::cerr << error.what() << '\n';
    return 1;
}
