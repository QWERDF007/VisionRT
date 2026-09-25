#include "python/PythonBridge.h"
#include "python/PythonRuntime.h"
#include "core/ToolRegistry.h"

#include <pybind11/embed.h>
#include <pybind11/numpy.h>

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
template<class F> void rejects(F&& operation) {
    bool rejected = false;
    try { operation(); } catch (const std::exception&) { rejected = true; }
    assert(rejected);
}

void readonlyOwners() {
    py::gil_scoped_acquire gil;
    py::dict scope;
    std::weak_ptr<const void> retained;
    {
        const auto image = OwnedImage::copyOf(cv::Mat(4, 5, CV_16UC1, cv::Scalar(123)));
        const auto crop = image.region({1, 1, 2, 2});
        retained = crop.owner();
        scope["image"] = toPythonReadonly(ImageBatch({crop})).cast<py::list>()[0];
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
)", scope);
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
    const auto buffer = NumericBuffer::copy(DType::Float32, {3}, std::as_bytes(std::span(data)));
    auto values = toPythonReadonly(NumericArray(DType::Float32, 1, Layout::Dense, {buffer}));
    assert(values.cast<py::array>().data() == buffer.data());
    const auto copied = fromPython({Type::NumericArray, DType::Float32, 1}, values);
    assert(std::get<NumericArray>(copied).buffers()[0].data() != buffer.data());
    assert(std::get<NumericArray>(copied).buffers()[0].values<float>()[1] == .9F);
    scope["values"] = values;
    scope["roi"] = toPythonReadonly(RoiBatch({{Roi::circle(2, 3, 4)}})).cast<py::list>()[0].cast<py::list>()[0];
    py::exec(R"(
for value in (values, roi):
    assert not value.flags.writeable
    try:
        value[0] = 0
        raise AssertionError('input mutation succeeded')
    except ValueError:
        pass
)", scope);
}

