#include "core/ToolRegistry.h"
#include <visionrt/contracts/PythonInterop.h>
#include "python/PythonRuntime.h"

#include <pybind11/embed.h>
#include <pybind11/numpy.h>
#include <visionrt/contracts/Package.h>

#include <array>
#include <cassert>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <future>
#include <iostream>

namespace py = pybind11;
using namespace visionrt;

namespace {
template<class F>
void rejects(F &&operation)
{
    bool rejected = false;
    try
    {
        operation();
    }
    catch (const std::exception &)
    {
        rejected = true;
    }
    assert(rejected);
}

void readonlyOwners()
{
    py::gil_scoped_acquire    gil;
    py::dict                  scope;
    std::weak_ptr<const void> retained;
    {
        const auto image = OwnedImage::copyOf(cv::Mat(4, 5, CV_16UC1, cv::Scalar(123)));
        const auto crop  = image.region({1, 1, 2, 2});
        retained         = crop.owner();
        scope["image"]   = toPythonReadonly(ImageBatch({crop})).cast<py::list>()[0];
        assert(py::cast<py::array>(scope["image"]).data() == crop.data());
        assert(py::cast<py::array>(scope["image"]).strides(0) == 10);
        py::exec(R"(
assert not image.flags.writeable
assert not image.flags.c_contiguous
try:
    image[0, 0] = 255
    raise AssertionError('input mutation succeeded')
except ValueError:
    pass
try:
    image.setflags(write=True)
    raise AssertionError('readonly capsule was made writeable')
except ValueError:
    pass
)",
                 scope);
        assert(crop.readOnlyMat().at<std::uint16_t>(0, 0) == 123);
        const auto returned = fromPython({Type::ImageBatch}, py::list(py::make_tuple(scope["image"])));
        assert(std::get<ImageBatch>(returned).images()[0].data() != crop.data());
        assert(std::get<ImageBatch>(returned).images()[0].readOnlyMat().at<std::uint16_t>(1, 1) == 123);
    }
    assert(!retained.expired());
    py::exec("assert int(image[1, 1]) == 123", scope);
    scope.clear();
    assert(retained.expired());

    const std::array<float, 3> data{0.8F, 0.9F, 0.7F};
    const auto                 buffer = NumericBuffer::copy(DType::Float32, {3}, std::as_bytes(std::span(data)));
    auto                       values = toPythonReadonly(NumericArray(DType::Float32, 1, Layout::Dense, {buffer}));
    assert(values.cast<py::array>().data() == buffer.data());
    const auto copied = fromPython({Type::NumericArray, DType::Float32, 1}, values);
    assert(std::get<NumericArray>(copied).buffers()[0].data() != buffer.data());
    assert(std::get<NumericArray>(copied).buffers()[0].values<float>()[1] == .9F);
    scope["values"] = values;
    scope["roi"]    = toPythonReadonly(RoiBatch({{Roi::circle(2, 3, 4)}})).cast<py::list>()[0].cast<py::list>()[0];
    py::exec(R"(
for value in (values, roi):
    assert not value.flags.writeable
    try:
        value[0] = 0
        raise AssertionError('input mutation succeeded')
    except ValueError:
        pass
)",
             scope);
}

