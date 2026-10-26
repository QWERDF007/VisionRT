#include <pybind11/embed.h>

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
#include <array>
#include <chrono>
#include <fstream>
#include <iostream>
#include <thread>

using namespace visionrt;
using namespace std::chrono_literals;
namespace py = pybind11;

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
    ToolDefinition generated;
    generated.id          = "test.installed.generated";
    generated.displayName = "Python-owned padded image";
    generated.kind        = ToolKind::PythonFunction;
    generated.entry       = {"examples/ownership", "tool.py", "execute"};
    generated.outputs     = {
        {"images", "Images", {Type::Image}}
    };
    write(root / "tools/custom/generated.json", emitNormalizedToolJson(generated));
    auto counter         = generated;
    counter.id           = "test.installed.ownership_counter";
    counter.entry.symbol = "counter";
    counter.outputs      = {
        {"allocated", "Allocated", {Type::Int}},
        { "released",  "Released", {Type::Int}}
    };
    write(root / "tools/custom/ownership_counter.json", emitNormalizedToolJson(counter));
    write(root / "python/custom/examples/ownership/tool.py", R"(
import numpy as np
import weakref
allocated = 0
released = 0

def finalized():
    global released
    released += 1

def execute(inputs):
    global allocated
    storage = np.arange(8 * 16, dtype=np.uint8)
    allocated += 1
    weakref.finalize(storage, finalized)
    return {'images': [storage.reshape(8, 16)[:, 2:12]], '__status_code': 0, '__status_message': ''}

def counter(inputs):
    return {'allocated': allocated, 'released': released, '__status_code': 0, '__status_message': ''}
)");
    ToolDefinition h35Combo;
    h35Combo.id          = "test.installed.h35_combo";
    h35Combo.displayName = "H35 native helper and declared resource";
    h35Combo.kind        = ToolKind::PythonFunction;
    h35Combo.entry       = {"examples/h35_combo", "tool.py", "execute"};
    h35Combo.resources   = {std::filesystem::path("resources/h35_weights.bin")};
    h35Combo.outputs     = {
        {    "val",          "Value", {Type::Int}},
        {"res_len", "ResourceLength", {Type::Int}}
    };
    write(root / "tools/custom/h35_combo.json", emitNormalizedToolJson(h35Combo));
    const std::string_view h35Payload = "H35_RESOURCE_PAYLOAD_35_BYTES_OK!";
    const auto             comboDir   = root / "python/custom/examples/h35_combo";
    std::filesystem::create_directories(comboDir);
    write(comboDir / "resources/h35_weights.bin", h35Payload);
    write(comboDir / "tool.py", R"(
from pathlib import Path
from . import _queue
def execute(inputs):
    q = _queue.SimpleQueue()
    q.put(42)
    package_dir = Path(__file__).resolve().parent
    res_path = package_dir / "resources" / "h35_weights.bin"
    return {'val': q.get(), 'res_len': len(res_path.read_bytes()), '__status_code': 0, '__status_message': ''}
)");
    const auto augmented = readToolsPackage(root);
    require(augmented.tools.size() == published.tools.size() + 4 && !augmented.index.tools.contains(relay.id),
            "Custom discovery required an index rebuild");
    validateToolsPackage(root);

    for (int batch = 0; batch < 2; ++batch)
    {
        std::vector<unsigned char> bytes;
        require(cv::imencode(".png", cv::Mat(8, 10, CV_8UC1, cv::Scalar(batch == 0 ? 7 : 11)), bytes),
                "Cannot encode image fixture");
        write(root / "images" / (std::to_string(batch) + ".png"),
              {reinterpret_cast<const char *>(bytes.data()), bytes.size()});
    }

    PythonRuntime python;
    {
        py::gil_scoped_acquire gil;
        const auto             filename  = py::module_::import("_queue").attr("__file__").cast<std::string>();
        const auto             extension = std::filesystem::path(std::u8string(filename.begin(), filename.end()));
        std::filesystem::copy_file(extension, comboDir / extension.filename(),
                                   std::filesystem::copy_options::overwrite_existing);
    }
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
    const auto    before  = editor.addNode(flow, *registry.find("visionrt.detection.filter"), {0, 10});
    const auto    bridge  = editor.addNode(flow, *registry.find(relay.id), {0, 20});
    const auto    after   = editor.addNode(flow, *registry.find("visionrt.detection.filter"), {0, 30});
    const auto    crop    = editor.addNode(flow, *registry.find("visionrt.image.crop"), {0, 40});
    const auto    builtin = editor.addNode(flow, *registry.find("visiontools.python.image_sizes"), {10, 15});
    const auto    h35     = editor.addNode(flow, *registry.find(h35Combo.id), {0, 50});
    editor.setLiteral(flow, reader, "path", std::string("images"));
    editor.setLiteral(flow, reader, "count", std::int64_t(2));
    editor.setLiteral(flow, reader, "reload", true);
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
        const auto  h35Snap       = snapshot(update, h35);
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
        require(std::get<std::int64_t>(h35Snap->outputs.at("val")) == 42,
                "H35 native helper queue output mismatch");
        require(std::get<std::int64_t>(h35Snap->outputs.at("res_len")) == static_cast<std::int64_t>(h35Payload.size()),
                "H35 declared resource size mismatch");
    }
    auto probe = python.factory(*registry.find(counter.id))(PlatformToolServices{});
    require(probe->initialize({}).code == 0, "Ownership probe initialization failed");
    const auto released = [&]
    {
        const auto counts = probe->execute({});
        require(counts.status.code == 0, "Ownership probe execution failed");
        require(std::get<std::int64_t>(counts.outputs.at("allocated")) == 1, "Python allocation count differs");
        return std::get<std::int64_t>(counts.outputs.at("released"));
    };
    cv::Mat retainedCrop;
    {
        ProjectEditor ownership;
        const auto    ownershipFlow = ownership.project().flows.front().id;
        const auto    parent        = ownership.addNode(ownershipFlow, *registry.find(generated.id), {0, 0});
        const auto    child         = ownership.addNode(ownershipFlow, *registry.find("visionrt.image.crop"), {0, 2});
        ownership.bindFreeInput(ownershipFlow, {parent, "images"}, {child, "images"});
        ownership.setLiteral(ownershipFlow, child, "rect", DataRef<ROI>(ROI{{{2., 1., 2., 4., 3., 0.}}}));
        ownership.setMode(ownershipFlow, child, "rect", BindingMode::Literal);
        RuntimeEngine ownerRuntime(2);
        require(ownerRuntime.start(ownership.project(), registry, RunMode::Once).code == 0,
                "Python allocation pipeline start failed");
        {
            const auto  update      = finish(ownerRuntime);
            const auto  original    = snapshot(update, parent);
            const auto  cropped     = snapshot(update, child);
            const auto &sourceImage = std::get<DataRef<Image>>(original->outputs.at("images")).get().at(0);
            retainedCrop            = std::get<DataRef<Image>>(cropped->outputs.at("images")).get().at(0);
            require(sourceImage.rows == 8 && sourceImage.cols == 10 && sourceImage.step[0] == 16,
                    "Python padded image layout differs");
            require(retainedCrop.rows == 3 && retainedCrop.cols == 4 && retainedCrop.step[0] == 16
                        && retainedCrop.data == sourceImage.data + 33,
                    "C++ crop copied or lost Python image stride");
            require(released() == 0, "Python allocation released before parent deletion");
            std::cout << "[OWNERSHIP] allocated=1 released=0 source=" << static_cast<const void *>(sourceImage.data)
                      << " crop=" << static_cast<const void *>(retainedCrop.data) << " stride=" << retainedCrop.step[0]
                      << " offset=" << retainedCrop.data - sourceImage.data << '\n';
        }
        const std::array<NodeId, 1> removed{parent};
        require(ownership.removeNodes(ownershipFlow, removed), "Python parent deletion failed");
        require(ownerRuntime.requestSynchronize(ownership.project(), registry).code == 0,
                "Parent deletion synchronization failed");
        finish(ownerRuntime);
        require(released() == 0, "Deleting the Python parent released the live C++ crop");
        std::cout << "[OWNERSHIP] parent_deleted released=0\n";
        for (int row = 0; row < retainedCrop.rows; ++row)
            for (int column = 0; column < retainedCrop.cols; ++column)
                require(retainedCrop.at<unsigned char>(row, column) == 35 + row * 16 + column,
                        "Cropped Python pixels differ after parent deletion");
    }
    require(released() == 0, "Runtime destruction released the externally retained crop");
    std::cout << "[OWNERSHIP] runtime_destroyed released=0\n";
    retainedCrop.release();
    const auto releaseDeadline = std::chrono::steady_clock::now() + 5s;
    while (released() == 0 && std::chrono::steady_clock::now() < releaseDeadline) std::this_thread::sleep_for(1ms);
    require(released() == 1, "Last crop release did not finalize the Python allocation exactly once");
    std::cout << "[OWNERSHIP] last_crop_released released=1\n";
    require(probe->release().code == 0, "Ownership probe release failed");
    std::cout << "Python allocation -> native cropped view -> parent deletion -> final owner release passed\n";
    std::cout << "Installed C++ -> Python -> C++ Image/Array/ROI and project roundtrip passed\n";
    return 0;
}
catch (const std::exception &error)
{
    std::cerr << error.what() << '\n';
    return 1;
}
