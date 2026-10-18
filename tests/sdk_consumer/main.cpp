#include <visionrt/sdk/Schema.h>
#include <visionrt/sdk/SchemaYaml.h>
#include <iostream>
#include <cmath>
#include <limits>
#include <string>

#define REQUIRE(expr) \
    do { \
        if (!(expr)) { \
            std::cerr << "[SDK Consumer ERROR] Requirement failed at " << __FILE__ << ":" << __LINE__ << " -> " << #expr << std::endl; \
            return 1; \
        } \
    } while (0)

#define REQUIRE_THROWS(code_block) \
    do { \
        bool threw = false; \
        try { \
            code_block; \
        } catch (const visionrt::ContractError&) { \
            threw = true; \
        } catch (const std::exception& e) { \
            threw = true; \
        } \
        if (!threw) { \
            std::cerr << "[SDK Consumer ERROR] Expected exception at " << __FILE__ << ":" << __LINE__ << " -> " << #code_block << std::endl; \
            return 1; \
        } \
    } while (0)

int main() {
    using namespace visionrt;

    std::cout << "[SDK Consumer] Starting comprehensive validation corpus checks..." << std::endl;

    // 1. Valid C++ Function Tool
    {
        const std::string validYaml = R"(
schema_version: 1
tool_id: external.test.pass
display_name: 外部有效工具
version: 1.0.0
kind: cpp_function
entry:
  registry_name: external.test.pass
init_params: []
inputs:
  - id: threshold
    name: 阈值
    type: Int
    default: 5
    min: 0
    max: 10
    step: 1
outputs:
  - id: result
    name: 结果
    type: Int
)";
        const auto def = parseToolDefinitionYaml(validYaml);
        REQUIRE(def.id == "external.test.pass");
        REQUIRE(def.inputs.size() == 1);
        REQUIRE(def.outputs.size() == 3); // result + __status_code + __status_message
        validateToolDefinition(def);
    }

    // 2. Valid Python Tool
    {
        const std::string validPythonYaml = R"(
schema_version: 1
tool_id: external.python.valid
display_name: 外部有效Python工具
version: 1.0.0
kind: python_function
entry:
  file: tool.py
  symbol: execute
init_params: []
inputs:
  - id: scale
    name: 缩放比
    type: Float
    default: 1.5
    min: 0.1
    max: 10.0
    step: 0.1
outputs:
  - id: output_val
    name: 输出
    type: Float
)";
        const auto def = parseToolDefinitionYaml(validPythonYaml);
        REQUIRE(def.id == "external.python.valid");
        validateToolDefinition(def);
    }

    // 3. Negative Corpus: Invalid kind
    {
        const std::string badKindYaml = R"(
schema_version: 1
tool_id: external.bad.kind
display_name: 非法Kind工具
version: 1.0.0
kind: rust_plugin
entry: {registry_name: external.bad.kind}
init_params: []
inputs: []
outputs: []
)";
        REQUIRE_THROWS(parseToolDefinitionYaml(badKindYaml));
    }

    // 4. Negative Corpus: Empty C++ registry_name / Empty Python entry
    {
        const std::string emptyEntryCpp = R"(
schema_version: 1
tool_id: external.empty.cpp
display_name: 空Entry C++
version: 1.0.0
kind: cpp_function
entry: {registry_name: ""}
init_params: []
inputs: []
outputs: []
)";
        REQUIRE_THROWS(parseToolDefinitionYaml(emptyEntryCpp));

        const std::string emptyEntryPy = R"(
schema_version: 1
tool_id: external.empty.python
display_name: 空Entry Python
version: 1.0.0
kind: python_function
entry: {file: "", symbol: ""}
init_params: []
inputs: []
outputs: []
)";
        REQUIRE_THROWS(parseToolDefinitionYaml(emptyEntryPy));
    }

    // 5. Negative Corpus: Wrong type in numeric range (min="abc" on Int port)
    {
        const std::string wrongTypeRange = R"(
schema_version: 1
tool_id: external.wrong.range
display_name: 错型范围
version: 1.0.0
kind: cpp_function
entry: {registry_name: external.wrong.range}
init_params: []
inputs:
  - id: val
    name: 值
    type: Int
    min: "abc"
    max: 10
outputs: []
)";
        REQUIRE_THROWS(parseToolDefinitionYaml(wrongTypeRange));
    }

    // 6. Negative Corpus: min > max
    {
        const std::string minExceedsMax = R"(
schema_version: 1
tool_id: external.min.exceeds.max
display_name: 倒置范围
version: 1.0.0
kind: cpp_function
entry: {registry_name: external.min.exceeds.max}
init_params: []
inputs:
  - id: val
    name: 值
    type: Int
    min: 10
    max: 5
outputs: []
)";
        REQUIRE_THROWS(parseToolDefinitionYaml(minExceedsMax));
    }

    // 7. Negative Corpus: Float NaN and Inf in default/min/max
    {
        const std::string nanYaml = R"(
schema_version: 1
tool_id: external.nan.val
display_name: NaN工具
version: 1.0.0
kind: cpp_function
entry: {registry_name: external.nan.val}
init_params: []
inputs:
  - id: val
    name: 值
    type: Float
    default: .nan
outputs: []
)";
        REQUIRE_THROWS(parseToolDefinitionYaml(nanYaml));

        const std::string infYaml = R"(
schema_version: 1
tool_id: external.inf.val
display_name: Inf工具
version: 1.0.0
kind: cpp_function
entry: {registry_name: external.inf.val}
init_params: []
inputs:
  - id: val
    name: 值
    type: Float
    max: .inf
outputs: []
)";
        REQUIRE_THROWS(parseToolDefinitionYaml(infYaml));
    }

    // 8. Negative Corpus: Default not in declared options
    {
        const std::string optionMismatch = R"(
schema_version: 1
tool_id: external.option.mismatch
display_name: 选项不匹配
version: 1.0.0
kind: cpp_function
entry: {registry_name: external.option.mismatch}
init_params: []
inputs:
  - id: opt
    name: 选项
    type: Int
    options: [1, 2, 3]
    default: 99
outputs: []
)";
        REQUIRE_THROWS(parseToolDefinitionYaml(optionMismatch));
    }

    // 9. Negative Corpus: Unknown field in YAML
    {
        const std::string unknownFieldYaml = R"(
schema_version: 1
tool_id: external.unknown.field
display_name: 未知字段
version: 1.0.0
kind: cpp_function
entry: {registry_name: external.unknown.field}
illegal_field: 42
init_params: []
inputs: []
outputs: []
)";
        REQUIRE_THROWS(parseToolDefinitionYaml(unknownFieldYaml));
    }

    // 10. Direct C++ ToolDefinition semantic validation
    {
        ToolDefinition directDef;
        directDef.schemaVersion = 1;
        directDef.id = "external.direct.cpp";
        directDef.displayName = "直接C++定义";
        directDef.version = "1.0.0";
        directDef.kind = ToolKind::CppFunction;
        directDef.registryName = "external.direct.cpp";

        PortDefinition inPort;
        inPort.id = "ratio";
        inPort.name = "比率";
        inPort.type = {Type::Float};
        inPort.defaultValue = PortValue{std::numeric_limits<double>::quiet_NaN()};
        directDef.inputs.push_back(inPort);

        REQUIRE_THROWS(validateToolDefinition(directDef));
    }

    std::cout << "[SDK Consumer] All positive and negative validation corpus checks passed successfully!" << std::endl;
    return 0;
}