void strictConversions()
{
    PortValue copiedImage;
    PortValue copiedScores;
    PortValue copiedRois;
    PortValue copiedNames;
    {
        py::gil_scoped_acquire gil;
        py::dict               scope;
        py::exec(R"(
import numpy as np
image = np.arange(24, dtype=np.uint16).reshape(4, 6)[::-1, ::2]
scores = [np.empty((0,), dtype=np.float32), np.array([.8, .9, .7], dtype=np.dtype('>f4').newbyteorder('='))]
classes = [np.empty((0,), dtype=np.int64), np.array([1, 2, 1], dtype=np.int64)]
names = [np.empty((0,), dtype='U'), np.array(['猫', 'dog', '猫'], dtype='U')]
rois = [[], [np.array([3, 1, 2, 3], dtype=np.float32),
             np.array([2, 0, 0, 4, 5, 0], dtype=np.float32),
             np.array([1, 0, 0, 2, 2], dtype=np.float32)]]
)",
                 scope);
        const TypeSpec scoresType{Type::NumericArray, DType::Float32, 2, Layout::BatchRagged};
        copiedImage        = fromPython({Type::ImageBatch}, py::list(py::make_tuple(scope["image"])));
        copiedScores       = fromPython(scoresType, scope["scores"]);
        copiedNames        = fromPython({Type::StringArray, {}, 2, Layout::BatchRagged}, scope["names"]);
        copiedRois         = fromPython({Type::RoiBatch}, scope["rois"]);
        const auto classes = fromPython({Type::NumericArray, DType::Int64, 2, Layout::BatchRagged}, scope["classes"]);
        assert(std::get<NumericArray>(classes).buffers()[1].values<std::int64_t>()[1] == 2);
        scope["roundtrip"] = toPythonReadonly(copiedNames);
        py::exec("assert roundtrip[1].dtype.kind == 'U'\nassert roundtrip[1].tolist() == ['猫', 'dog', '猫']", scope);
        rejects([&] { fromPython(scoresType, py::eval("[np.ones(3, dtype=np.float64)]", scope)); });
        rejects([&] { fromPython(scoresType, py::eval("[np.arange(6, dtype=np.float32)[::2]]", scope)); });
        rejects([&] { fromPython(scoresType, py::eval("np.ones((1, 3), dtype=np.float32)", scope)); });
        rejects([&] { fromPython(scoresType, py::eval("[np.ones((1, 3), dtype=np.float32)]", scope)); });
        rejects(
            [&]
            {
                fromPython(
                    scoresType,
                    py::eval(
                        "[np.lib.stride_tricks.as_strided(np.zeros(1, dtype=np.float32), shape=(4,), strides=(4,))]",
                        scope));
            });
        rejects(
            [&]
            {
                fromPython(
                    {Type::ImageBatch},
                    py::eval(
                        "[np.lib.stride_tricks.as_strided(np.zeros(1, dtype=np.uint8), shape=(3,3), strides=(3,1))]",
                        scope));
            });
        rejects([&] { fromPython({Type::StringArray, {}, 1}, py::eval("np.array(['x'], dtype=object)", scope)); });
        rejects([&] { fromPython({Type::StringArray, {}, 1}, py::eval("np.array([b'x'], dtype='S')", scope)); });
        rejects([&] { fromPython({Type::RoiBatch}, py::eval("[[np.array([2, 1], dtype=np.float32)]]", scope)); });
        rejects([&] { fromPython({Type::Int}, py::bool_(true)); });
        rejects([&] { fromPython({Type::Int}, py::eval("2 ** 63")); });
        rejects([&] { fromPython({Type::Float}, py::int_(1)); });
        rejects([&] { fromPython({Type::Bool}, py::int_(1)); });
        rejects([&] { fromPython({Type::String}, py::bytes("x")); });
        assert(std::get<std::int64_t>(fromPython({Type::Int}, py::eval("-(2 ** 63)"))) == INT64_MIN);
        assert(std::get<double>(fromPython({Type::Float}, py::float_(1.5))) == 1.5);
        assert(std::get<bool>(fromPython({Type::Bool}, py::bool_(true))));
        assert(std::get<Utf8>(fromPython({Type::String}, py::str("猫"))).str() == "猫");
        assert(isMissing(fromPython(scoresType, py::none())));
        py::exec("image[:] = 99\nscores[1][:] = 0\nrois[1][0][1] = 99", scope);
    }
    assert(std::get<ImageBatch>(copiedImage).images()[0].readOnlyMat().at<std::uint16_t>(0, 2) == 22);
    assert(std::get<ImageBatch>(copiedImage).images()[0].readOnlyMat().at<std::uint16_t>(3, 1) == 2);
    assert(std::get<NumericArray>(copiedScores).buffers()[0].shape()[0] == 0);
    assert(std::get<NumericArray>(copiedScores).buffers()[1].values<float>()[1] == .9F);
    assert(std::get<RoiBatch>(copiedRois).images()[0].empty());
    assert(std::get<RoiBatch>(copiedRois).images()[1][0].encoded()[1] == 1);
    assert(std::get<StringArray>(copiedNames).buffers()[1].values()[0] == "猫");
}

