#pragma once

#include "visionrt/sdk/Tool.h"
#include <filesystem>
#include <functional>
#include <map>
#include <memory>
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
    std::string id;
    std::string displayName;
    std::string version;
    ToolKind kind;
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

class IResultReader;
struct PlatformToolServices : ToolServices {
    using ToolServices::ToolServices;
    PlatformToolServices() = default;
    PlatformToolServices(const ToolServices& base) : ToolServices(base) {}
    PlatformToolServices(PathResolver p, std::shared_ptr<IResultReader> r = {})
        : ToolServices{std::move(p)}, results(std::move(r)) {}

    std::shared_ptr<IResultReader> results;
};

using ToolFactory = std::function<std::unique_ptr<IToolInstance>(const PlatformToolServices&)>;
}
