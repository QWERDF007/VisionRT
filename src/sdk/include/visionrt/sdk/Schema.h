#pragma once

#include "visionrt/sdk/Tool.h"
#include <filesystem>
#include <optional>
#include <string>
#include <vector>

namespace visionrt {

enum class ToolKind { CppFunction, CppClass, PythonFunction, PythonClass };

struct PortDefinition {
    std::string id;
    std::string name;
    TypeSpec type;
    PortValue defaultValue = Missing{};
    std::string editor;
    std::string tab = "基本输入";
    std::string group;
    std::optional<PortValue> minimum;
    std::optional<PortValue> maximum;
    std::optional<PortValue> step;
    std::optional<int> decimals;
    std::vector<PortValue> options;
    std::string optionsProvider;
    std::string imageInput;
    std::string imagePort;
    std::string colorOrder;
    std::string sameGeometryAsInput;
};

struct ToolDefinition {
    int schemaVersion = 1;
    std::string id;
    std::string displayName;
    std::string version = "1.0.0";
    ToolKind kind = ToolKind::CppFunction;
    std::filesystem::path definitionPath;
    std::string category;
    std::string subcategory;
    bool builtin = false;
    std::string registryName;
    std::filesystem::path script;
    std::string symbol;
    std::vector<PortDefinition> initParams;
    std::vector<PortDefinition> inputs;
    std::vector<PortDefinition> outputs;
};

// SSOT schema validation for tool declarations across VisionRT and VisionTools.
// Throws ContractError if any validation rule is violated.
VT_EXPORT void validateToolDefinition(const ToolDefinition& def);

} // namespace visionrt