void strictConversions() {
    PortValue copiedImage;
    PortValue copiedScores;
    PortValue copiedRois;
    PortValue copiedNames;
    {
        py::gil_scoped_acquire gil;
        py::dict scope;
        py::exec(R"(
import numpy as np
image = np.arange(24, dtype=np.uint16).reshape(4, 6)[::-1, ::2]
scores = [np.empty((0,), dtype=np.float32), np.array([.8, .9, .7], dtype=np.dtype('>f4').newbyteorder('='))]
classes = [np.empty((0,), dtype=np.int64), np.array([1, 2, 1], dtype=np.int64)]
names = [np.empty((0,), dtype='U'), np.array(['猫', 'dog', '猫'], dtype='U')]
rois = [[], [np.array([3, 1, 2, 3], dtype=np.float32),
             np.array([2, 0, 0, 4, 5, 0], dtype=np.float32),
             np.array([1, 0, 0, 2, 2], dtype=np.float32)]]
)", scope);
        const TypeSpec scoresType{Type::NumericArray, DType::Float32, 2, Layout::BatchRagged};
        copiedImage = fromPython({Type::ImageBatch}, py::list(py::make_tuple(scope["image"])));
        copiedScores = fromPython(scoresType, scope["scores"]);
        copiedNames = fromPython({Type::StringArray, {}, 2, Layout::BatchRagged}, scope["names"]);
        copiedRois = fromPython({Type::RoiBatch}, scope["rois"]);
        const auto classes = fromPython({Type::NumericArray, DType::Int64, 2, Layout::BatchRagged}, scope["classes"]);
        assert(std::get<NumericArray>(classes).buffers()[1].values<std::int64_t>()[1] == 2);
        scope["roundtrip"] = toPythonReadonly(copiedNames);
        py::exec("assert roundtrip[1].dtype.kind == 'U'\nassert roundtrip[1].tolist() == ['猫', 'dog', '猫']", scope);
        rejects([&] { fromPython(scoresType, py::eval("[np.ones(3, dtype=np.float64)]", scope)); });
        rejects([&] { fromPython(scoresType, py::eval("[np.arange(6, dtype=np.float32)[::2]]", scope)); });
        rejects([&] { fromPython(scoresType, py::eval("np.ones((1, 3), dtype=np.float32)", scope)); });
        rejects([&] { fromPython(scoresType, py::eval("[np.ones((1, 3), dtype=np.float32)]", scope)); });
        rejects([&] { fromPython(scoresType, py::eval("[np.lib.stride_tricks.as_strided(np.zeros(1, dtype=np.float32), shape=(4,), strides=(4,))]", scope)); });
        rejects([&] { fromPython({Type::ImageBatch}, py::eval("[np.lib.stride_tricks.as_strided(np.zeros(1, dtype=np.uint8), shape=(3,3), strides=(3,1))]", scope)); });
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

struct Sources {
    std::filesystem::path directory = std::filesystem::temp_directory_path() /
        ("visionrt_python_" + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
    Sources() { std::filesystem::create_directory(directory); }
    ~Sources() { std::error_code error; std::filesystem::remove_all(directory, error); }
    void put(const std::string& name, const std::string& source) {
        std::ofstream stream(directory / name, std::ios::binary | std::ios::trunc);
        stream.exceptions(std::ios::badbit | std::ios::failbit);
        stream << source;
    }
    ToolDefinition definition(const std::string& id, ToolKind kind, const std::string& symbol,
                              bool secondOutput = false) {
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

void adaptersAndRefresh(PythonRuntime& runtime) {
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
    auto counter = sources.definition("python.counter", ToolKind::PythonClass, "Counter");
    auto broken = sources.definition("python.broken", ToolKind::PythonClass, "Broken");
    auto missing = sources.definition("python.missing", ToolKind::PythonFunction, "missing");
    auto extra = sources.definition("python.extra", ToolKind::PythonFunction, "extra");
    auto wrong = sources.definition("python.wrong", ToolKind::PythonFunction, "wrong");
    auto business = sources.definition("python.business", ToolKind::PythonFunction, "business");
    auto partial = sources.definition("python.partial", ToolKind::PythonFunction, "partial", true);
    std::vector<ToolDefinition> definitions{function, counter, broken, missing, extra, wrong, business, partial};
    std::uintptr_t numpyIdentity;
    {
        py::gil_scoped_acquire gil;
        numpyIdentity = reinterpret_cast<std::uintptr_t>(py::module_::import("numpy").ptr());
        py::module_::import("py_compile").attr("compile")((sources.directory / "helper.py").string(),
                                                       py::arg("doraise") = true);
    }
    const auto loaded = runtime.refresh(definitions);
    for (const auto& record : loaded) assert(record.status.code == 0);
    const ToolServices services;
    auto first = runtime.factory(counter)(services);
    auto second = runtime.factory(counter)(services);
    assert(first->initialize({}).code == 0 && second->initialize({}).code == 0);
    assert(std::get<std::int64_t>(first->execute({}).outputs.at("value")) == 1);
    assert(std::get<std::int64_t>(first->execute({}).outputs.at("value")) == 2);
    assert(std::get<std::int64_t>(second->execute({}).outputs.at("value")) == 1);
    rejects([&] { runtime.refresh(definitions); });
    assert(first->release().code == 0 && second->release().code == 0);
    first.reset();
    second.reset();

    auto failures = runtime.factory(broken)(services);
    const auto initialization = failures->initialize({});
    assert(initialization.code == -1 && initialization.message.find("initialization detail") != std::string::npos);
    assert(initialization.message.find("entry.py") != std::string::npos);
    const auto execution = failures->execute({});
    assert(execution.status.code == -1 && execution.status.message.find("execution detail") != std::string::npos);
    assert(execution.outputs.size() == 1 && isMissing(execution.outputs.at("value")));
    const auto cleanup = failures->release();
    assert(cleanup.code == -4 && cleanup.message.find("release detail") != std::string::npos);
    failures.reset();
    for (const auto& definition : {missing, extra, wrong}) {
        auto instance = runtime.factory(definition)(services);
        const auto result = instance->execute({});
        assert(result.status.code == -2 && result.outputs.size() == 1);
        assert(isMissing(result.outputs.at("value")));
    }
    {
        auto instance = runtime.factory(business)(services);
        const auto result = instance->execute({});
        assert(result.status.code == 5 && std::get<std::int64_t>(result.outputs.at("value")) == 7);
    }
    {
        auto instance = runtime.factory(partial)(services);
        const auto result = instance->execute({});
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
        auto instance = oldFactory(services);
        const auto result = instance->execute({});
        assert(result.status.code == -3 && result.outputs.size() == 1 && isMissing(result.outputs.at("value")));
    }
    runtime.refresh({});
}

void exampleTools(PythonRuntime& runtime, const std::filesystem::path& root) {
    const auto meanPath = root / std::filesystem::path(u8"custom/Python/示例/custom.image.mean.yaml");
    const auto mean = parseDefinition(std::filesystem::relative(meanPath), false);
    const auto counter = parseDefinition(root / std::filesystem::path(u8"custom/Python/示例/custom.call.counter.yaml"), false);
    ToolRegistry registry;
    registry.addDefinition(mean);
    registry.addDefinition(counter);
    for (const auto& loaded : runtime.refresh(registry.definitions())) {
        assert(loaded.status.code == 0);
        registry.setPythonImplementation(loaded.toolId, runtime.factory(*registry.find(loaded.toolId)), loaded.status);
    }
    const ToolServices services;
    {
        auto function = registry.create(mean.id, services);
        const auto image = OwnedImage::copyOf(cv::Mat(2, 3, CV_8UC1, cv::Scalar(8)));
        const auto result = function->execute({{"images", ImageBatch({image})}});
        assert(result.status.code == 0 && result.outputs.size() == 1);
        const auto& means = std::get<NumericArray>(result.outputs.at("means"));
        assert(means.buffers()[0].values<double>()[0] == 8.0);
        const auto missing = function->execute({{"images", Missing{}}});
        assert(missing.status.code == 1 && missing.outputs.size() == 1);
        assert(isMissing(missing.outputs.at("means")));
        const auto empty = function->execute({{"images", ImageBatch{}}});
        assert(empty.status.code == 0);
        assert(std::get<NumericArray>(empty.outputs.at("means")).buffers()[0].shape()[0] == 0);
        auto first = registry.create(counter.id, services);
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
    auto unavailable = runtime.factory(mean)(services);
    const auto result = unavailable->execute({});
    assert(result.status.code == -3 && result.outputs.size() == 1);
    assert(isMissing(result.outputs.at("means")));
}
}

int main(int argc, char** argv) {
    assert(argc == 2);
    const auto examples = std::filesystem::absolute(std::filesystem::u8path(argv[1]));
    PythonRuntime runtime;
    auto worker = std::async(std::launch::async, [&] {
        readonlyOwners();
        strictConversions();
        adaptersAndRefresh(runtime);
        exampleTools(runtime, examples);
    });
    try {
        worker.get();
    } catch (const std::exception& error) {
        py::gil_scoped_acquire gil;
        std::cerr << "Python contract failed: " << error.what() << '\n';
        return 1;
    }
    std::cout << "Python ownership, strict contracts, adapters and fresh-source refresh passed\n";
}
