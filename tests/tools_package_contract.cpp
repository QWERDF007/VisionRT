#ifdef NDEBUG
#undef NDEBUG
#endif

#include "core/ToolPackage.h"

#include <cassert>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <stdexcept>
#include <string>

using namespace visionrt;

namespace {

const char* kToolAlpha = R"(schema_version: 1
tool_id: test.pkg.alpha
display_name: 包内工具甲
category: Explicit Category
subcategory: Explicit Group
resources: [settings.json]
version: 1.2.0
kind: cpp_function
entry:
  registry_name: test.pkg.alpha
init_params: []
inputs:
  - id: x
    name: 输入
    type: Int
outputs:
  - id: y
    name: 输出
    type: Int
)";

const char* kToolBravo = R"(schema_version: 1
tool_id: test.pkg.bravo
display_name: 包内工具乙
version: 1.0.0
kind: cpp_class
entry:
  registry_name: test.pkg.bravo.registry
init_params: []
inputs: []
outputs:
  - id: count
    name: 计数
    type: Int
)";

const char* kToolGamma = R"(schema_version: 1
tool_id: test.pkg.gamma
display_name: 自定义工具
version: 1.0.0
kind: cpp_function
entry:
  registry_name: test.pkg.gamma.registry
init_params: []
inputs: []
outputs:
  - id: value
    name: 值
    type: Float
)";

const char* kValidIndex = R"(schema_version: 2
variant: cpu
modules:
  mod_a:
    library: bin/mod_a.dll
tools:
  test.pkg.bravo:
    module: mod_a
    definition: assets/tools/builtin/类别/组二/bravo.yaml
  test.pkg.alpha:
    module: mod_a
    definition: assets/tools/builtin/类别/组一/alpha.yaml
  test.pkg.gamma:
    module: mod_a
    definition: assets/tools/custom/Python/示例/gamma.yaml
runtime_files:
  - bin/runtime_dep.dat
)";

const auto kAlphaPath = std::filesystem::path(u8"assets/tools/builtin/类别/组一/alpha.yaml");
const auto kBravoPath = std::filesystem::path(u8"assets/tools/builtin/类别/组二/bravo.yaml");
const auto kGammaPath = std::filesystem::path(u8"assets/tools/custom/Python/示例/gamma.yaml");

void writeFile(const std::filesystem::path& path, const std::string& content) {
    std::filesystem::create_directories(path.parent_path());
    std::ofstream out(path, std::ios::binary | std::ios::trunc);
    assert(out);
    out << content;
    assert(out.good());
}

std::filesystem::path makeRoot(const std::filesystem::path& base, const std::filesystem::path& name) {
    const auto root = base / name;
    std::error_code error;
    std::filesystem::remove_all(root, error);
    std::filesystem::create_directories(root / "bin");
    std::filesystem::create_directories(root / "assets" / "tools");
    writeFile(root / "bin" / "mod_a.dll", "dummy module");
    writeFile(root / "bin" / "runtime_dep.dat", "dummy runtime");
    writeFile(root / kAlphaPath, kToolAlpha);
    writeFile(root / kAlphaPath.parent_path() / "settings.json", "{}");
    writeFile(root / kBravoPath, kToolBravo);
    writeFile(root / kGammaPath, kToolGamma);
    writeFile(root / "module_index.yaml", kValidIndex);
    return root;
}

template <typename Callable>
bool failsWith(Callable&& callable, const std::string& fragment) {
    try {
        callable();
    } catch (const std::exception& error) {
        const std::string message = error.what();
        if (message.find(fragment) != std::string::npos) return true;
        std::cerr << "unexpected diagnostic: " << message << "\n(expected fragment: " << fragment << ")\n";
        return false;
    }
    std::cerr << "expected failure containing: " << fragment << "\n";
    return false;
}

