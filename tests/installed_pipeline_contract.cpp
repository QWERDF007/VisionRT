#include "core/DefinitionJson.h"
#include "core/ProjectEditor.h"
#include "core/ProjectStorage.h"
#include "core/ToolPackage.h"
#include "core/ToolRegistry.h"
#include "plugins/PluginLoader.h"
#include "python/PythonRuntime.h"
#include "runtime/RuntimeEngine.h"

#include <QCoreApplication>
#include <QTemporaryDir>
#include <opencv2/imgcodecs.hpp>
#include <algorithm>
#include <chrono>
#include <fstream>
#include <iostream>
#include <thread>

using namespace visionrt;
using namespace std::chrono_literals;

namespace {
void require(bool condition, const char *message)
{
    if (!condition)
        throw std::runtime_error(message);
}

void write(const std::filesystem::path &path, std::string_view bytes)
{
    std::filesystem::create_directories(path.parent_path());
    std::ofstream stream(path, std::ios::binary);
    stream.write(bytes.data(), static_cast<std::streamsize>(bytes.size()));
    require(stream.good(), "Cannot write installation fixture");
}

ResultSlot::Snapshot snapshot(const RuntimeUpdate &update, NodeId id)
{
    const auto found
        = std::find_if(update.nodes.begin(), update.nodes.end(), [id](const auto &entry) { return entry.node == id; });
    require(found != update.nodes.end() && found->snapshot, "Installed node publication missing");
    const auto result = found->snapshot;
    if (!result->status)
        throw std::runtime_error("Node " + std::to_string(id) + ": " + result->status.message + ": "
                                 + result->status.detail);
    return result;
}

RuntimeUpdate finish(RuntimeEngine &engine)
{
    RuntimeUpdate result;
    const auto    deadline = std::chrono::steady_clock::now() + 15s;
    while (std::chrono::steady_clock::now() < deadline)
    {
        auto update = engine.pollLatest();
        for (auto &entry : update.nodes)
        {
            const auto found = std::find_if(result.nodes.begin(), result.nodes.end(),
                                            [&](const auto &saved) { return saved.node == entry.node; });
            if (found == result.nodes.end())
                result.nodes.push_back(std::move(entry));
            else
                *found = std::move(entry);
        }
        if (update.completion)
        {
            require(engine.acknowledgeCompletion(*update.completion), "Completion acknowledgement rejected");
            return result;
        }
        std::this_thread::sleep_for(1ms);
    }
    throw std::runtime_error("Installed pipeline completion deadline exceeded");
}
}

