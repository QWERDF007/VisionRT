#ifdef NDEBUG
#    undef NDEBUG
#endif

#include "core/DefinitionJson.h"
#include "core/ToolDefinition.h"
#include "core/ValueJson.h"

#include <visionrt/contracts/Value.h>

#include <algorithm>
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
    const TypeSpec roiType{Type::ROI};
    const auto     rectValue = ValueJson::parse("[[[2, 1.25, -2.5, 3.75, 4.5, 17.5]]]", roiType);
    assert(roiKind(std::get<DataRef<ROI>>(rectValue).get()[0][0]) == RoiKind::Rectangle);
    assert(typeOf(rectValue) == roiType);
    assert(ValueJson::encodeType(roiType)["type"].toString() == "ROI");
    assert(std::string_view(typeName(Type::ROI)) == "ROI");
    assert(std::string_view(dtypeName(DType::Float64)) == "float64");
    rejects([] { (void)typeName(static_cast<Type>(999)); });
    assert(equalValues(ValueJson::decode(ValueJson::encode(rectValue, roiType), roiType), rectValue));
    validateValue(roiType, Missing{});
    rejects([&] { (void)ValueJson::parse("[2, 1, 2, 3, 4, 0]", roiType); });
    rejects([&] { (void)ValueJson::parse("[[[2, 0, 0, 0, 4, 0]]]", roiType); });
    rejects([&] { validateValue(roiType, Array::empty(DType::Float64, 3)); });

    // Schema validation: one token per type, name_en/name_zh identity, qualified relations.
    const auto makePort = [](std::string nameEn, TypeSpec type)
    {
        PortDefinition port;
        port.nameEn       = std::move(nameEn);
        port.nameZh       = port.nameEn;
        port.type         = type;
        port.defaultValue = defaultValue(type);
        return port;
    };

    ToolDefinition valid;
    valid.nameEn  = "valid.tool";
    valid.nameZh  = "Valid Tool";
    valid.version = "1.0.0";
    valid.kind    = ToolKind::Cpp;

    valid.inputs.push_back(makePort("in_img", {Type::Image}));
    auto threshold         = makePort("threshold", {Type::Int});
    threshold.defaultValue = std::int64_t{5};
    threshold.editor       = "number";
    threshold.minimum      = std::int64_t{0};
    threshold.maximum      = std::int64_t{10};
    threshold.step         = std::int64_t{1};
    threshold.decimals     = 0;
    valid.inputs.push_back(threshold);

    auto imageOut         = makePort("out_img", {Type::Image});
    imageOut.colorOrder   = "BGR";
    imageOut.sameGeometryAs = PortRef{PortStage::Inputs, "in_img"};
    valid.outputs.push_back(imageOut);
    validateToolDefinition(valid);

    {
        auto           geometryTool = valid;
        auto           rectInput    = makePort("rect", {Type::ROI});
        rectInput.editor            = "rect";
        rectInput.image             = PortRef{PortStage::Inputs, "in_img"};
        geometryTool.inputs.push_back(rectInput);
        auto rectOutput  = makePort("rect", {Type::ROI});
        rectOutput.image = PortRef{PortStage::Outputs, "out_img"};
        geometryTool.outputs.push_back(rectOutput);
        validateToolDefinition(geometryTool);
        geometryTool.inputs.back().image = PortRef{PortStage::Inputs, "threshold"};
        rejects([&] { validateToolDefinition(geometryTool); });
    }

    // An authored status port is replaced by the canonical host-owned one, never trusted as is.
    {
        auto authored = valid;
        authored.outputs.push_back(makePort("__status_code", {Type::String}));
        normalizeToolDefinition(authored);
        const auto &status = *std::find_if(authored.outputs.begin(), authored.outputs.end(),
                                           [](const auto &port) { return port.nameEn == "__status_code"; });
        assert(status.type == TypeSpec{Type::Int});
        assert(isMissing(status.defaultValue));
    }
    // 1. bad schemaVersion
    {
        auto bad          = valid;
        bad.schemaVersion = 4;
        rejects([&] { validateToolDefinition(bad); });
    }
    // 2. empty tool name
    {
        auto bad   = valid;
        bad.nameEn = "";
        rejects([&] { validateToolDefinition(bad); });
    }
    // 3. invalid tool name
    {
        auto bad   = valid;
        bad.nameEn = "bad id with space";
        rejects([&] { validateToolDefinition(bad); });
    }
    // 4. host tool without a binding
    {
        auto bad = valid;
        bad.kind = ToolKind::Host;
        rejects([&] { validateToolDefinition(bad); });
    }
    // 5. function tool cannot carry init parameters
    {
        auto bad  = valid;
        bad.kind  = ToolKind::PythonFunction;
        bad.entry = PythonEntry{"fixture", "tool.py", "execute"};
        bad.initParams.push_back(makePort("param1", {Type::Int}));
        rejects([&] { validateToolDefinition(bad); });
    }
    // 6. duplicate port name
    {
        auto bad = valid;
        bad.inputs.push_back(makePort("in_img", {Type::Int}));
        rejects([&] { validateToolDefinition(bad); });
    }
    // 7. reserved port prefix __
    {
        auto bad = valid;
        bad.inputs.push_back(makePort("__custom", {Type::Int}));
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
    // 13. relations.image must reference an existing Image port
    {
        auto bad         = valid;
        auto rois        = makePort("rois", {Type::ROI});
        bad.inputs.push_back(rois);
        bad.inputs.back().image = PortRef{PortStage::Inputs, "non_existent_img"};
        rejects([&] { validateToolDefinition(bad); });
    }
    // 14. init and inputs stay distinct namespaces
    {
        auto bad = valid;
        bad.initParams.push_back(makePort("threshold", {Type::Int}));
        rejects([&] { validateToolDefinition(bad); });
    }

    const auto pathEditor = parseToolDefinitionJson(
        R"json({
  "schema_version": 5,
  "name_en": "test.path",
  "name_zh": "Path",
  "version": "1.0.0",
  "implementation": {
    "kind": "cpp"
  },
  "ports": {
    "inputs": [
      {
        "name_en": "mode",
        "name_zh": "Mode",
        "type": "string",
        "default": "file",
        "constraints": {
          "options": [
            "file",
            "directory"
          ]
        }
      },
      {
        "name_en": "path",
        "name_zh": "Path",
        "type": "string",
        "ui": {
          "editor": "path"
        },
        "relations": {
          "path_mode": "inputs.mode"
        }
      }
    ]
  }
})json");
    // The effective contract prepends the host-owned upstream_status input.
    assert(pathEditor.inputs[2].editor == "path");
    assert((pathEditor.inputs[2].pathModeFrom == PortRef{PortStage::Inputs, "mode"}));
    {
        auto invalid                   = pathEditor;
        invalid.inputs[2].pathModeFrom = PortRef{PortStage::Inputs, "missing"};
        rejects([&] { validateToolDefinition(invalid); });
        invalid.inputs[2].pathModeFrom = PortRef{PortStage::Inputs, "path"};
        rejects([&] { validateToolDefinition(invalid); });
        invalid.inputs[2].pathModeFrom = PortRef{PortStage::Inputs, "mode"};
        invalid.inputs[2].editor       = "file";
        rejects([&] { validateToolDefinition(invalid); });
        invalid                        = pathEditor;
        invalid.inputs[1].type         = {Type::Int};
        invalid.inputs[1].defaultValue = std::int64_t{0};
        invalid.inputs[1].options.clear();
        rejects([&] { validateToolDefinition(invalid); });
        invalid = pathEditor;
        invalid.inputs[2].pathModeFrom.reset();
        validateToolDefinition(invalid);
    }
    {
        auto initialization       = pathEditor;
        initialization.initParams = std::move(initialization.inputs);
        initialization.inputs.clear();
        for (auto &port : initialization.initParams)
            if (port.pathModeFrom)
                port.pathModeFrom->stage = PortStage::Init;
        validateToolDefinition(initialization);
    }

    return 0;
}
catch (const std::exception &error)
{
    std::cerr << error.what() << '\n';
    return 1;
}