void readValidPackage(const std::filesystem::path& root) {
    const auto package = readToolsPackage(root);
    assert(package.root == std::filesystem::absolute(root).lexically_normal());
    assert(package.index.schemaVersion == 2);
    assert(package.index.variant == "cpu");
    assert(package.index.tools.size() == 3);
    assert(package.index.modules.size() == 1);
    assert(package.tools.size() == 3);

    // Document order of the index is not trusted; tools are deterministic by declaration path.
    assert(package.tools[0].toolId == "test.pkg.alpha");
    assert(package.tools[1].toolId == "test.pkg.bravo");
    assert(package.tools[2].toolId == "test.pkg.gamma");
    assert(package.tools[0].registryName == "test.pkg.alpha");
    assert(package.tools[1].registryName == "test.pkg.bravo.registry");
    assert(package.tools[2].registryName == "test.pkg.gamma.registry");
    assert(package.tools[0].definition.displayName == "包内工具甲");
    assert(package.tools[0].definition.version == "1.2.0");
    assert(package.tools[0].definition.category == "Explicit Category");
    assert(package.tools[0].definition.subcategory == "Explicit Group");
    const auto relocated = parseToolDefinitionYaml(kToolAlpha, package.root / "unrelated" / "renamed.yaml", true);
    assert(relocated.category == package.tools[0].definition.category);
    assert(relocated.subcategory == package.tools[0].definition.subcategory);
    assert(package.tools[2].definition.category.empty());
    assert(package.tools[2].definition.subcategory.empty());
    assert(package.tools[0].moduleLibrary == package.root / "bin" / "mod_a.dll");
    assert(std::filesystem::is_regular_file(package.tools[0].moduleLibrary));
    assert(package.tools[0].definition.definitionPath ==
           package.root / std::filesystem::path(u8"assets/tools/builtin/类别/组一/alpha.yaml"));
    assert(package.tools[1].definition.kind == ToolKind::CppClass);

    assert(package.tools[0].definition.builtin);
    assert(package.tools[1].definition.builtin);
    assert(package.tools[2].definition.builtin);
    assert(package.tools[2].definition.definitionPath == package.root / kGammaPath);

    // Validation entry delegates to the same reading semantics.
    validateToolsPackage(root);
}