struct Sources
{
    std::filesystem::path directory
        = std::filesystem::temp_directory_path()
        / ("visionrt_python_" + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));

    Sources()
    {
        std::filesystem::create_directory(directory);
    }

    ~Sources()
    {
        std::error_code error;
        std::filesystem::remove_all(directory, error);
    }

    void put(const std::string &name, const std::string &source)
    {
        std::ofstream stream(directory / name, std::ios::binary | std::ios::trunc);
        stream.exceptions(std::ios::badbit | std::ios::failbit);
        stream << source;
    }

    ToolDefinition definition(const std::string &id, ToolKind kind, const std::string &symbol,
                              bool secondOutput = false)
    {
        const auto name = id + ".yaml";
        put(name, "schema_version: 1\ntool_id: " + id +
            "\ndisplay_name: Contract tool\nversion: v1\nkind: " +
            (kind == ToolKind::PythonClass ? "python_class" : "python_function") +
            "\nentry: {file: entry.py, symbol: " + symbol +
            "}\ninit_params: []\ninputs: []\noutputs:\n"
            "- {id: value, name: Value, type: Int}\n" +
            (secondOutput ? "- {id: other, name: Other, type: Float}\n" : ""));
        return parseDefinition(directory / name, false);
    }
};

void adaptersAndRefresh(PythonRuntime &runtime)
{
    Sources sources;
    sources.put("helper.py", "value = 11\nobsolete = True\n");
    sources.put("entry.py", R"(
from . import helper
import numpy
def execute(inputs):
    return {'__status_code': 0, '__status_message': '', 'value': helper.value}
class Counter:
    def initialize(self, inputs):
        self.count = 0
        return {'__status_code': 0, '__status_message': ''}
    def execute(self, inputs):
        self.count += 1
        return {'__status_code': 0, '__status_message': '', 'value': self.count}
    def release(self):
        return {'__status_code': 0, '__status_message': ''}
class Broken:
    def initialize(self, inputs):
        raise RuntimeError('initialization detail')
    def execute(self, inputs):
        raise RuntimeError('execution detail')
    def release(self):
        raise RuntimeError('release detail')
def missing(inputs):
    return {'__status_code': 0, '__status_message': ''}
def extra(inputs):
    return {'__status_code': 0, '__status_message': '', 'value': 1, 'typo': 2}
def wrong(inputs):
    return {'__status_code': 0, '__status_message': '', 'value': True}
def business(inputs):
    return {'__status_code': 5, '__status_message': 'expected failure', 'value': 7}
def partial(inputs):
    return {'__status_code': 0, '__status_message': '', 'value': 9, 'other': True}
)");
    auto function = sources.definition("python.function", ToolKind::PythonFunction, "execute");
    auto counter  = sources.definition("python.counter", ToolKind::PythonClass, "Counter");
    auto broken   = sources.definition("python.broken", ToolKind::PythonClass, "Broken");
    auto missing  = sources.definition("python.missing", ToolKind::PythonFunction, "missing");
    auto extra    = sources.definition("python.extra", ToolKind::PythonFunction, "extra");
    auto wrong    = sources.definition("python.wrong", ToolKind::PythonFunction, "wrong");
    auto business = sources.definition("python.business", ToolKind::PythonFunction, "business");
    auto partial  = sources.definition("python.partial", ToolKind::PythonFunction, "partial", true);
    std::vector<ToolDefinition> definitions{function, counter, broken, missing, extra, wrong, business, partial};
    std::uintptr_t              numpyIdentity;
    {
        py::gil_scoped_acquire gil;
        numpyIdentity = reinterpret_cast<std::uintptr_t>(py::module_::import("numpy").ptr());
        py::module_::import("py_compile")
            .attr("compile")((sources.directory / "helper.py").string(), py::arg("doraise") = true);
    }
    const auto loaded = runtime.refresh(definitions);
    for (const auto &record : loaded) assert(record.status.code == 0);
    const ToolServices services;
    auto               first  = runtime.factory(counter)(services);
    auto               second = runtime.factory(counter)(services);
    assert(first->initialize({}).code == 0 && second->initialize({}).code == 0);
    assert(std::get<std::int64_t>(first->execute({}).outputs.at("value")) == 1);
    assert(std::get<std::int64_t>(first->execute({}).outputs.at("value")) == 2);
    assert(std::get<std::int64_t>(second->execute({}).outputs.at("value")) == 1);
    rejects([&] { runtime.refresh(definitions); });
    assert(first->release().code == 0 && second->release().code == 0);
    first.reset();
    second.reset();

    auto       failures       = runtime.factory(broken)(services);
    const auto initialization = failures->initialize({});
    assert(initialization.code == -1 && initialization.message.find("initialization detail") != std::string::npos);
    assert(initialization.message.find("entry.py") != std::string::npos);
    const auto execution = failures->execute({});
    assert(execution.status.code == -1 && execution.status.message.find("execution detail") != std::string::npos);
    assert(execution.outputs.size() == 1 && isMissing(execution.outputs.at("value")));
    const auto cleanup = failures->release();
    assert(cleanup.code == -4 && cleanup.message.find("release detail") != std::string::npos);
    failures.reset();
    for (const auto &definition : {missing, extra, wrong})
    {
        auto       instance = runtime.factory(definition)(services);
        const auto result   = instance->execute({});
        assert(result.status.code == -2 && result.outputs.size() == 1);
        assert(isMissing(result.outputs.at("value")));
    }
    {
        auto       instance = runtime.factory(business)(services);
        const auto result   = instance->execute({});
        assert(result.status.code == 5 && std::get<std::int64_t>(result.outputs.at("value")) == 7);
    }
    {
        auto       instance = runtime.factory(partial)(services);
        const auto result   = instance->execute({});
        assert(result.status.code == -2 && result.outputs.size() == 2);
        assert(isMissing(result.outputs.at("value")) && isMissing(result.outputs.at("other")));
    }
    const auto oldFactory = runtime.factory(function);
    {
        auto instance = oldFactory(services);
        assert(std::get<std::int64_t>(instance->execute({}).outputs.at("value")) == 11);
    }
    const auto timestamp = std::filesystem::last_write_time(sources.directory / "helper.py");
    sources.put("helper.py", "value = 22\nobsolete = True\n");
    std::filesystem::last_write_time(sources.directory / "helper.py", timestamp);
    runtime.refresh(std::span<const ToolDefinition>(&function, 1));
    {
        auto instance = oldFactory(services);
        assert(std::get<std::int64_t>(instance->execute({}).outputs.at("value")) == 22);
    }
    sources.put("helper.py", "value = 33\n");
    sources.put("entry.py", R"(
from . import helper
def execute(inputs):
    assert not hasattr(helper, 'obsolete')
    return {'__status_code': 0, '__status_message': '', 'value': helper.value}
)");
    runtime.refresh(std::span<const ToolDefinition>(&function, 1));
    {
        auto instance = oldFactory(services);
        assert(std::get<std::int64_t>(instance->execute({}).outputs.at("value")) == 33);
        py::gil_scoped_acquire gil;
        assert(reinterpret_cast<std::uintptr_t>(py::module_::import("numpy").ptr()) == numpyIdentity);
    }
    sources.put("entry.py", "def execute(:\n");
    const auto invalid = runtime.refresh(std::span<const ToolDefinition>(&function, 1));
    assert(invalid[0].status.code == -3 && invalid[0].status.message.find("SyntaxError") != std::string::npos);
    {
        auto       instance = oldFactory(services);
        const auto result   = instance->execute({});
        assert(result.status.code == -3 && result.outputs.size() == 1 && isMissing(result.outputs.at("value")));
    }
    runtime.refresh({});
}

