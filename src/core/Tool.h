#pragma once

#include "common/Value.h"
#include <filesystem>
#include <functional>
#include <map>
#include <memory>
#include <optional>
#include <string>
#include <vector>

namespace visionrt {
using Values = std::map<std::string, PortValue, std::less<>>;

struct ToolStatus {
    std::int64_t code = 0;
    std::string message;
    explicit operator bool() const noexcept { return code == 0; }
};
struct ToolResult {
    ToolStatus status;
    Values outputs;
    double waitGilMs = 0;
};
class IToolInstance {
public:
    virtual ~IToolInstance() = default;
    virtual ToolStatus initialize(const Values& parameters) = 0;
    virtual ToolResult execute(const Values& inputs) = 0;
    virtual ToolStatus release() = 0;
};

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

class PathResolver {
public:
    PathResolver() = default;
    explicit PathResolver(std::filesystem::path projectDirectory)
        : directory_(std::move(projectDirectory)) {}
    std::filesystem::path resolve(const std::string& utf8) const;
private:
    std::filesystem::path directory_;
};
class IResultReader;
struct ToolServices {
    PathResolver paths;
    std::shared_ptr<IResultReader> results;
};
using ToolFactory = std::function<std::unique_ptr<IToolInstance>(const ToolServices&)>;
}
