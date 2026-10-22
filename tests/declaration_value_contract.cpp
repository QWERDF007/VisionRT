#ifdef NDEBUG
#    undef NDEBUG
#endif

#include "core/ToolDeclarationYaml.h"
#include "core/ToolDefinition.h"
#include "core/ValueYaml.h"

#include <visionrt/contracts/Value.h>

#include <cassert>
#include <iostream>
#include <stdexcept>

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
}

int main()
try
{
    const auto rectType  = decodeTypeSpec(YAML::Load("{type: Rect}"));
    const auto rectValue = decodeValue(YAML::Load("[2, 1.25, -2.5, 3.75, 4.5, 17.5]"), rectType);
    assert(std::get<Roi>(rectValue).kind() == RoiKind::Rectangle);
    assert(equalValues(rectValue, Roi::rectangle(1.25F, -2.5F, 3.75F, 4.5F, 17.5F)));
    assert(typeOf(rectValue) == rectType);
    assert(encodeTypeSpec(rectType)["type"].as<std::string>() == "Rect");
    assert(std::string_view(typeName(Type::Rect)) == "Rect");
    assert(std::string_view(dtypeName(DType::Float64)) == "float64");
    rejects([] { (void)typeName(static_cast<Type>(999)); });
    assert(equalValues(decodeValue(encodeValue(rectValue, rectType), rectType), rectValue));
    validateValue(rectType, Missing{});
    rejects([&] { (void)decodeValue(YAML::Load("[3, 1, 2, 3]"), rectType); });
    rejects([&] { (void)decodeValue(YAML::Load("[[[2, 1, 2, 3, 4, 0]]]"), rectType); });
    rejects([&] { (void)decodeValue(YAML::Load("[2, 0, 0, 0, 4, 0]"), rectType); });
    rejects([&] { validateValue(rectType, RoiBatch{}); });
    rejects([&] { validateValue(rectType, Roi::circle(1, 2, 3)); });

    // Schema validation tests
    ToolDefinition valid;
    valid.schemaVersion = 1;
    valid.id            = "valid.tool";
    valid.displayName   = "Valid Tool";
    valid.version       = "1.0.0";
    valid.kind          = ToolKind::CppFunction;
    valid.registryName  = "valid.tool.cpp";
    valid.inputs.push_back({"in_img", "Input Image", {Type::ImageBatch}});
    valid.inputs.push_back({"threshold",
                            "Threshold",
                            {Type::Int},
                            std::int64_t{5},
                            "number",
                            "基本输入",
                            "",
                            std::int64_t{0},
                            std::int64_t{10},
                            std::int64_t{1},
                            0});
    valid.outputs.push_back({"out_img",
                             "Output Image",
                             {Type::ImageBatch},
                             Missing{},
                             "",
                             "",
                             "",
                             std::nullopt,
                             std::nullopt,
                             std::nullopt,
                             std::nullopt,
                             {},
                             "",
                             "",
                             "",
                             "BGR",
                             "in_img"});
    validateToolDefinition(valid);

    {
        auto           geometryTool = valid;
        PortDefinition input{"rect", "Rectangle", {Type::Rect}};
        input.imageInput = "in_img";
        geometryTool.inputs.push_back(input);
        PortDefinition output{"rect", "Rectangle", {Type::Rect}};
        output.imagePort = "out_img";
        geometryTool.outputs.push_back(output);
        validateToolDefinition(geometryTool);
        geometryTool.inputs.back().imageInput = "threshold";
        rejects([&] { validateToolDefinition(geometryTool); });
    }

    // Negative tests
    // 1. bad schemaVersion
    {
        auto bad          = valid;
        bad.schemaVersion = 2;
        rejects([&] { validateToolDefinition(bad); });
    }
    // 2. empty tool id
    {
        auto bad = valid;
        bad.id   = "";
        rejects([&] { validateToolDefinition(bad); });
    }
    // 3. invalid tool id
    {
        auto bad = valid;
        bad.id   = "bad id with space";
        rejects([&] { validateToolDefinition(bad); });
    }
    // 4. missing registryName on C++ tool
    {
        auto bad         = valid;
        bad.registryName = "";
        rejects([&] { validateToolDefinition(bad); });
    }
    // 5. CppFunction having non-empty init_params
    {
        auto bad = valid;
        bad.initParams.push_back({"param1", "Param 1", {Type::Int}});
        rejects([&] { validateToolDefinition(bad); });
    }
    // 6. duplicate port id
    {
        auto bad = valid;
        bad.inputs.push_back({"in_img", "Duplicate", {Type::Int}});
        rejects([&] { validateToolDefinition(bad); });
    }
    // 7. reserved port prefix __
    {
        auto bad = valid;
        bad.inputs.push_back({"__custom", "Reserved", {Type::Int}});
        rejects([&] { validateToolDefinition(bad); });
    }
    // 8. min > max
    {
        auto bad              = valid;
        bad.inputs[1].minimum = std::int64_t{20};
        bad.inputs[1].maximum = std::int64_t{10};
        rejects([&] { validateToolDefinition(bad); });
    }
    // 9. step <= 0
    {
        auto bad           = valid;
        bad.inputs[1].step = std::int64_t{0};
        rejects([&] { validateToolDefinition(bad); });
    }
    // 10. output with editor
    {
        auto bad              = valid;
        bad.outputs[0].editor = "number";
        rejects([&] { validateToolDefinition(bad); });
    }
    // 10b. default exceeds max
    {
        auto bad                   = valid;
        bad.inputs[1].defaultValue = std::int64_t{999};
        rejects([&] { validateToolDefinition(bad); });
    }
    // 11. input with display
    {
        auto bad                 = valid;
        bad.inputs[0].colorOrder = "BGR";
        rejects([&] { validateToolDefinition(bad); });
    }
    // 12. invalid color order
    {
        auto bad                  = valid;
        bad.outputs[0].colorOrder = "INVALID_COLOR";
        rejects([&] { validateToolDefinition(bad); });
    }
    // 13. image_input reference mismatch
    {
        auto bad = valid;
        bad.inputs.push_back({"rois",
                              "ROIs",
                              {Type::RoiBatch},
                              Missing{},
                              "",
                              "",
                              "",
                              std::nullopt,
                              std::nullopt,
                              std::nullopt,
                              std::nullopt,
                              {},
                              "",
                              "non_existent_img"});
        rejects([&] { validateToolDefinition(bad); });
    }

    const auto pathEditor = parseToolDefinitionYaml(
        "schema_version: 1\ntool_id: test.path\ndisplay_name: Path\nversion: 1.0.0\n"
        "kind: cpp_function\nentry: {registry_name: test.path}\ninit_params: []\n"
        "inputs:\n  - {id: mode, name: Mode, type: String, default: file, options: [file, directory]}\n"
        "  - {id: path, name: Path, type: String, editor: path, path_mode_from: mode}\noutputs: []\n");
    assert(pathEditor.inputs[1].editor == "path");
    assert(pathEditor.inputs[1].pathModeFrom == "mode");
    {
        auto invalid                   = pathEditor;
        invalid.inputs[1].pathModeFrom = "missing";
        rejects([&] { validateToolDefinition(invalid); });
        invalid.inputs[1].pathModeFrom = "path";
        rejects([&] { validateToolDefinition(invalid); });
        invalid.inputs[1].pathModeFrom = "mode";
        invalid.inputs[1].editor       = "file";
        rejects([&] { validateToolDefinition(invalid); });
        invalid                        = pathEditor;
        invalid.inputs[0].type         = {Type::Int};
        invalid.inputs[0].defaultValue = std::int64_t{0};
        invalid.inputs[0].options.clear();
        rejects([&] { validateToolDefinition(invalid); });
        invalid = pathEditor;
        invalid.inputs[1].pathModeFrom.clear();
        rejects([&] { validateToolDefinition(invalid); });
    }
    {
        auto initialization       = pathEditor;
        initialization.kind       = ToolKind::CppClass;
        initialization.initParams = std::move(initialization.inputs);
        initialization.inputs.clear();
        validateToolDefinition(initialization);
    }

    return 0;
}
catch (const std::exception &error)
{
    std::cerr << error.what() << '\n';
    return 1;
}
