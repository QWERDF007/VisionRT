#pragma once

#include "visionrt/contracts/Tool.h"
#include <filesystem>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace visionrt {

enum class ToolKind { CppFunction, CppClass, PythonFunction, PythonClass };

struct PortTypeReference {
    std::string flowParameter;
    std::string nodeParameter;
    std::string portParameter;
    bool operator==(const PortTypeReference&) const = default;
};

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
    std::optional<PortTypeReference> typeFrom;
    std::string pathModeFrom;
    bool required = true;
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
    std::vector<std::filesystem::path> resources;
    std::vector<PortDefinition> initParams;
    std::vector<PortDefinition> inputs;
    std::vector<PortDefinition> outputs;
};

// True only for String ports whose editor selects a resource path (file, directory, path).
bool isPathPort(const PortDefinition& port);

// Fixed parameter pages shared by the validator and the workbench.
std::span<const std::string_view> toolTabs() noexcept;

void validateToolDefinition(const ToolDefinition& def);

} // namespace visionrt
