#include <visionrt/sdk/Schema.h>
#include <visionrt/sdk/SchemaYaml.h>
#include <iostream>
#include <cassert>

int main() {
    using namespace visionrt;
    const std::string yaml = R"(
schema_version: 1
tool_id: external.test
display_name: External Test
version: v1
kind: cpp_function
entry: {registry_name: external.pass}
init_params: []
inputs:
  - {id: threshold, name: Threshold, type: Int, default: 5, min: 0, max: 10, step: 1}
outputs:
  - {id: result, name: Result, type: Int}
)";
    const auto def = parseToolDefinitionYaml(yaml);
    assert(def.id == "external.test");
    assert(def.inputs.size() == 1);
    assert(def.outputs.size() == 1);
    validateToolDefinition(def);
    std::cout << "[SDK Consumer] Successfully validated external tool definition: " << def.id << std::endl;
    return 0;
}
