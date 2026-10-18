#include <visionrt/sdk/Schema.h>
#include <visionrt/sdk/SchemaYaml.h>
#include <iostream>
#include <fstream>
#include <filesystem>
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

    // 11. Positive Module Index: Unified module_index.yaml contract
    {
        const std::string validIndexYaml = R"(
variant: cpu
modules:
  preprocess:
    library: bin/visiontools_preprocess.dll
tools:
  visionrt.image.read:
    module: preprocess
    definition: assets/tools/read.yaml
runtime_files:
  - bin/opencv_world480.dll
)";
        ModuleIndexData idx = parseModuleIndexYaml(validIndexYaml);
        REQUIRE(idx.variant == "cpu");
        REQUIRE(idx.modules.size() == 1);
        REQUIRE(idx.modules.at("preprocess").library == "bin/visiontools_preprocess.dll");
        REQUIRE(idx.tools.size() == 1);
        REQUIRE(idx.tools.at("visionrt.image.read").module == "preprocess");
        REQUIRE(idx.tools.at("visionrt.image.read").definition == "assets/tools/read.yaml");
        REQUIRE(idx.runtimeFiles.size() == 1);
        REQUIRE(idx.runtimeFiles[0] == "bin/opencv_world480.dll");
    }

    // 12. Negative Module Index: Duplicate key detection
    {
        const std::string dupKeyIndex = R"(
variant: cpu
variant: gpu
modules:
  mod:
    library: bin/mod.dll
tools:
  t1:
    module: mod
    definition: tools/t1.yaml
)";
        REQUIRE_THROWS(parseModuleIndexYaml(dupKeyIndex));
    }

    // 13. Negative Module Index: Dangling module reference
    {
        const std::string danglingIndex = R"(
variant: cpu
modules:
  mod1:
    library: bin/mod1.dll
tools:
  t1:
    module: nonexistent_mod
    definition: tools/t1.yaml
)";
        REQUIRE_THROWS(parseModuleIndexYaml(danglingIndex));
    }

    // 14. Negative Module Index: Path escaping root
    {
        const std::string escapingIndex = R"(
variant: cpu
modules:
  mod1:
    library: ../escaping.dll
tools:
  t1:
    module: mod1
    definition: tools/t1.yaml
)";
        REQUIRE_THROWS(parseModuleIndexYaml(escapingIndex));
    }

    // 15. Negative Module Index: Unknown root field
    {
        const std::string unknownRootIndex = R"(
variant: cpu
unknown_key: true
modules:
  mod1:
    library: bin/mod1.dll
tools:
  t1:
    module: mod1
    definition: tools/t1.yaml
)";
        REQUIRE_THROWS(parseModuleIndexYaml(unknownRootIndex));
    }

    // 16. Package validation (RT-S01 validateToolsPackage)
    {
        const auto tempRoot = std::filesystem::temp_directory_path() / "sdk_consumer_pkg_test";
        std::error_code ec;
        std::filesystem::remove_all(tempRoot, ec);
        std::filesystem::create_directories(tempRoot / "bin", ec);
        std::filesystem::create_directories(tempRoot / "assets" / "tools", ec);

        // Create library and runtime file
        {
            std::ofstream out(tempRoot / "bin" / "mod1.dll");
            out << "dummy dll";
        }
        {
            std::ofstream out(tempRoot / "bin" / "runtime_dep.dll");
            out << "dummy runtime dep";
        }

        // Create tool definition
        const std::string validToolDef = R"(
schema_version: 1
tool_id: test.pkg.tool
display_name: Package Test Tool
version: 1.0.0
kind: cpp_function
entry:
  registry_name: test.pkg.tool
init_params: []
inputs: []
outputs: []
)";
        {
            std::ofstream out(tempRoot / "assets" / "tools" / "tool.yaml");
            out << validToolDef;
        }

        // Create valid module_index.yaml
        const std::string validPkgIndex = R"(
variant: cpu
modules:
  mod1:
    library: bin/mod1.dll
tools:
  test.pkg.tool:
    module: mod1
    definition: assets/tools/tool.yaml
runtime_files:
  - bin/runtime_dep.dll
)";
        {
            std::ofstream out(tempRoot / "module_index.yaml");
            out << validPkgIndex;
        }

        // Baseline: valid package passes
        validateToolsPackage(tempRoot);

        // Negative: Missing variant
        {
            std::ofstream out(tempRoot / "module_index.yaml");
            out << R"(
modules:
  mod1:
    library: bin/mod1.dll
tools:
  test.pkg.tool:
    module: mod1
    definition: assets/tools/tool.yaml
)";
        }
        REQUIRE_THROWS(validateToolsPackage(tempRoot));

        // Negative: Missing definition file
        {
            std::ofstream out(tempRoot / "module_index.yaml");
            out << R"(
variant: cpu
modules:
  mod1:
    library: bin/mod1.dll
tools:
  test.pkg.tool:
    module: mod1
    definition: assets/tools/nonexistent.yaml
)";
        }
        REQUIRE_THROWS(validateToolsPackage(tempRoot));

        // Negative: Missing module library file
        {
            std::ofstream out(tempRoot / "module_index.yaml");
            out << R"(
variant: cpu
modules:
  mod1:
    library: bin/nonexistent.dll
tools:
  test.pkg.tool:
    module: mod1
    definition: assets/tools/tool.yaml
)";
        }
        REQUIRE_THROWS(validateToolsPackage(tempRoot));

        // Negative: Missing runtime file
        {
            std::ofstream out(tempRoot / "module_index.yaml");
            out << R"(
variant: cpu
modules:
  mod1:
    library: bin/mod1.dll
tools:
  test.pkg.tool:
    module: mod1
    definition: assets/tools/tool.yaml
runtime_files:
  - bin/nonexistent_runtime.dll
)";
        }
        REQUIRE_THROWS(validateToolsPackage(tempRoot));

        // Negative: Registry name mismatch
        {
            std::ofstream out(tempRoot / "module_index.yaml");
            out << R"(
variant: cpu
modules:
  mod1:
    library: bin/mod1.dll
tools:
  other.pkg.tool:
    module: mod1
    definition: assets/tools/tool.yaml
)";
        }
        REQUIRE_THROWS(validateToolsPackage(tempRoot));

        // Negative: Dangling module (unreferenced)
        {
            std::ofstream out(tempRoot / "module_index.yaml");
            out << R"(
variant: cpu
modules:
  mod1:
    library: bin/mod1.dll
  unused_mod:
    library: bin/mod1.dll
tools:
  test.pkg.tool:
    module: mod1
    definition: assets/tools/tool.yaml
)";
        }
        REQUIRE_THROWS(validateToolsPackage(tempRoot));

        std::filesystem::remove_all(tempRoot, ec);
    }

    std::cout << "[SDK Consumer] All positive and negative validation corpus checks passed successfully!" << std::endl;
    return 0;
}