void exampleTools(PythonRuntime &runtime, const std::filesystem::path &root)
{
    ToolRegistry registry;
    for (const auto &tool : readToolsPackage(root).tools)
        if (tool.definition.kind == ToolKind::PythonClass || tool.definition.kind == ToolKind::PythonFunction)
            registry.addDefinition(tool.definition);
    assert(registry.find("custom.image.mean") && registry.find("custom.call.counter"));
    const auto &mean = *registry.find("custom.image.mean");
    const auto &counter = *registry.find("custom.call.counter");
    for (const auto &loaded : runtime.refresh(registry.definitions()))
    {
        assert(loaded.status.code == 0);
        registry.setPythonImplementation(loaded.toolId, runtime.factory(*registry.find(loaded.toolId)), loaded.status);
    }
    const ToolServices services;
    {
        auto       function = registry.create(mean.id, services);
        const auto image    = OwnedImage::copyOf(cv::Mat(2, 3, CV_8UC1, cv::Scalar(8)));
        const auto result   = function->execute({
            {"images", ImageBatch({image})}
        });
        assert(result.status.code == 0 && result.outputs.size() == 1);
        const auto &means = std::get<NumericArray>(result.outputs.at("means"));
        assert(means.buffers()[0].values<double>()[0] == 8.0);
        const auto missing = function->execute({
            {"images", Missing{}}
        });
        assert(missing.status.code == 1 && missing.outputs.size() == 1);
        assert(isMissing(missing.outputs.at("means")));
        const auto empty = function->execute({
            {"images", ImageBatch{}}
        });
        assert(empty.status.code == 0);
        assert(std::get<NumericArray>(empty.outputs.at("means")).buffers()[0].shape()[0] == 0);
        auto first  = registry.create(counter.id, services);
        auto second = registry.create(counter.id, services);
        assert(first->initialize({}).code == 0 && second->initialize({}).code == 0);
        assert(std::get<std::int64_t>(first->execute({}).outputs.at("count")) == 1);
        assert(std::get<std::int64_t>(first->execute({}).outputs.at("count")) == 2);
        const auto independent = second->execute({});
        assert(independent.status.code == 0 && independent.outputs.size() == 1);
        assert(std::get<std::int64_t>(independent.outputs.at("count")) == 1);
        assert(first->release().code == 0 && second->release().code == 0);
    }
    runtime.refresh({});
    auto       unavailable = runtime.factory(mean)(services);
    const auto result      = unavailable->execute({});
    assert(result.status.code == -3 && result.outputs.size() == 1);
    assert(isMissing(result.outputs.at("means")));
}

