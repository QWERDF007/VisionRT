#pragma once

#include "visionrt/contracts/Tool.h"
#include <array>
#include <cstdint>
#include <filesystem>
#include <map>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace visionrt {

enum class ToolKind { Cpp, PythonFunction, PythonClass, Host };

// Which port list a reference points at. The stage is stored, never inferred from a spelling.
enum class PortStage
{
    Init,
    Inputs,
    Outputs
};

// A port reference is a stage plus a number: the same number may exist once per direction.
struct PortRef
{
    PortStage stage = PortStage::Inputs;
    PortId    id    = 0;
    bool      operator==(const PortRef &) const = default;
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

// Helper port number -> the tool port it feeds (inputs) or proposes into (proposals).
struct InteractionMapping
{
    std::string                 helperNameEn;
    std::map<PortId, PortRef>   inputs;
    std::map<PortId, PortRef>   proposals;
    std::vector<RoiKind>        roiKinds;
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

// One declared parameter. `id` is its identity inside the tool and stage; the vector order is only
// the display order, so moving a row never renumbers it. `nameEn` is an author note for readers and
// migration diffs — nothing looks a port up by it.
struct PortDefinition {
    PortId      id = 0;
    std::string nameZh;
    TypeSpec    type;
    // The declaration's explicit default, or the type's empty value materialised during
    // normalisation. A host-owned status port and a type_from output keep it Missing.
    PortValue defaultValue = Missing{};
    std::string nameEn;
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
    // Author's explicit choice to offer this Image port in the workbench image selector. Absent in
    // the declaration means false: an image port never joins the selector without being named.
    bool                     imageVisible = false;
    std::optional<PortTypeReference> typeFrom;
};

struct ToolDefinition {
    int                                schemaVersion = 6;
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

    // High-water counters: the next number each direction will hand out. They are persisted, never
    // recomputed from the live ids, so deleting the highest port cannot recycle its number.
    std::uint64_t nextInitPortId   = 0;
    std::uint64_t nextInputPortId  = 1;
    std::uint64_t nextOutputPortId = 2;

    bool derivesPorts() const noexcept
    {
        return nodeModel != NodeModel::Fixed;
    }

    std::uint64_t nextPortId(PortStage stage) const noexcept;
    void          setNextPortId(PortStage stage, std::uint64_t value);

    std::filesystem::path pythonPackage() const;
    std::filesystem::path pythonScript() const;
};

// "inputs.7" — diagnostics only. The number is the identity; the text never becomes one.
PortRef     parsePortRef(std::string_view text);
std::string formatPortRef(const PortRef& reference);

// The one Chinese breadcrumb of a tool ("预处理/图像处理"), used by the tool tree and node badges.
std::string categoryPath(const ToolDefinition& def);

// True only for String ports whose editor selects a resource path (file, directory, path).
bool isPathPort(const PortDefinition& port);

// A stable ASCII identifier: letter or underscore first, then letters, digits, '_', '.' or '-'.
// Tool ids name declaration files, so anything that could leave a directory is rejected here.
void validateIdentifier(const std::string& value, bool isPort);

// Execution conditions belong to the logical node models only. A future model
// never inherits the privilege by accident.
bool supportsExecutionGuard(NodeModel model) noexcept;

// Canonical host-provided ports. Input 0 carries the upstream status; outputs 0 and 1 carry the
// status envelope. They are materialised by normalisation and never written by tool declarations.
bool isReservedPort(PortStage stage, PortId id) noexcept;

// The label, type and default of one host-owned port. Normalisation materialises a copy into a
// declaration and the editor projects the same copy as a fixed row, so the two cannot drift apart.
PortDefinition hostPort(PortStage stage, PortId id);

// The inverse of normalisation for host ports: leaves exactly what the declaration authored. The
// editor draft is a declaration, so it round-trips through this on load and never holds a second
// copy of a fixed row the port model already projects.
void stripHostPorts(ToolDefinition &def);

// Input 0 is reserved for every model, but only a Fixed tool is handed the upstream status.
bool isInjectedInput(NodeModel model, PortId id) noexcept;

// Initialisation entries derived from the node model rather than edited directly. They are still
// handed to the tool through nodeParameters(), but never become parameter rows.
inline constexpr PortId kVariablesInitPort  = 0;
inline constexpr PortId kProgramInitPort    = 1;
inline constexpr PortId kConditionModePort  = 2;
bool                    isDerivedInitParam(NodeModel model, PortId id) noexcept;

// Fixed parameter pages shared by the validator and the workbench.
std::span<const std::string_view> toolTabs() noexcept;

// The one place that turns a declaration into an effective contract: host-owned ports are
// materialised at their reserved numbers, every value-carrying port receives its explicit default
// or the type's empty value, high-water counters are raised above the live ids, and the result is
// validated. Idempotent; conflicting author declarations fail loudly.
void normalizeToolDefinition(ToolDefinition &def);

// Validating entry used after normalisation; kept separate for read-only checks.
void validateToolDefinition(const ToolDefinition& def);

// Allocates the next number of one direction. Throws once the 32-bit space is exhausted; a number
// is never reused, so a deleted port leaves a permanent hole.
PortId allocatePortId(ToolDefinition &def, PortStage stage);

// Editor kinds that can actually present this type. The declaration validator accepts any known
// editor whose shape fits; this is the subset the editor offers and reconciliation keeps, so a
// value field is never declared with a control that cannot render it.
std::vector<std::string_view> editorChoices(const TypeSpec &type);

// Retypes one draft port: every value, constraint, relation and reference the new type can still
// hold is kept, and the rest is cleared. The returned sentences name each cleared field, so an
// editor can show the exact loss before committing. A derived-type port, or one another declaration
// uses as a type source, is refused instead of being half-retyped.
std::vector<std::string> retypePort(ToolDefinition &def, PortStage stage, PortId id, TypeSpec newType);

// Removes one business port together with everything that named it: the relations other ports point
// at it with, and the interaction entries that offered it. The returned sentences name each dropped
// reference. A host-owned port, or one a type_from output derives from, is refused instead of being
// removed from under a declaration that still needs it.
std::vector<std::string> erasePort(ToolDefinition &def, PortStage stage, PortId id);

// An independent copy of a port's default value, safe to hand to a node that may write it.
PortValue portDefault(const PortDefinition& port);

} // namespace visionrt