void readInvalidPackages(const std::filesystem::path& base) {
    const auto root = makeRoot(base, "negatives");

    assert(failsWith([&] { validateToolsPackage(root / "missing_root"); },
                     "not a valid directory"));

    auto noIndex = makeRoot(base, "no_index");
    std::filesystem::remove(noIndex / "module_index.yaml");
    assert(failsWith([&] { readToolsPackage(noIndex); }, "Missing module_index.yaml in package"));

    auto brokenIndex = makeRoot(base, "broken_index");
    writeFile(brokenIndex / "module_index.yaml", "modules: [ unclosed\n");
    assert(failsWith([&] { readToolsPackage(brokenIndex); }, "YAML"));

    auto missingSchemaVer = makeRoot(base, "missing_schema_ver");
    writeFile(missingSchemaVer / "module_index.yaml",
        "variant: cpu\nmodules:\n  mod_a:\n    library: bin/mod_a.dll\ntools:\n"
        "  test.pkg.alpha:\n    module: mod_a\n    definition: assets/tools/builtin/类别/组一/alpha.yaml\n");
    assert(failsWith([&] { readToolsPackage(missingSchemaVer); }, "Missing required field 'schema_version'"));

    auto wrongSchemaVer = makeRoot(base, "wrong_schema_ver");
    writeFile(wrongSchemaVer / "module_index.yaml",
        "schema_version: 1\nvariant: cpu\nmodules:\n  mod_a:\n    library: bin/mod_a.dll\ntools:\n"
        "  test.pkg.alpha:\n    module: mod_a\n    definition: assets/tools/builtin/类别/组一/alpha.yaml\n");
    assert(failsWith([&] { readToolsPackage(wrongSchemaVer); }, "Unsupported schema_version"));

    auto dangling = makeRoot(base, "dangling");
    writeFile(dangling / "module_index.yaml",
        "schema_version: 2\nvariant: cpu\nmodules:\n  mod_a:\n    library: bin/mod_a.dll\ntools:\n"
        "  test.pkg.alpha:\n    module: mod_missing\n    definition: assets/tools/builtin/类别/组一/alpha.yaml\n");
    assert(failsWith([&] { readToolsPackage(dangling); }, "references undeclared module"));

    auto unreferenced = makeRoot(base, "unreferenced");
    writeFile(unreferenced / "module_index.yaml",
        "schema_version: 2\nvariant: cpu\nmodules:\n  mod_a:\n    library: bin/mod_a.dll\n  mod_b:\n    library: bin/mod_b.dll\n"
        "tools:\n  test.pkg.alpha:\n    module: mod_a\n"
        "    definition: assets/tools/builtin/类别/组一/alpha.yaml\n");
    assert(failsWith([&] { readToolsPackage(unreferenced); }, "not referenced by any tool"));

    auto unknownField = makeRoot(base, "unknown_field");
    writeFile(unknownField / "module_index.yaml",
        "schema_version: 2\nvariant: cpu\nunknown_key: bad\nmodules:\n  mod_a:\n    library: bin/mod_a.dll\ntools:\n"
        "  test.pkg.alpha:\n    module: mod_a\n    definition: assets/tools/builtin/类别/组一/alpha.yaml\n");
    assert(failsWith([&] { readToolsPackage(unknownField); }, "unknown field"));

    auto missingLibrary = makeRoot(base, "missing_library");
    std::filesystem::remove(missingLibrary / "bin" / "mod_a.dll");
    assert(failsWith([&] { readToolsPackage(missingLibrary); }, "library file does not exist"));

    auto missingDefinition = makeRoot(base, "missing_definition");
    std::filesystem::remove(missingDefinition / kAlphaPath);
    assert(failsWith([&] { readToolsPackage(missingDefinition); }, "definition file does not exist"));

    auto toolIdMismatch = makeRoot(base, "tool_id_mismatch");
    writeFile(toolIdMismatch / kAlphaPath,
        R"(schema_version: 1
tool_id: test.pkg.different
display_name: 包内工具甲
version: 1.2.0
kind: cpp_function
entry:
  registry_name: test.pkg.alpha
init_params: []
inputs: []
outputs: []
)");
    assert(failsWith([&] { readToolsPackage(toolIdMismatch); }, "does not match index tool_id"));

    auto bothModuleAndHostBinding = makeRoot(base, "both_module_host");
    writeFile(bothModuleAndHostBinding / "module_index.yaml",
        "schema_version: 2\nvariant: cpu\nmodules:\n  mod_a:\n    library: bin/mod_a.dll\ntools:\n"
        "  test.pkg.alpha:\n    module: mod_a\n    host_binding: platform_recv\n    definition: assets/tools/builtin/类别/组一/alpha.yaml\n");
    assert(failsWith([&] { readToolsPackage(bothModuleAndHostBinding); }, "cannot specify both"));

    auto cppMissingModule = makeRoot(base, "cpp_missing_module");
    writeFile(cppMissingModule / "module_index.yaml",
        "schema_version: 2\nvariant: cpu\ntools:\n"
        "  test.pkg.alpha:\n    definition: assets/tools/builtin/类别/组一/alpha.yaml\n");
    assert(failsWith([&] { readToolsPackage(cppMissingModule); }, "must reference a module"));

    auto missingRuntime = makeRoot(base, "missing_runtime");
    std::filesystem::remove(missingRuntime / "bin" / "runtime_dep.dat");
    assert(failsWith([&] { readToolsPackage(missingRuntime); }, "runtime_files entry does not exist"));

    auto duplicateIdentity = makeRoot(base, "duplicate_identity");
    writeFile(duplicateIdentity / "module_index.yaml",
        "schema_version: 2\nvariant: cpu\nmodules:\n  mod_a:\n    library: bin/mod_a.dll\ntools:\n"
        "  test.pkg.alpha:\n    module: mod_a\n    definition: assets/tools/builtin/类别/组一/alpha.yaml\n"
        "  test.pkg.bravo:\n    module: mod_a\n    definition: assets/tools/builtin/类别/组二/bravo.yaml\n");
    writeFile(duplicateIdentity / kBravoPath,
        R"(schema_version: 1
tool_id: test.pkg.alpha
display_name: 重复身份工具
version: 1.0.0
kind: cpp_class
entry:
  registry_name: test.pkg.bravo.registry
init_params: []
inputs: []
outputs: []
)");
    assert(failsWith([&] { readToolsPackage(duplicateIdentity); }, "does not match index tool_id"));

    auto duplicateKey = makeRoot(base, "duplicate_key");
    writeFile(duplicateKey / "module_index.yaml",
        "schema_version: 2\nvariant: cpu\nmodules:\n  mod_a:\n    library: bin/mod_a.dll\ntools:\n"
        "  test.pkg.alpha:\n    module: mod_a\n    definition: assets/tools/builtin/类别/组一/alpha.yaml\n"
        "  test.pkg.alpha:\n    module: mod_a\n    definition: assets/tools/builtin/类别/组二/bravo.yaml\n");
    assert(failsWith([&] { readToolsPackage(duplicateKey); }, "Duplicate key"));

    // A normalized index reference must not climb out of the package root.
    auto escapingDefinition = makeRoot(base, "escaping_definition");
    writeFile(escapingDefinition / "module_index.yaml",
        "schema_version: 2\nvariant: cpu\nmodules:\n  mod_a:\n    library: bin/mod_a.dll\ntools:\n"
        "  test.pkg.alpha:\n    module: mod_a\n    definition: assets/../../outside.yaml\n");
    assert(failsWith([&] { readToolsPackage(escapingDefinition); }, "escapes root"));

    auto escapingLibrary = makeRoot(base, "escaping_library");
    writeFile(escapingLibrary / "module_index.yaml",
        "schema_version: 2\nvariant: cpu\nmodules:\n  mod_a:\n    library: bin/../../../outside.dll\ntools:\n"
        "  test.pkg.alpha:\n    module: mod_a\n    definition: assets/tools/builtin/类别/组一/alpha.yaml\n");
    assert(failsWith([&] { readToolsPackage(escapingLibrary); }, "escapes root"));

    // A declaration must not point at a script outside its own package.
    auto escapingScript = makeRoot(base, "escaping_script");
    writeFile(escapingScript / "module_index.yaml",
        "schema_version: 2\nvariant: cpu\ntools:\n"
        "  test.pkg.py:\n    definition: assets/tools/custom/Python/示例/escape.yaml\n");
    writeFile(escapingScript / std::filesystem::path(u8"assets/tools/custom/Python/示例/escape.yaml"),
        R"(schema_version: 1
tool_id: test.pkg.py
display_name: 越界脚本
version: 1.0.0
kind: python_class
entry:
  file: ../../../../../outside.py
  symbol: ToolInstance
init_params: []
inputs: []
outputs: []
)");
    assert(failsWith([&] { readToolsPackage(escapingScript); }, "must not escape its declaration directory"));
}

} // namespace