void deployedPurePythonPackage(PythonRuntime &runtime, const std::filesystem::path &root)
{
    const auto package = readToolsPackage(root);
    assert(package.index.modules.empty() && package.tools.size() == 1);
    ToolRegistry registry;
    registry.registerPackage(package);
    for (const auto &loaded : runtime.refresh(registry.definitions()))
    {
        assert(loaded.status);
        registry.setPythonImplementation(loaded.toolId, runtime.factory(*registry.find(loaded.toolId)), loaded.status);
    }
    auto instance = registry.create("test.add", {});
    assert(instance && instance->initialize({}));
    const auto result = instance->execute({});
    assert(result.status && std::get<std::int64_t>(result.outputs.at("value")) == 42);
    assert(instance->release());
    instance.reset();
    runtime.refresh({});
    std::cout << "Pure Python ZIP result: 42\n";
}

void packagePythonExecutionDemonstration(PythonRuntime &runtime)
{
    // 1. Setup isolated package root with unicode and spaces (Ticket 04)
    const auto tempBase = std::filesystem::temp_directory_path() / "visionrt_test_py_pkg";
    std::filesystem::remove_all(tempBase);
    const auto pkgRoot = tempBase / std::filesystem::path(u8"测试 工具 目录 with space");
    const auto calcDir = pkgRoot / "assets" / "tools" / "custom" / "Python" / "calc";
    std::filesystem::create_directories(calcDir);

    // 2. module_index.yaml (schema_version: 2, pure python package)
    {
        std::ofstream indexOut(pkgRoot / "module_index.yaml");
        indexOut << "schema_version: 2\n"
                 << "variant: cpu\n"
                 << "modules: {}\n"
                 << "tools:\n"
                 << "  custom.image.biased_mean:\n"
                 << "    definition: assets/tools/custom/Python/calc/calc.yaml\n";
    }

    // 3. calc.yaml declaration
    {
        std::ofstream yamlOut(calcDir / "calc.yaml");
        yamlOut << "schema_version: 1\n"
                << "tool_id: custom.image.biased_mean\n"
                << "display_name: 偏置均值计算\n"
                << "version: 1.0.0\n"
                << "kind: python_function\n"
                << "entry:\n"
                << "  file: calc.py\n"
                << "  symbol: calculate_biased_mean\n"
                << "init_params: []\n"
                << "inputs:\n"
                << "  - id: images\n"
                << "    name: 输入图像\n"
                << "    type: ImageBatch\n"
                << "outputs:\n"
                << "  - id: biased_means\n"
                << "    name: 偏置均值\n"
                << "    type: NumericArray\n"
                << "    dtype: float64\n"
                << "    rank: 1\n"
                << "    layout: dense\n";
    }

    // 4. calc.py main script with relative import and adjacent resource
    {
        std::ofstream pyOut(calcDir / "calc.py");
        pyOut << "import pathlib\n"
              << "from . import helper\n"
              << "import numpy as np\n"
              << "\n"
              << "def calculate_biased_mean(inputs):\n"
              << "    res_file = pathlib.Path(__file__).parent / 'bias.dat'\n"
              << "    bias = float(res_file.read_text(encoding='utf-8').strip())\n"
              << "    images = inputs.get('images', [])\n"
              << "    means = []\n"
              << "    for img in images:\n"
              << "        m = float(np.mean(img))\n"
              << "        means.append(helper.apply_bias(m, bias))\n"
              << "    return {\n"
              << "        '__status_code': 0,\n"
              << "        '__status_message': '',\n"
              << "        'biased_means': np.array(means, dtype=np.float64)\n"
              << "    }\n";
    }

    // 5. helper.py relative import
    {
        std::ofstream helperOut(calcDir / "helper.py");
        helperOut << "def apply_bias(val, bias):\n"
                  << "    return val + bias\n";
    }

    // 6. bias.dat adjacent resource
    {
        std::ofstream datOut(calcDir / "bias.dat");
        datOut << "10.5\n";
    }

    // 7. Extra unindexed files: must NOT automatically become tools (US-20)
    {
        std::ofstream unindexedPy(calcDir / "unindexed_tool.py");
        unindexedPy << "def unindexed(inputs): return {}\n";
        std::ofstream unindexedYaml(calcDir / "unindexed_tool.yaml");
        unindexedYaml << "schema_version: 1\n"
                      << "tool_id: custom.unindexed\n"
                      << "display_name: 未索引工具\n"
                      << "version: 1.0.0\n"
                      << "kind: python_function\n"
                      << "entry:\n"
                      << "  file: unindexed_tool.py\n"
                      << "  symbol: unindexed\n";
    }

    // 8. SDK readToolsPackage & validation
    visionrt::validateToolsPackage(pkgRoot);
    const auto package = visionrt::readToolsPackage(pkgRoot);
    assert(package.tools.size() == 1);
    assert(package.tools[0].toolId == "custom.image.biased_mean");

    // 9. ToolRegistry registration: unindexed tools do NOT appear
    ToolRegistry registry;
    registry.registerPackage(package);
    assert(registry.definitions().size() == 1);
    assert(registry.definitions()[0].id == "custom.image.biased_mean");
    assert(registry.definitions()[0].displayName == "偏置均值计算");

    // 10. PythonRuntime initialization & execution
    const auto loaded = runtime.refresh(registry.definitions());
    assert(loaded.size() == 1 && loaded[0].status.code == 0);
    registry.setPythonImplementation(loaded[0].toolId, runtime.factory(*registry.find(loaded[0].toolId)),
                                     loaded[0].status);

    const ToolServices services;
    {
        auto instance = registry.create("custom.image.biased_mean", services);
        assert(instance != nullptr);

        // Execute with 2 images: 40.0 + 10.5 = 50.5, 20.0 + 10.5 = 30.5
        cv::Mat    mat1(10, 10, CV_8UC1, cv::Scalar(40));
        cv::Mat    mat2(10, 10, CV_8UC1, cv::Scalar(20));
        ImageBatch batch({OwnedImage::copyOf(mat1), OwnedImage::copyOf(mat2)});
        auto       result = instance->execute({
            {"images", batch}
        });
        assert(result.status.code == 0);
        const auto &meansOut = std::get<NumericArray>(result.outputs.at("biased_means"));
        assert(meansOut.buffers().size() == 1);
        const auto vals = meansOut.buffers()[0].values<double>();
        assert(vals.size() == 2);
        assert(std::abs(vals[0] - 50.5) < 1e-6);
        assert(std::abs(vals[1] - 30.5) < 1e-6);
    }

    // 11. Declaration change reflected in registry metadata without executing algorithm
    {
        std::ofstream yamlOut(calcDir / "calc.yaml");
        yamlOut << "schema_version: 1\n"
                << "tool_id: custom.image.biased_mean\n"
                << "display_name: 更新后的偏置均值\n"
                << "version: 2.0.0\n"
                << "kind: python_function\n"
                << "entry:\n"
                << "  file: calc.py\n"
                << "  symbol: calculate_biased_mean\n"
                << "init_params: []\n"
                << "inputs: []\n"
                << "outputs: []\n";
    }
    const auto   updatedPkg = visionrt::readToolsPackage(pkgRoot);
    ToolRegistry updatedRegistry;
    updatedRegistry.registerPackage(updatedPkg);
    assert(updatedRegistry.definitions().size() == 1);
    assert(updatedRegistry.definitions()[0].displayName == "更新后的偏置均值");
    assert(updatedRegistry.definitions()[0].version == "2.0.0");

    // 12. Negative: Missing script file is diagnosed cleanly by readToolsPackage
    {
        std::filesystem::rename(calcDir / "calc.py", calcDir / "calc.py.bak");
        bool rejected = false;
        try
        {
            visionrt::readToolsPackage(pkgRoot);
        }
        catch (const ContractError &err)
        {
            rejected = true;
            assert(std::string(err.what()).find("script file does not exist") != std::string::npos);
        }
        assert(rejected);
        std::filesystem::rename(calcDir / "calc.py.bak", calcDir / "calc.py");
    }

    // 13. Negative: Python tool illegally specifying module in index is rejected
    {
        std::filesystem::create_directories(pkgRoot / "bin");
        {
            std::ofstream dummyLib(pkgRoot / "bin" / "dummy.dll");
            dummyLib << "dummy\n";
        }
        {
            std::ofstream badIndex(pkgRoot / "module_index.yaml");
            badIndex << "schema_version: 2\n"
                     << "variant: cpu\n"
                     << "modules:\n"
                     << "  dummy_mod:\n"
                     << "    library: bin/dummy.dll\n"
                     << "tools:\n"
                     << "  custom.image.biased_mean:\n"
                     << "    module: dummy_mod\n"
                     << "    definition: assets/tools/custom/Python/calc/calc.yaml\n";
        }
        bool rejected = false;
        try
        {
            visionrt::readToolsPackage(pkgRoot);
        }
        catch (const ContractError &err)
        {
            rejected = true;
            assert(std::string(err.what()).find("Python tool") != std::string::npos);
        }
        assert(rejected);
    }

    // 14. Reset runtime
    runtime.refresh({});

    // Cleanup
    std::filesystem::remove_all(tempBase);
}

