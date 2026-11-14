#include <pybind11/embed.h>
#include "ToolResultAssertions.h"

#include "core/DefinitionJson.h"
#include "core/ProjectEditor.h"
#include "core/ProjectStorage.h"
#include "core/ToolPackage.h"
#include "core/ToolRegistry.h"
#include "plugins/PluginLoader.h"
#include "python/PythonRuntime.h"
#include "TestPythonEnvironment.h"
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
    relay.nameEn           = "test.installed.relay";
    relay.nameZh           = "Installed writable relay";
    relay.kind             = ToolKind::PythonFunction;
    relay.entry            = {"examples/image_sizes", "tool.py", "execute"};
    relay.inputs           = {
        PortDefinition{.id = 1, .nameZh = "Images", .type = {Type::Image}, .nameEn = "images"},
        PortDefinition{.id = 2, .nameZh = "Regions", .type = {Type::ROI}, .nameEn = "rois"},
        PortDefinition{.id = 3, .nameZh = "Scores", .type = {Type::Array, DType::Float32, 2}, .nameEn = "scores"},
        PortDefinition{.id = 4, .nameZh = "Classes", .type = {Type::Array, DType::Int64, 2}, .nameEn = "classes"},
        PortDefinition{.id = 5, .nameZh = "Names", .type = {Type::Array, DType::String, 2}, .nameEn = "class_names"}
    };
    relay.outputs = {
        PortDefinition{.id = 2, .nameZh = "Images", .type = {Type::Image}, .nameEn = "images"},
        PortDefinition{.id = 3, .nameZh = "Regions", .type = {Type::ROI}, .nameEn = "rois"},
        PortDefinition{.id = 4, .nameZh = "Scores", .type = {Type::Array, DType::Float32, 2}, .nameEn = "scores"},
        PortDefinition{.id = 5, .nameZh = "Classes", .type = {Type::Array, DType::Int64, 2}, .nameEn = "classes"},
        PortDefinition{.id = 6, .nameZh = "Names", .type = {Type::Array, DType::String, 2}, .nameEn = "class_names"},
        PortDefinition{.id = 7, .nameZh = "Rectangle", .type = {Type::ROI}, .nameEn = "rect"}
    };
    write(root / "tools/custom/relay.json", emitNormalizedToolJson(relay));
    write(root / "python/custom/examples/image_sizes/tool.py", R"(
import numpy as np

def execute(inputs):
    images = inputs[1]
    assert isinstance(images, list) and len(images) == 2
    for image in images:
        assert image.flags.writeable and image.shape == (8, 10)
        image += 3
    # Rectangular numeric arrays arrive as writable NumPy views; text arrays keep the nested list
    # form, and a ROI is always the three-level list plus its own geometry encoding.
    assert isinstance(inputs[3], np.ndarray) and inputs[3].flags.writeable
    assert isinstance(inputs[4], np.ndarray) and inputs[4].dtype == np.int64
    assert isinstance(inputs[5], list) and isinstance(inputs[5][0], list)
    assert isinstance(inputs[2], list) and isinstance(inputs[2][0][0][0], float)
    assert int(inputs[4][0][0]) == 9007199254740993
    for regions in inputs[2]:
        regions[0][1] += 1.0
    inputs[3] += 0.125
    inputs[4] += 1
    for names in inputs[5]:
        names[0] += '中文'
    out = {2: inputs[1], 3: inputs[2], 4: inputs[3], 5: inputs[4], 6: inputs[5], 7: [inputs[2][0]], 0: 0, 1: ''}
    return {'outputs': out, 'detail': ''}
)");
    ToolDefinition generated;
    generated.nameEn           = "test.installed.generated";
    generated.nameZh           = "Python-owned padded image";
    generated.kind             = ToolKind::PythonFunction;
    generated.entry            = {"examples/ownership", "tool.py", "execute"};
    generated.outputs          = {
        PortDefinition{.id = 2, .nameZh = "Images", .type = {Type::Image}, .nameEn = "images"}
    };
    write(root / "tools/custom/generated.json", emitNormalizedToolJson(generated));
    auto counter               = generated;
    counter.nameEn             = "test.installed.ownership_counter";
    counter.entry.symbol       = "counter";
    counter.outputs            = {
        PortDefinition{.id = 2, .nameZh = "Allocated", .type = {Type::Int}, .nameEn = "allocated"},
        PortDefinition{.id = 3, .nameZh = "Released", .type = {Type::Int}, .nameEn = "released"}
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
    return {'outputs': {2: [storage.reshape(8, 16)[:, 2:12]], 0: 0, 1: ''}, 'detail': ''}

def counter(inputs):
    return {'outputs': {2: allocated, 3: released, 0: 0, 1: ''}, 'detail': ''}
)");
    ToolDefinition h35Combo;
    h35Combo.nameEn           = "test.installed.h35_combo";
    h35Combo.nameZh           = "H35 native helper and declared resource";
    h35Combo.kind             = ToolKind::PythonFunction;
    h35Combo.entry            = {"examples/h35_combo", "tool.py", "execute"};
    h35Combo.resources        = {std::filesystem::path("resources/h35_weights.bin")};
    h35Combo.outputs          = {
        PortDefinition{.id = 2, .nameZh = "Value", .type = {Type::Int}, .nameEn = "val"},
        PortDefinition{.id = 3, .nameZh = "ResourceLength", .type = {Type::Int}, .nameEn = "res_len"}
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
    return {'outputs': {2: q.get(), 3: len(res_path.read_bytes()), 0: 0, 1: ''}, 'detail': ''}
)");
    const auto augmented = readToolsPackage(root);
    require(augmented.tools.size() == published.tools.size() + 4 && !augmented.index.tools.contains(relay.nameEn),
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

    PythonRuntime python(testsupport::pythonEnvironment());
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
        require(testview::status(loaded).code == 0, "Installed Python definition unavailable");
        registry.setPythonImplementation(loaded.toolId, python.factory(*registry.find(loaded.toolId)), loaded.status);
    }
    ProjectEditor editor;
    const auto    flow    = editor.project().flows.front().id;
    const auto    reader  = editor.addNode(flow, *registry.find("ReadImage"), {0, 0});
    // The brightness tool is registered through registerFunction: executing it here proves the
    // function entry reaches RuntimeEngine, not only a direct unit-test call.
    const auto *brightnessDef = registry.find("AdjustBrightness");
    std::cerr << "[DEBUG] brightness definition " << (brightnessDef ? "found" : "missing") << std::endl;
    const auto    brightness = editor.addNode(flow, *brightnessDef, {0, 4});
    const auto    before  = editor.addNode(flow, *registry.find("FilterDetections"), {0, 10});
    const auto    bridge  = editor.addNode(flow, *registry.find(relay.nameEn), {0, 20});
    const auto    after   = editor.addNode(flow, *registry.find("FilterDetections"), {0, 30});
    const auto    crop    = editor.addNode(flow, *registry.find("CropImage"), {0, 40});
    const auto    builtin = editor.addNode(flow, *registry.find("ImageSizes"), {10, 15});
    const auto    h35     = editor.addNode(flow, *registry.find(h35Combo.nameEn), {0, 50});
    editor.setLiteral(flow, reader, 1, std::string("images"));
    editor.setLiteral(flow, reader, 5, std::int64_t(2));
    editor.setLiteral(flow, reader, 6, true);
    editor.setLiteral(flow, brightness, 2, 1.0);
    editor.setLiteral(flow, brightness, 3, 1.0);
    editor.setLiteral(flow, brightness, 4, DataRef<ROI>(ROI{}));
    editor.setLiteral(flow, brightness, 6, Array::fromNested(NestedVector<double, 3>{}));
    editor.bindFreeInput(flow, {reader, 2}, {before, 1});
    // A second consumer keeps the primary reader -> relay -> crop storage chain intact while still
    // executing the function-registered tool through the real runtime.
    editor.bindFreeInput(flow, {reader, 2}, {brightness, 1});
    editor.setLiteral(flow, before, 2, DataRef<ROI>(ROI{{{2., 1., 2., 3., 4., 0.}}, {{2., 1., 2., 3., 4., 0.}}}));
    editor.setLiteral(flow, before, 3, Array::fromNested(NestedVector<float, 2>{{0.75F}, {0.5F}}));
    editor.setLiteral(flow, before, 4, Array::fromNested(NestedVector<std::int64_t, 2>{{9007199254740993LL}, {2}}));
    editor.setLiteral(flow, before, 5, Array::fromNested(NestedVector<std::string, 2>{{"first"}, {"second"}}));
    for (const PortId port : {2, 3, 4, 5})
        editor.setMode(flow, before, port, BindingMode::Literal);
    for (PortId port = 1; port <= 5; ++port)
    {
        editor.bindFreeInput(flow, {before, static_cast<PortId>(port + 1)}, {bridge, port});
        editor.bindFreeInput(flow, {bridge, static_cast<PortId>(port + 1)}, {after, port});
    }
    editor.bindFreeInput(flow, {bridge, 2}, {crop, 1});
    editor.bindFreeInput(flow, {bridge, 7}, {crop, 2});
    editor.bindFreeInput(flow, {before, 2}, {builtin, 1});
    ProjectStorage storage(registry);
    auto           project = editor.project();
    project.path           = root / "pipeline.vrt.json";
    storage.save(project);
    auto reopened = storage.load(project.path);
    require(reopened.diagnostics.empty(), "Installed project reload diagnostics");
    RuntimeEngine runtime(3);
    for (const auto &candidate : {project, reopened.project})
    {
        require(runtime.start(candidate, registry, RunMode::Once).code == 0, "Installed pipeline start failed");
        const auto update = finish(runtime);
        for (const auto &node : candidate.flows.front().nodes) snapshot(update, node.id);
        const auto brightnessSnapshot = snapshot(update, brightness);
        require(brightnessSnapshot->status.code == 0,
                "the function-registered brightness tool did not succeed in the installed package");
        const auto  original      = snapshot(update, reader);
        const auto  relayed       = snapshot(update, bridge);
        const auto  filtered      = snapshot(update, after);
        const auto  cropped       = snapshot(update, crop);
        const auto  sizes         = snapshot(update, builtin);
        const auto  h35Snap       = snapshot(update, h35);
        const auto &sourceImages  = std::get<DataRef<Image>>(original->outputs.at(2)).get();
        const auto &bridgedImages = std::get<DataRef<Image>>(relayed->outputs.at(2)).get();
        const auto &cropImages    = std::get<DataRef<Image>>(cropped->outputs.at(2)).get();
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
        require((nestedOf<NestedVector<float, 2>>(std::get<Array>(filtered->outputs.at(4)))
                 == NestedVector<float, 2>{{0.875F}, {0.625F}}),
                "Float array roundtrip differs");
        require((nestedOf<NestedVector<std::int64_t, 2>>(std::get<Array>(filtered->outputs.at(5)))
                 == NestedVector<std::int64_t, 2>{{9007199254740994LL}, {3}}),
                "Int64 array precision lost");
        require((nestedOf<NestedVector<std::string, 2>>(std::get<Array>(filtered->outputs.at(6)))
                 == NestedVector<std::string, 2>{{"first中文"}, {"second中文"}}),
                "String array roundtrip differs");
        require((std::get<DataRef<ROI>>(filtered->outputs.at(3)).get()
                 == ROI{{{2., 2., 2., 3., 4., 0.}}, {{2., 2., 2., 3., 4., 0.}}}),
                "ROI roundtrip differs");
        require((nestedOf<NestedVector<std::int64_t, 2>>(std::get<Array>(sizes->outputs.at(2)))
                    == NestedVector<std::int64_t, 2>{{8, 10}, {8, 10}}), "Builtin/custom same-name packages collided");
        require(std::get<std::int64_t>(h35Snap->outputs.at(2)) == 42,
                "H35 native helper queue output mismatch");
        require(std::get<std::int64_t>(h35Snap->outputs.at(3)) == static_cast<std::int64_t>(h35Payload.size()),
                "H35 declared resource size mismatch");
    }
    auto probe = python.factory(*registry.find(counter.nameEn))();
    require(probe->initialize({}).code == 0, "Ownership probe initialization failed");
    const auto released = [&]
    {
        const auto counts = probe->execute({});
        require(testview::status(counts).code == 0, "Ownership probe execution failed");
        require(std::get<std::int64_t>(counts.outputs.at(2)) == 1, "Python allocation count differs");
        return std::get<std::int64_t>(counts.outputs.at(3));
    };
    cv::Mat retainedCrop;
    {
        ProjectEditor ownership;
        const auto    ownershipFlow = ownership.project().flows.front().id;
        const auto    parent        = ownership.addNode(ownershipFlow, *registry.find(generated.nameEn), {0, 0});
        const auto    child         = ownership.addNode(ownershipFlow, *registry.find("CropImage"), {0, 2});
        ownership.bindFreeInput(ownershipFlow, {parent, 2}, {child, 1});
        ownership.setLiteral(ownershipFlow, child, 2, DataRef<ROI>(ROI{{{2., 1., 2., 4., 3., 0.}}}));
        ownership.setMode(ownershipFlow, child, 2, BindingMode::Literal);
        RuntimeEngine ownerRuntime(2);
        require(ownerRuntime.start(ownership.project(), registry, RunMode::Once).code == 0,
                "Python allocation pipeline start failed");
        {
            const auto  update      = finish(ownerRuntime);
            const auto  original    = snapshot(update, parent);
            const auto  cropped     = snapshot(update, child);
            const auto &sourceImage = std::get<DataRef<Image>>(original->outputs.at(2)).get().at(0);
            retainedCrop            = std::get<DataRef<Image>>(cropped->outputs.at(2)).get().at(0);
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
