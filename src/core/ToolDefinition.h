#pragma once

#include "visionrt/contracts/Tool.h"
#include <filesystem>
#include <map>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace visionrt {

enum class ToolKind { Cpp, PythonFunction, PythonClass, Host };

// How the host edits and materialises a node contract. Only fixed nodes use the
// declaration ports directly; the other models derive ports from node config.
enum class NodeModel
{
    Fixed,
    Expression,
    Condition,
    Select
};

enum class ToolVisibility
{
    Normal,
    Assist
};

struct InteractionMapping
{
    std::string                        helperToolId;
    std::map<std::string, std::string, std::less<>> inputMap;
    std::map<std::string, std::string, std::less<>> proposalMap;
    std::vector<RoiKind>               roiKinds;
    bool                               operator==(const InteractionMapping &) const = default;
};

enum class ToolOrigin
{
    Builtin,
    Custom
};

struct ToolRoots
{
    std::filesystem::path declarations;
    std::filesystem::path python;
    bool                  operator==(const ToolRoots &) const = default;
};

struct ToolSource
{
    ToolOrigin origin = ToolOrigin::Custom;
    ToolRoots  roots;
    bool       operator==(const ToolSource &) const = default;
};

struct PythonEntry
{
    std::filesystem::path package;
    std::filesystem::path file;
    std::string           symbol;
    bool                  operator==(const PythonEntry &) const = default;
};

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
    std::string alignmentInput;
    std::optional<PortTypeReference> typeFrom;
    std::string pathModeFrom;
    bool required = true;
};

struct ToolDefinition {
    int                                schemaVersion = 4;
    std::string id;
    std::string displayName;
    std::string version = "1.0.0";
    ToolKind kind = ToolKind::Cpp;
    NodeModel        nodeModel  = NodeModel::Fixed;
    ToolVisibility   visibility = ToolVisibility::Normal;
    std::optional<InteractionMapping> interaction;
    std::filesystem::path definitionPath;
    std::string category;
    std::string subcategory;
    ToolSource                         source;
    std::string hostBinding;
    PythonEntry                        entry;
    std::vector<std::filesystem::path> resources;
    std::vector<PortDefinition> initParams;
    std::vector<PortDefinition> inputs;
    std::vector<PortDefinition> outputs;

    bool derivesPorts() const noexcept
    {
        return nodeModel != NodeModel::Fixed;
    }

    std::filesystem::path pythonPackage() const;
    std::filesystem::path pythonScript() const;
};

// True only for String ports whose editor selects a resource path (file, directory, path).
bool isPathPort(const PortDefinition& port);

// Execution conditions belong to the logical node models only. A future model
// never inherits the privilege by accident.
bool supportsExecutionGuard(NodeModel model) noexcept;

// Canonical host-provided ports live in the contract header so a tool and the host can never
// disagree about their spelling. Materialised into every effective contract and never written by
// tool declaration files.

bool isHostOwnedPort(std::string_view id) noexcept;

// Initialisation entries derived from the node model rather than edited directly. They are still
// handed to the tool through nodeParameters(), but never become parameter rows.
bool isDerivedInitParam(std::string_view id) noexcept;

// Fixed parameter pages shared by the validator and the workbench.
std::span<const std::string_view> toolTabs() noexcept;

// Materialises host-owned contract requirements (currently upstream_status) and
// then validates. Idempotent; conflicting author declarations fail loudly.
void normalizeToolDefinition(ToolDefinition &def);

// Validating entry used after normalisation; kept separate for read-only checks.
void validateToolDefinition(const ToolDefinition& def);

} // namespace visionrt