void sourceDerivedOutputsHaveNoConcreteDeclaration(const std::filesystem::path& root) {
    const std::string declaration = R"(schema_version: 1
tool_id: test.receive
display_name: 接收
version: 1.0.0
kind: cpp_class
entry: {registry_name: test.receive}
init_params:
- {id: flow, name: 流程, type: String, default: ''}
- {id: node, name: 节点, type: String, default: ''}
- {id: port, name: 端口, type: String, default: ''}
inputs: []
outputs:
- id: value
  name: 数据
  type_from: {flow: flow, node: node, port: port}
)";
    const auto parsed = parseToolDefinitionYaml(declaration);
    const auto &reference = parsed.outputs.back().typeFrom;
    assert(reference && reference->flowParameter == "flow" && reference->nodeParameter == "node"
        && reference->portParameter == "port");
    auto mixed = declaration;
    mixed += "  type: Int\n";
    assert(failsWith([&] { parseToolDefinitionYaml(mixed); }, "cannot be combined"));
    auto missing = declaration;
    const auto position = missing.find("port: port");
    missing.replace(position, std::string("port: port").size(), "port: missing");
    assert(failsWith([&] { parseToolDefinitionYaml(missing); }, "String initialization parameter"));
    auto incorrect = parsed;
    incorrect.outputs.back().typeFrom->nodeParameter = "value";
    assert(failsWith([&] { validateToolDefinition(incorrect); }, "String initialization parameter"));
    incorrect = parsed;
    incorrect.outputs.back().colorOrder = "BGR";
    assert(failsWith([&] { validateToolDefinition(incorrect); }, "display attributes"));
    writeFile(root / "receive.yaml", declaration);
    writeFile(root / "module_index.yaml",
        "schema_version: 2\nvariant: reference\nmodules: {}\ntools:\n"
        "  test.receive:\n    definition: receive.yaml\n    host_binding: visionrt.receiver\n");
    assert(readToolsPackage(root).tools.front().definition.outputs.back().typeFrom.has_value());
    writeFile(root / "bin/vst_test.dll", "fixture");
    writeFile(root / "module_index.yaml",
        "schema_version: 2\nvariant: reference\nmodules:\n  native:\n    library: bin/vst_test.dll\ntools:\n"
        "  test.receive:\n    definition: receive.yaml\n    module: native\n");
    assert(failsWith([&] { readToolsPackage(root); }, "require host_binding"));
}

int main() {
    const auto unique = std::to_string(static_cast<long long>(
        std::chrono::steady_clock::now().time_since_epoch().count()));
    const auto base = std::filesystem::temp_directory_path() / ("vrt_sdk_package_" + unique);
    std::error_code error;
    std::filesystem::remove_all(base, error);
    std::filesystem::create_directories(base);

    try {
        sourceDerivedOutputsHaveNoConcreteDeclaration(base / "derived-types");
        // Chinese characters and spaces in the package path are ordinary paths.
        const auto unicodeRoot = makeRoot(base, std::filesystem::path(u8"测试 目录/工具 包 1.0"));
        readValidPackage(unicodeRoot);
        readInvalidPackages(base);
        const auto missingResource = makeRoot(base, "missing-resource");
        std::filesystem::remove(missingResource / kAlphaPath.parent_path() / "settings.json");
        assert(failsWith([&] { readToolsPackage(missingResource); }, "resource file does not exist"));
        std::filesystem::remove_all(base, error);
        std::cout << "sdk package contract passed\n";
        return 0;
    } catch (const std::exception& failure) {
        std::cerr << "SDK package contract failed: " << failure.what() << '\n';
        std::filesystem::remove_all(base, error);
        return 1;
    }
}