void pathInputs(PythonRuntime &runtime)
{
    Sources sources;
    sources.put("entry.py", R"(
class PathProbe:
    def initialize(self, params):
        self.base = params['base_dir']
        return {'__status_code': 0, '__status_message': 'ok'}
    def execute(self, inputs):
        return {'__status_code': 0, '__status_message': 'ok',
                'resolved': self.base + '|' + inputs['input_path']}
    def release(self):
        return {'__status_code': 0, '__status_message': 'ok'}
)");
    sources.put("paths.yaml", R"(
schema_version: 1
tool_id: python.paths
display_name: Path probe
version: 1.0.0
kind: python_class
entry: {file: entry.py, symbol: PathProbe}
init_params:
  - {id: init_mode, name: Mode, type: String, editor: text}
  - {id: base_dir, name: Base, type: String, editor: path, path_mode_from: init_mode}
inputs:
  - {id: input_mode, name: Mode, type: String, editor: text}
  - {id: input_path, name: Input, type: String, editor: path, path_mode_from: input_mode}
outputs:
  - {id: resolved, name: Resolved, type: String}
)");
    const auto definition = parseDefinition(sources.directory / "paths.yaml", false);
    const auto loaded = runtime.refresh(std::span<const ToolDefinition>(&definition, 1));
    assert(loaded.size() == 1 && loaded[0].status.code == 0);

    const auto project = sources.directory / std::filesystem::path(u8"工程 目录");
    std::filesystem::create_directories(project);
    const auto text = [](const std::filesystem::path &value) {
        const auto utf8 = value.u8string();
        return std::string(reinterpret_cast<const char *>(utf8.data()), utf8.size());
    };
    const auto expectedBase = text(project / std::filesystem::path(L"数据") / L"模型.bin");
    const auto expectedInput = text(project / std::filesystem::path(L"图像") / L"输入.png");
    const ToolServices services{PathResolver(project)};
    auto instance = runtime.factory(definition)(services);
    assert(instance->initialize({
        {"init_mode", Utf8("file")},
        {"base_dir", Utf8("数据/模型.bin")}}).code == 0);
    const auto result = instance->execute({
        {"input_mode", Utf8("file")},
        {"input_path", Utf8("图像/输入.png")}});
    assert(result.status.code == 0);
    assert(std::get<Utf8>(result.outputs.at("resolved")).str() == expectedBase + "|" + expectedInput);

    const auto empty = instance->execute({
        {"input_mode", Utf8("file")},
        {"input_path", Utf8("")}});
    assert(empty.status.code == 0);
    assert(std::get<Utf8>(empty.outputs.at("resolved")).str() == expectedBase + "|");
    instance.reset();
    runtime.refresh({});
}
} // namespace

int main(int argc, char **argv)
{
    assert(argc == 2 || (argc == 3 && std::string_view(argv[1]) == "--package"));
    const auto packageRoot = std::filesystem::absolute(std::filesystem::u8path(argv[argc - 1]));
    PythonRuntime runtime;
    auto          worker = std::async(std::launch::async,
                                      [&]
                                      {
                                 if (argc == 3)
                                 {
                                     deployedPurePythonPackage(runtime, packageRoot);
                                     return;
                                 }
                                 readonlyOwners();
                                 strictConversions();
                                 adaptersAndRefresh(runtime);
                                 exampleTools(runtime, packageRoot);
                                 pathInputs(runtime);
                                 packagePythonExecutionDemonstration(runtime);
                                      });
    try
    {
        worker.get();
    }
    catch (const std::exception &error)
    {
        py::gil_scoped_acquire gil;
        std::cerr << "Python contract failed: " << error.what() << '\n';
        return 1;
    }
    if (argc == 2)
        std::cout << "Python ownership, strict contracts, adapters, fresh-source refresh and package execution passed\n";
}