int main(int argc, char **argv)
try
{
    QCoreApplication application(argc, argv);
    const auto       arguments = QCoreApplication::arguments();
    require(arguments.size() == 2, "Usage: installed_pipeline_contract <installed-tools-package>");
    const auto    source = std::filesystem::path(arguments[1].toStdU16String());
    QTemporaryDir temporary;
    require(temporary.isValid(), "Cannot create independent installation");
    const auto root = std::filesystem::path(temporary.path().toStdU16String()) / u8"安装 空格";
    std::filesystem::copy(source, root, std::filesystem::copy_options::recursive);
    const auto published = readToolsPackage(root);
    require(!published.index.tools.contains("test.installed.relay"), "Fixture unexpectedly indexed");

    ToolDefinition relay;
    relay.id          = "test.installed.relay";
    relay.displayName = "Installed writable relay";
    relay.kind        = ToolKind::PythonFunction;
    relay.entry       = {"examples/image_sizes", "tool.py", "execute"};
    relay.inputs      = {
        {     "images",  "Images",                    {Type::Image}},
        {       "rois", "Regions",                      {Type::ROI}},
        {     "scores",  "Scores", {Type::Array, DType::Float32, 2}},
        {    "classes", "Classes",   {Type::Array, DType::Int64, 2}},
        {"class_names",   "Names",  {Type::Array, DType::String, 2}}
    };
    relay.outputs = relay.inputs;
    relay.outputs.push_back({"rect", "Rectangle", {Type::ROI}});
    write(root / "tools/custom/relay.json", emitNormalizedToolJson(relay));
    write(root / "python/custom/examples/image_sizes/tool.py", R"(
def execute(inputs):
    images = inputs['images']
    assert isinstance(images, list) and len(images) == 2
    for image in images:
        assert image.flags.writeable and image.shape == (8, 10)
        image += 3
    for name in ('rois', 'scores', 'classes', 'class_names'):
        assert isinstance(inputs[name], list) and isinstance(inputs[name][0], list)
    assert isinstance(inputs['rois'][0][0][0], float)
    assert inputs['classes'][0][0] == 9007199254740993
    for regions in inputs['rois']:
        regions[0][1] += 1.0
    for scores in inputs['scores']:
        scores[0] += 0.125
    for classes in inputs['classes']:
        classes[0] += 1
    for names in inputs['class_names']:
        names[0] += '中文'
    return dict(inputs, rect=[inputs['rois'][0]], __status_code=0, __status_message='')
)");
    const auto augmented = readToolsPackage(root);
    require(augmented.tools.size() == published.tools.size() + 1 && !augmented.index.tools.contains(relay.id),
            "Custom discovery required an index rebuild");

    for (int batch = 0; batch < 2; ++batch)
    {
        std::vector<unsigned char> bytes;
        require(cv::imencode(".png", cv::Mat(8, 10, CV_8UC1, cv::Scalar(batch == 0 ? 7 : 11)), bytes),
                "Cannot encode image fixture");
        write(root / "images" / (std::to_string(batch) + ".png"),
              {reinterpret_cast<const char *>(bytes.data()), bytes.size()});
    }

    PythonRuntime python;
    ToolRegistry  registry;
    registry.setPluginLoader(std::make_shared<PluginLoader>());
    registry.registerPackage(augmented);
    registry.loadNativeImplementations();
    for (const auto &loaded : python.refresh(registry.definitions()))
    {
        require(loaded.status.code == 0, "Installed Python definition unavailable");
        registry.setPythonImplementation(loaded.toolId, python.factory(*registry.find(loaded.toolId)), loaded.status);
    }
    ProjectEditor editor;
    const auto    flow    = editor.project().flows.front().id;
    const auto    reader  = editor.addNode(flow, *registry.find("visionrt.image.read"), {0, 0});
    const auto    before  = editor.addNode(flow, *registry.find("visionrt.detection.filter"), {10, 0});
    const auto    bridge  = editor.addNode(flow, *registry.find(relay.id), {20, 0});
    const auto    after   = editor.addNode(flow, *registry.find("visionrt.detection.filter"), {30, 0});
    const auto    crop    = editor.addNode(flow, *registry.find("visionrt.image.crop"), {40, 0});
    const auto    builtin = editor.addNode(flow, *registry.find("visiontools.python.image_sizes"), {20, 10});
    editor.setLiteral(flow, reader, "mode", std::string("directory"));
    editor.setLiteral(flow, reader, "path", std::string("images"));
    editor.setLiteral(flow, reader, "color", std::string("gray"));
    editor.bindFreeInput(flow, {reader, "images"}, {before, "images"});
    editor.setLiteral(flow, before, "rois", DataRef<ROI>(ROI{{{2., 1., 2., 3., 4., 0.}}, {{2., 1., 2., 3., 4., 0.}}}));
    editor.setLiteral(flow, before, "scores", Array(NestedVector<float, 2>{{0.75F}, {0.5F}}));
    editor.setLiteral(flow, before, "classes", Array(NestedVector<std::int64_t, 2>{{9007199254740993LL}, {2}}));
    editor.setLiteral(flow, before, "class_names", Array(NestedVector<std::string, 2>{{"first"}, {"second"}}));
    for (const auto *port : {"rois", "scores", "classes", "class_names"})
        editor.setMode(flow, before, port, BindingMode::Literal);
    for (const auto &port : relay.inputs)
    {
        editor.bindFreeInput(flow, {before, port.id}, {bridge, port.id});
        editor.bindFreeInput(flow, {bridge, port.id}, {after, port.id});
    }
    editor.bindFreeInput(flow, {bridge, "images"}, {crop, "images"});
    editor.bindFreeInput(flow, {bridge, "rect"}, {crop, "rect"});
    editor.bindFreeInput(flow, {before, "images"}, {builtin, "images"});
    ProjectStorage storage(registry);
    auto           project = editor.project();
    project.path           = root / "pipeline.vrt.json";
    require(storage.save(project).empty(), "Installed project save diagnostics");
    auto reopened = storage.load(project.path);
    require(reopened.diagnostics.empty(), "Installed project reload diagnostics");
    RuntimeEngine runtime(3);
    for (const auto &candidate : {project, reopened.project})
    {
        require(runtime.start(candidate, registry, RunMode::Once).code == 0, "Installed pipeline start failed");
        const auto update = finish(runtime);
        for (const auto &node : candidate.flows.front().nodes) snapshot(update, node.id);
        const auto  original      = snapshot(update, reader);
        const auto  relayed       = snapshot(update, bridge);
        const auto  filtered      = snapshot(update, after);
        const auto  cropped       = snapshot(update, crop);
        const auto  sizes         = snapshot(update, builtin);
        const auto &sourceImages  = std::get<DataRef<Image>>(original->outputs.at("images")).get();
        const auto &bridgedImages = std::get<DataRef<Image>>(relayed->outputs.at("images")).get();
        const auto &cropImages    = std::get<DataRef<Image>>(cropped->outputs.at("images")).get();
        require(sourceImages.size() == 2 && bridgedImages.size() == 2 && cropImages.size() == 2,
                "Installed image batch changed");
        for (std::size_t batch = 0; batch < 2; ++batch)
        {
            require(sourceImages[batch].data == bridgedImages[batch].data, "Python passthrough copied image storage");
            require(sourceImages[batch].at<unsigned char>(0, 0) == (batch == 0 ? 10 : 14),
                    "Writable Python mutation did not reach native owner");
            require(cropImages[batch].rows == 4 && cropImages[batch].cols == 3
                        && cv::mean(cropImages[batch])[0] == (batch == 0 ? 10 : 14),
                    "Native consumer received different image or ROI");
        }
        require((std::get<Array>(filtered->outputs.at("scores")).get<NestedVector<float, 2>>()
                 == NestedVector<float, 2>{{0.875F}, {0.625F}}),
                "Float array roundtrip differs");
        require((std::get<Array>(filtered->outputs.at("classes")).get<NestedVector<std::int64_t, 2>>()
                 == NestedVector<std::int64_t, 2>{{9007199254740994LL}, {3}}),
                "Int64 array precision lost");
        require((std::get<Array>(filtered->outputs.at("class_names")).get<NestedVector<std::string, 2>>()
                 == NestedVector<std::string, 2>{{"first中文"}, {"second中文"}}),
                "String array roundtrip differs");
        require((std::get<DataRef<ROI>>(filtered->outputs.at("rois")).get()
                 == ROI{{{2., 2., 2., 3., 4., 0.}}, {{2., 2., 2., 3., 4., 0.}}}),
                "ROI roundtrip differs");
        require((std::get<Array>(sizes->outputs.at("sizes")).get<NestedVector<std::int64_t, 2>>()
                    == NestedVector<std::int64_t, 2>{{8, 10}, {8, 10}}), "Builtin/custom same-name packages collided");
    }
    std::cout << "Installed C++ -> Python -> C++ Image/Array/ROI and project roundtrip passed\n";
    return 0;
}
catch (const std::exception &error)
{
    std::cerr << error.what() << '\n';
    return 1;
}
