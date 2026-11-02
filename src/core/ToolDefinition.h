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

// Which port list a reference points at. A declaration never guesses: an author writes
// "inputs.images", "outputs.rect" or "init.source_flow_id", and the stage is resolved once here.
enum class PortStage
{
    Init,
    Inputs,
    Outputs
};

struct PortRef
{
    PortStage   stage = PortStage::Inputs;
    std::string port;
    bool        operator==(const PortRef &) const = default;
};

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
    std::string                                    helperNameEn;
    std::map<std::string, PortRef, std::less<>>     inputs;    // helper input port -> tool port
    std::map<std::string, PortRef, std::less<>>     proposals; // helper output port -> tool input port
    std::vector<RoiKind>                           roiKinds;
    bool operator==(const InteractionMapping &) const = default;
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

// A dynamic output type names three initialisation parameters by reference.
struct PortTypeReference {
    PortRef flowParameter;
    PortRef nodeParameter;
    PortRef portParameter;
    bool operator==(const PortTypeReference&) const = default;
};

struct PortDefinition {
    std::string nameEn;
    std::string nameZh;
    TypeSpec    type;
    // The declaration's explicit default, or the type's empty value materialised during
    // normalisation. A host-owned status port and a type_from output keep it Missing.
    PortValue defaultValue = Missing{};
    std::string editor;
    std::string tab = "基本输入";
    std::string group;
    std::optional<PortValue> step;
    std::optional<int>       decimals;
    std::string              optionsProvider;
    std::optional<PortValue> minimum;
    std::optional<PortValue> maximum;
    std::vector<PortValue>   options;
    std::optional<PortRef>   image;
    std::optional<PortRef>   alignment;
    std::optional<PortRef>   pathModeFrom;
    std::optional<PortRef>   sameGeometryAs;
    std::string              colorOrder;
    std::optional<PortTypeReference> typeFrom;
};

struct ToolDefinition {
    int                                schemaVersion = 5;
    std::string                        nameEn;
    std::string                        nameZh;
    std::string                        version = "1.0.0";
    ToolKind                           kind = ToolKind::Cpp;
    NodeModel                          nodeModel  = NodeModel::Fixed;
    ToolVisibility                     visibility = ToolVisibility::Normal;
    std::optional<InteractionMapping>  interaction;
    std::filesystem::path              definitionPath;
    std::vector<std::string>           categories;
    ToolSource                         source;
    std::string                        hostBinding;
    PythonEntry                        entry;
    std::vector<std::filesystem::path> resources;
    std::vector<PortDefinition>        initParams;
    std::vector<PortDefinition>        inputs;
    std::vector<PortDefinition>        outputs;

    bool derivesPorts() const noexcept
    {
        return nodeModel != NodeModel::Fixed;
    }

    std::filesystem::path pythonPackage() const;
    std::filesystem::path pythonScript() const;
};

// "inputs.images" / "outputs.rect" / "init.source_flow_id". The stage prefix is mandatory: a
// reference that could mean two things is an author error, never a guess.
PortRef     parsePortRef(std::string_view text);
std::string formatPortRef(const PortRef& reference);

// The one Chinese breadcrumb of a tool ("预处理/图像处理"), used by the tool tree and node badges.
std::string categoryPath(const ToolDefinition& def);

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

// The one place that turns a declaration into an effective contract: host-owned ports are
// materialised, every value-carrying port receives its explicit default or the type's empty
// value, and the result is validated. Idempotent; conflicting author declarations fail loudly.
void normalizeToolDefinition(ToolDefinition &def);

// Validating entry used after normalisation; kept separate for read-only checks.
void validateToolDefinition(const ToolDefinition& def);

// An independent copy of a port's default value, safe to hand to a node that may write it.
PortValue portDefault(const PortDefinition& port);

} // namespace visionrt
