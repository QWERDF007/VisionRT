#include "core/ToolRegistry.h"
#include "core/ValueYaml.h"
#include "visionrt/sdk/SchemaYaml.h"

#include <algorithm>
#include <cassert>
#include <chrono>
#include <fstream>
#include <limits>
#include <stdexcept>

using namespace visionrt;

namespace {
template<class F> std::string rejects(F&& operation) {
    try { operation(); }
    catch (const std::exception& error) { return error.what(); }
    throw std::runtime_error("Expected contract rejection");
}

struct TemporaryTools {
    std::filesystem::path root = std::filesystem::temp_directory_path() /
        ("visionrt-registry-" + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
    TemporaryTools() { std::filesystem::create_directories(root); }
    ~TemporaryTools() { std::error_code error; std::filesystem::remove_all(root, error); }
    std::filesystem::path put(const std::string& relative, const std::string& yaml) const {
        const auto path = root / relative;
        std::filesystem::create_directories(path.parent_path());
        std::ofstream file(path, std::ios::binary);
        file << yaml;
        file.close();
        assert(file);
        return path;
    }
};

YAML::Node definition(const std::string& id) {
    return YAML::Load("schema_version: 1\ntool_id: " + id + R"(
display_name: Contract tool
version: v1
kind: cpp_function
entry: {registry_name: test.pass}
init_params: []
inputs:
  - {id: images, name: Images, type: ImageBatch, default: null}
  - {id: rois, name: ROIs, type: RoiBatch, default: [], image_input: images}
  - {id: threshold, name: Threshold, type: Int, default: 4, min: 0, max: 10, step: 1}
outputs:
  - {id: images, name: Images, type: ImageBatch, display: {same_geometry_as_input: images, color_order: BGR}}
  - {id: rois, name: ROIs, type: RoiBatch, image_port: images}
)");
}

class PassTool final : public IToolInstance {
public:
    ToolStatus initialize(const Values&) override { return {}; }
    ToolResult execute(const Values& input) override {
        return {{}, {{"images", input.at("images")}, {"rois", input.at("rois")}}};
    }
    ToolStatus release() override { return {}; }
};
}

int main(int argc, char** argv) {
    assert(argc == 2);
    const TypeSpec integer{Type::Int};
    for (const auto text : {"-9223372036854775808", "9223372036854775807"}) {
        const auto value = decodeValue(YAML::Load(text), integer);
        assert(std::get<std::int64_t>(value) == parseInt64(text));
        const auto serialized = YAML::Dump(encodeValue(value, integer));
        assert(std::get<std::int64_t>(decodeValue(YAML::Load(serialized), integer)) == parseInt64(text));
    }
    rejects([&] { decodeValue(YAML::Load("9223372036854775808"), integer); });
    rejects([&] { decodeValue(YAML::Load("true"), integer); });
    rejects([&] { decodeValue(YAML::Load("2.0"), integer); });
    assert(isMissing(decodeValue(YAML::Load("null"), {Type::ImageBatch})));
    assert(std::get<ImageBatch>(decodeValue(YAML::Load("[]"), {Type::ImageBatch})).images().empty());
    rejects([] { decodeValue(YAML::Load("[1]"), {Type::ImageBatch}); });
    rejects([] { decodeTypeSpec(YAML::Load("{type: NumericArray, rank: 1}")); });
    rejects([] { decodeTypeSpec(YAML::Load("{type: NumericArray, dtype: int64}")); });
    rejects([] { decodeTypeSpec(YAML::Load("{type: StringArray, rank: 1, dtype: int64}")); });
    const TypeSpec dense{Type::NumericArray, DType::Int64, 2, Layout::Dense};
    assert(decodeTypeSpec(encodeTypeSpec(dense)) == dense);
    const auto denseValue = decodeValue(YAML::Load("{shape: [1, 2], data: [9223372036854775807, -9223372036854775808]}"), dense);
    const auto denseRoundTrip = decodeValue(YAML::Load(YAML::Dump(encodeValue(denseValue, dense))), dense);
    const auto denseData = std::get<NumericArray>(denseRoundTrip).buffers()[0].values<std::int64_t>();
    assert(denseData[0] == std::numeric_limits<std::int64_t>::max());
    assert(denseData[1] == std::numeric_limits<std::int64_t>::min());
    rejects([&] { decodeValue(YAML::Load("{shape: [1, 2], data: [1]}"), dense); });
    rejects([&] { decodeValue(YAML::Load("{shape: [-1, 0], data: []}"), dense); });
    rejects([&] { decodeValue(YAML::Load("{shape: [9223372036854775807, 3], data: []}"), dense); });
    const TypeSpec bytes{Type::NumericArray, DType::UInt8, 1};
    const auto byteValue = decodeValue(YAML::Load("{shape: [2], data: [0, 255]}"), bytes);
    const auto byteRoundTrip = decodeValue(encodeValue(byteValue, bytes), bytes);
    assert(std::get<NumericArray>(byteRoundTrip).buffers()[0].values<std::uint8_t>()[1] == 255);
    rejects([&] { decodeValue(YAML::Load("{shape: [1], data: [256]}"), bytes); });
    const TypeSpec ints{Type::NumericArray, DType::Int32, 1};
    rejects([&] { decodeValue(YAML::Load("{shape: [1], data: [-2147483649]}"), ints); });
    const TypeSpec flags{Type::NumericArray, DType::Bool, 1};
    const auto flagValue = decodeValue(YAML::Load("{shape: [2], data: [true, false]}"), flags);
    const auto flagRoundTrip = decodeValue(encodeValue(flagValue, flags), flags);
    assert(std::get<NumericArray>(flagRoundTrip).buffers()[0].values<bool>()[0]);
    assert(!std::get<NumericArray>(flagRoundTrip).buffers()[0].values<bool>()[1]);
    rejects([&] { decodeValue(YAML::Load("{shape: [1], data: [1]}"), flags); });
    const TypeSpec ragged{Type::NumericArray, DType::Float32, 2, Layout::BatchRagged};
    const auto raggedValue = decodeValue(YAML::Load("[{shape: [0], data: []}, {shape: [3], data: [0.8, 0.95, 0.7]}]"), ragged);
    const auto raggedRoundTrip = decodeValue(encodeValue(raggedValue, ragged), ragged);
    const auto& scores = std::get<NumericArray>(raggedRoundTrip);
    assert(scores.buffers()[0].shape()[0] == 0);
    assert(scores.buffers()[1].values<float>()[1] == 0.95F);
    const TypeSpec names{Type::StringArray, {}, 2, Layout::BatchRagged};
    const auto namesValue = decodeValue(YAML::Load("[{shape: [0], data: []}, {shape: [2], data: [one, two]}]"), names);
    const auto namesRoundTrip = decodeValue(encodeValue(namesValue, names), names);
    assert(std::get<StringArray>(namesRoundTrip).buffers()[1].values()[1] == "two");
    const TypeSpec denseNames{Type::StringArray, {}, 1};
    const auto literalNames = decodeValue(YAML::Load("{shape: [2], data: ['null', '001']}"), denseNames);
    const auto literalRoundTrip = decodeValue(YAML::Load(YAML::Dump(encodeValue(literalNames, denseNames))), denseNames);
    assert(std::get<StringArray>(literalRoundTrip).buffers()[0].values()[0] == "null");
    assert(std::get<StringArray>(literalRoundTrip).buffers()[0].values()[1] == "001");
    const auto rois = decodeValue(YAML::Load("[[[1, 0, 0, 1, 1], [2, 0, 0, 2, 3, 90], [3, 1, 2, 3], [4, 0, 0, 1, 0, 0, 1], [5, 1, 2]], []]"), {Type::RoiBatch});
    const auto roiRoundTrip = decodeValue(encodeValue(rois, {Type::RoiBatch}), {Type::RoiBatch});
    const auto& shapes = std::get<RoiBatch>(roiRoundTrip).images();
    assert(shapes[0][0].kind() == RoiKind::Segment);
    assert(shapes[0][1].encoded()[5] == 90);
    assert(shapes[0][2].kind() == RoiKind::Circle);
    assert(shapes[0][3].kind() == RoiKind::Polygon);
    assert(shapes[0][4].kind() == RoiKind::PointSet);
    assert(shapes[1].empty());
    rejects([] { decodeValue(YAML::Load("[[[3, 1, 2, 0]]]"), {Type::RoiBatch}); });

    TemporaryTools temporary;
    const auto builtinPath = temporary.put("builtin/Image/Basic/a.yaml", YAML::Dump(definition("test.pass")));
    temporary.put("custom/Image/Basic/duplicate.yaml", YAML::Dump(definition("test.pass")));
    temporary.put("builtin/Image/Basic/broken.yaml", "inputs: [");
    auto other = definition("test.other");
    other["entry"]["registry_name"] = "test.other";
    temporary.put("custom/Image/Basic/valid.yaml", YAML::Dump(other));
    ToolRegistry registry;
    registry.scan(temporary.root);
    const auto duplicate = std::find_if(registry.diagnostics().begin(), registry.diagnostics().end(),
        [](const auto& diagnostic) { return !diagnostic.relatedPath.empty(); });
    assert(duplicate != registry.diagnostics().end());
    assert(duplicate->relatedPath == builtinPath);
    assert(duplicate->path.filename() == "duplicate.yaml");
    assert(registry.definitions().size() == 2);
    assert(registry.diagnostics().size() == 2);
    assert(registry.find("test.pass")->definitionPath == builtinPath);
    assert(registry.find("test.pass")->category == "Image");
    assert(registry.find("test.pass")->subcategory == "Basic");
    assert(registry.find("test.pass")->outputs[0].id == "__status_code");
    assert(registry.find("test.pass")->outputs[1].id == "__status_message");
    assert(registry.find("test.pass")->outputs[2].id == "images");
    assert(registry.availability("not.loaded").state == ToolAvailability::DefinitionMissing);
    assert(registry.availability("test.pass").state == ToolAvailability::ImplementationUnavailable);
    assert(rejects([&] { registry.create("test.pass", {}); }).find("unavailable") != std::string::npos);
    auto contract = *registry.find("test.pass");
    auto incompatible = contract;
    incompatible.inputs[2].type = {Type::Float};
    incompatible.inputs[2].defaultValue = 4.0;
    rejects([&] { registry.registerCpp("test.pass", incompatible,
        [](const ToolServices&) { return std::make_unique<PassTool>(); }); });
    registry.registerCpp("test.pass", contract,
        [](const ToolServices&) { return std::make_unique<PassTool>(); });
    assert(registry.availability("test.pass").state == ToolAvailability::Available);
    auto tool = registry.create("test.pass", {});
    assert(tool->initialize({}));
    const auto result = tool->execute({{"images", ImageBatch{}}, {"rois", RoiBatch{}}});
    assert(result.status && std::get<ImageBatch>(result.outputs.at("images")).images().empty());
    assert(tool->release());
    auto customContract = parseDefinitionNode(definition("custom.pass"), temporary.root / "custom/Image/Basic/copy.yaml", false);
    registry.validateContract(customContract);
    registry.addDefinition(std::move(customContract));
    assert(registry.availability("custom.pass").state == ToolAvailability::Available);
    auto changedSignature = definition("custom.changed");
    changedSignature["inputs"][2]["type"] = "Float";
    const auto changed = parseDefinitionNode(changedSignature, temporary.root / "custom/Image/Basic/changed.yaml", false);
    rejects([&] { registry.validateContract(changed); });
    auto invalidReplacement = changed;
    invalidReplacement.id = "test.pass";
    invalidReplacement.displayName = "Rejected replacement";
    rejects([&] { registry.replaceDefinition(invalidReplacement); });
    assert(registry.find("test.pass")->displayName == contract.displayName);
    assert(registry.availability("test.pass").state == ToolAvailability::Available);
    auto renamed = contract;
    renamed.displayName = "Saved custom metadata";
    registry.replaceDefinition(std::move(renamed));
    assert(registry.find("test.pass")->displayName == "Saved custom metadata");
    assert(registry.availability("test.pass").state == ToolAvailability::Available);
    registry.scan(temporary.root);
    assert(registry.definitions()[0].id == "test.pass");
    assert(registry.definitions()[1].id == "test.other");
    assert(registry.availability("test.pass").state == ToolAvailability::Available);

    std::vector<std::filesystem::path> invalidYamlPaths;
    int invalidSeq = 0;
    const auto rejectDefinition = [&](YAML::Node yaml) {
        const auto path = temporary.put("invalid/Image/Basic/tool_" + std::to_string(++invalidSeq) + ".yaml", YAML::Dump(yaml));
        invalidYamlPaths.push_back(path);
        const auto message = rejects([&] { parseDefinition(path, false); });
        assert(message.find("tool_") != std::string::npos);
    };
    auto invalid = definition("bad.duplicate");
    invalid["inputs"].push_back(YAML::Clone(invalid["inputs"][0]));
    rejectDefinition(invalid);
    invalid = definition("bad.reserved"); invalid["outputs"][0]["id"] = "__status_code"; rejectDefinition(invalid);
    invalid = definition("bad.init"); invalid["init_params"].push_back(YAML::Clone(invalid["inputs"][0])); rejectDefinition(invalid);
    invalid = definition("bad.reference"); invalid["outputs"][1]["image_port"] = "rois"; rejectDefinition(invalid);
    invalid = definition("bad.display"); invalid["outputs"][0]["display"]["same_geometry_as_input"] = "rois"; rejectDefinition(invalid);
    invalid = definition("bad.range"); invalid["inputs"][2]["min"] = 20; rejectDefinition(invalid);
    invalid = definition("bad.default"); invalid["inputs"][2]["default"] = 12; rejectDefinition(invalid);
    invalid = definition("bad.step"); invalid["inputs"][2]["step"] = 0; rejectDefinition(invalid);
    invalid = definition("bad.array"); invalid["outputs"][1]["type"] = "NumericArray"; rejectDefinition(invalid);
    invalid = definition("bad.function"); invalid["init_params"].push_back(YAML::Load("{id: count, name: Count, type: Int}")); rejectDefinition(invalid);
    invalid = definition("bad.fields"); invalid["run_params"] = YAML::Node(YAML::NodeType::Sequence); rejectDefinition(invalid);
    invalid = definition("bad.kind"); invalid["kind"] = "unsupported_kind"; rejectDefinition(invalid);
    invalid = definition("bad.py_empty_file"); invalid["kind"] = "python_function"; invalid["entry"] = YAML::Load("{file: '', symbol: run}"); rejectDefinition(invalid);
    invalid = definition("bad.py_empty_sym"); invalid["kind"] = "python_function"; invalid["entry"] = YAML::Load("{file: run.py, symbol: ''}"); rejectDefinition(invalid);
    invalid = definition("bad.unknown_port_field"); invalid["inputs"][2]["unsupported_prop"] = 42; rejectDefinition(invalid);
    invalid = definition("bad.unknown_entry_field"); invalid["entry"]["extra"] = "val"; rejectDefinition(invalid);
    invalid = definition("bad.unknown_display_field"); invalid["outputs"][0]["display"]["extra"] = "val"; rejectDefinition(invalid);
    invalid = definition("bad.min_type"); invalid["inputs"][2]["min"] = "wrong_type"; rejectDefinition(invalid);
    invalid = definition("bad.max_type"); invalid["inputs"][2]["max"] = "wrong_type"; rejectDefinition(invalid);
    invalid = definition("bad.step_type"); invalid["inputs"][2]["step"] = "wrong_type"; rejectDefinition(invalid);
    invalid = definition("bad.nan_min"); invalid["inputs"][2]["type"] = "Float"; invalid["inputs"][2]["default"] = 1.0; invalid["inputs"][2]["min"] = ".nan"; rejectDefinition(invalid);
    invalid = definition("bad.inf_max"); invalid["inputs"][2]["type"] = "Float"; invalid["inputs"][2]["default"] = 1.0; invalid["inputs"][2]["max"] = ".inf"; rejectDefinition(invalid);
    invalid = definition("bad.default_less_min"); invalid["inputs"][2]["default"] = -1; rejectDefinition(invalid);
    invalid = definition("bad.options_out_of_range"); invalid["inputs"][2]["options"] = YAML::Load("[0, 5, 20]"); rejectDefinition(invalid);
    invalid = definition("bad.default_not_in_options"); invalid["inputs"][2]["options"] = YAML::Load("[0, 2, 4]"); invalid["inputs"][2]["default"] = 3; rejectDefinition(invalid);

    // Positive case: valid tool YAML with options and bounds
    auto validWithOptions = definition("valid.options");
    validWithOptions["inputs"][2]["options"] = YAML::Load("[0, 4, 8]");
    validWithOptions["inputs"][2]["default"] = 4;
    const auto validPath = temporary.put("valid/Image/Basic/options.yaml", YAML::Dump(validWithOptions));
    const auto parsedOptionsTool = parseDefinition(validPath, false);
    assert(parsedOptionsTool.id == "valid.options");
    assert(parsedOptionsTool.inputs[2].options.size() == 3);

    // Direct SchemaYaml SDK tests
    const std::string validYamlText = YAML::Dump(definition("sdk.direct"));
    const auto directDef = parseToolDefinitionYaml(validYamlText);
    assert(directDef.id == "sdk.direct");
    validateToolYaml(validYamlText);
    validateToolDefinition(directDef);
    rejects([&] { parseToolDefinitionYaml("unknown_top_field: 123"); });

    // Direct C++ semantic negative tests on validateToolDefinition
    {
        ToolDefinition bad = directDef;
        bad.id = "bad ID with spaces";
        rejects([&] { validateToolDefinition(bad); });

        bad = directDef;
        bad.displayName = "";
        rejects([&] { validateToolDefinition(bad); });

        bad = directDef;
        bad.version = "";
        rejects([&] { validateToolDefinition(bad); });

        bad = directDef;
        bad.kind = static_cast<ToolKind>(99);
        rejects([&] { validateToolDefinition(bad); });

        bad = directDef;
        bad.kind = ToolKind::PythonFunction;
        bad.script = "";
        bad.symbol = "run";
        rejects([&] { validateToolDefinition(bad); });

        bad = directDef;
        bad.kind = ToolKind::PythonFunction;
        bad.script = "tool.py";
        bad.symbol = "";
        rejects([&] { validateToolDefinition(bad); });

        bad = directDef;
        bad.inputs[2].minimum = PortValue{"wrong_type"};
        rejects([&] { validateToolDefinition(bad); });

        bad = directDef;
        bad.inputs[2].maximum = PortValue{"wrong_type"};
        rejects([&] { validateToolDefinition(bad); });

        bad = directDef;
        bad.inputs[2].step = PortValue{"wrong_type"};
        rejects([&] { validateToolDefinition(bad); });

        bad = directDef;
        bad.inputs[2].minimum = PortValue{std::int64_t{10}};
        bad.inputs[2].maximum = PortValue{std::int64_t{5}};
        rejects([&] { validateToolDefinition(bad); });

        bad = directDef;
        bad.inputs[2].step = PortValue{std::int64_t{0}};
        rejects([&] { validateToolDefinition(bad); });

        bad = directDef;
        bad.inputs[2].step = PortValue{std::int64_t{-1}};
        rejects([&] { validateToolDefinition(bad); });

        bad = directDef;
        bad.inputs[2].defaultValue = PortValue{std::int64_t{20}};
        rejects([&] { validateToolDefinition(bad); });

        bad = directDef;
        bad.inputs[2].defaultValue = PortValue{std::int64_t{-5}};
        rejects([&] { validateToolDefinition(bad); });

        bad = directDef;
        bad.inputs[2].options = {PortValue{std::int64_t{1}}, PortValue{std::int64_t{2}}};
        bad.inputs[2].defaultValue = PortValue{std::int64_t{4}};
        rejects([&] { validateToolDefinition(bad); });

        bad = directDef;
        bad.inputs[2].options = {PortValue{std::int64_t{100}}}; // exceeds max=10
        rejects([&] { validateToolDefinition(bad); });

        // Float NaN and Inf in C++
        ToolDefinition floatDef = directDef;
        floatDef.inputs[2].type = {Type::Float};
        floatDef.inputs[2].defaultValue = PortValue{std::numeric_limits<double>::quiet_NaN()};
        rejects([&] { validateToolDefinition(floatDef); });

        floatDef.inputs[2].defaultValue = PortValue{std::numeric_limits<double>::infinity()};
        rejects([&] { validateToolDefinition(floatDef); });

        floatDef.inputs[2].defaultValue = PortValue{1.0};
        floatDef.inputs[2].minimum = PortValue{std::numeric_limits<double>::quiet_NaN()};
        rejects([&] { validateToolDefinition(floatDef); });

        floatDef.inputs[2].minimum = PortValue{0.0};
        floatDef.inputs[2].maximum = PortValue{std::numeric_limits<double>::infinity()};
        rejects([&] { validateToolDefinition(floatDef); });

        floatDef.inputs[2].maximum = PortValue{10.0};
        floatDef.inputs[2].step = PortValue{std::numeric_limits<double>::quiet_NaN()};
        rejects([&] { validateToolDefinition(floatDef); });
    }

    // Validate CLI executable tests across valid definition and all negative corpus files
    std::filesystem::path validatorExe = std::filesystem::exists("bin/visionrt_validate_tool.exe")
        ? std::filesystem::path("bin/visionrt_validate_tool.exe")
        : (std::filesystem::exists("bin/visionrt_validate_tool") ? std::filesystem::path("bin/visionrt_validate_tool") : std::filesystem::path());
    if (!validatorExe.empty()) {
        validatorExe = std::filesystem::absolute(validatorExe).make_preferred();
        const auto validCommand = "\"\"" + validatorExe.string() + "\" \"" + std::filesystem::absolute(validPath).make_preferred().string() + "\"\"";
        const auto codeValid = std::system(validCommand.c_str());
        assert(codeValid == 0);
        const auto invalidFile = temporary.put("invalid/bad.yaml", "unknown_field: 123\n");
        invalidYamlPaths.push_back(invalidFile);
        for (const auto& path : invalidYamlPaths) {
            const auto invalidCommand = "\"\"" + validatorExe.string() + "\" \"" + std::filesystem::absolute(path).make_preferred().string() + "\"\"";
            const auto codeInvalid = std::system(invalidCommand.c_str());
            assert(codeInvalid != 0);
        }
    }

    TemporaryTools refreshTools;
    refreshTools.put("builtin/Image/Basic/cpp.yaml", YAML::Dump(definition("refresh.cpp")));
    auto python = definition("refresh.python");
    python["kind"] = "python_function";
    python["entry"] = YAML::Load("{file: tool.py, symbol: execute}");
    const auto pythonPath = refreshTools.put("custom/Python/Basic/python.yaml", YAML::Dump(python));
    ToolRegistry refreshed;
    refreshed.scan(refreshTools.root);
    const auto originalCppName = refreshed.find("refresh.cpp")->displayName;
    refreshed.setPythonImplementation("refresh.python",
        [](const ToolServices&) { return std::make_unique<PassTool>(); });
    assert(refreshed.availability("refresh.python").state == ToolAvailability::Available);
    auto otherPython = YAML::Clone(python);
    otherPython["tool_id"] = "refresh.other";
    const auto otherPath = refreshTools.put("custom/Python/Basic/other.yaml", YAML::Dump(otherPython));
    refreshed.replaceDefinition(parseDefinition(otherPath, false));
    refreshed.setPythonImplementation("refresh.other",
        [](const ToolServices&) { return std::make_unique<PassTool>(); });
    auto replacement = *refreshed.find("refresh.python");
    replacement.displayName = "Targeted replacement";
    refreshed.replaceDefinition(std::move(replacement));
    assert(refreshed.find("refresh.python")->displayName == "Targeted replacement");
    assert(refreshed.availability("refresh.python").state == ToolAvailability::ImplementationUnavailable);
    assert(refreshed.availability("refresh.other").state == ToolAvailability::Available);
    refreshTools.put("builtin/Image/Basic/cpp.yaml", "invalid: [");
    python["display_name"] = "Updated Python";
    refreshTools.put("custom/Python/Basic/python.yaml", YAML::Dump(python));
    refreshed.scanPython(refreshTools.root);
    assert(refreshed.find("refresh.cpp")->displayName == originalCppName);
    assert(refreshed.find("refresh.python")->displayName == "Updated Python");
    assert(refreshed.availability("refresh.python").state == ToolAvailability::ImplementationUnavailable);
    refreshTools.put("custom/Python/Basic/python.yaml", "invalid: [");
    refreshed.scanPython(refreshTools.root);
    assert(refreshed.find("refresh.cpp"));
    assert(!refreshed.find("refresh.python"));
    assert(std::any_of(refreshed.diagnostics().begin(), refreshed.diagnostics().end(),
        [&](const auto& diagnostic) { return diagnostic.path == pythonPath; }));
    refreshTools.put("custom/Python/Basic/python.yaml", YAML::Dump(python));
    refreshed.scanPython(refreshTools.root);
    assert(refreshed.find("refresh.python"));
    std::filesystem::remove(pythonPath);
    refreshed.scanPython(refreshTools.root);
    assert(!refreshed.find("refresh.python"));
    assert(std::any_of(refreshed.diagnostics().begin(), refreshed.diagnostics().end(),
        [&](const auto& diagnostic) { return diagnostic.path == pythonPath; }));

    ToolRegistry examples;
    examples.scan(std::filesystem::u8path(argv[1]));
    assert(examples.diagnostics().empty());
    assert(examples.definitions().size() == 22);
    assert(examples.find("visionrt.image.crop")->outputs[3].type == dense);
    assert(examples.find("custom.image.mean")->kind == ToolKind::PythonFunction);
    assert(examples.availability("custom.image.mean").state == ToolAvailability::ImplementationUnavailable);
}
