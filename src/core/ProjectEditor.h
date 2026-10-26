#pragma once

#include "core/Project.h"

#include <span>
#include <string_view>
#include <utility>

namespace visionrt {

using DependencyEdge = std::pair<NodeId, NodeId>;
// Only effective upstream bindings contribute edges. Invalid active references throw.
std::vector<DependencyEdge> dependencyEdges(const FlowDefinition &flow);
bool                        hasDependencyCycle(const FlowDefinition &flow);

struct InputPort
{
    NodeId      node;
    std::string port;
    bool        operator==(const InputPort &) const = default;
};

struct PortRelation
{
    SourcePort source;
    InputPort target;
    bool operator==(const PortRelation &) const = default;
};

enum class BindingError
{
    None = 0,
    Self,
    OtherFlow,
    SameOrUpperRow,
    MissingPort,
    TypeMismatch,
    Cycle,
};

struct BindingCheck
{
    BindingError reason = BindingError::None;
    std::string  message;
    explicit operator bool() const noexcept { return reason == BindingError::None; }
};

using PositionLookup = std::function<std::optional<GridPoint>(NodeId)>;

BindingCheck validateBinding(const FlowDefinition &flow, NodeId sourceNodeId, const std::string &sourcePort,
                            NodeId targetNodeId, const std::string &targetInputPort,
                            const PositionLookup &positions = {});

// A guard is a Bool control dependency with the same row/ownership rules as data
// edges. It never carries images, arrays or ROI values.
BindingCheck validateGuard(const FlowDefinition &flow, const SourcePort &source, NodeId targetNodeId,
                           const PositionLookup &positions = {});

struct CandidateInput
{
    NodeId      node{};
    std::string port;
    bool        connected = false;
    bool        occupied  = false;
};

const std::optional<SourcePort> &effectiveSource(const InputBinding &binding);
std::vector<CandidateInput> candidateInputsWithState(const FlowDefinition &flow, const SourcePort &source);

struct PortRelations
{
    std::vector<PortRelation> active;
    std::vector<std::string> errors;
};

PortRelations portRelations(const FlowDefinition &flow);

// Immutable node configuration captured at copy time. It holds no snapshot,
// runtime instance, image output or source identity, so later source edits or
// deletion cannot change a paste.
struct CopiedSelection
{
    std::vector<NodeDefinition> nodes;
};

// GUI-thread owned. Rejected commands throw ContractError without changing configuration.
// Successful bool commands return whether persistent configuration actually changed.
class ProjectEditor
{
public:
    explicit ProjectEditor(ProjectDefinition project = {});

    const ProjectDefinition &project() const noexcept
    {
        return project_;
    }

    bool busy() const noexcept
    {
        return busy_;
    }

    void setBusy(bool busy) noexcept
    {
        busy_ = busy;
    }

    void replaceProject(ProjectDefinition project);

    void markSaved() noexcept
    {
        project_.dirty = false;
    }

    bool        renameProject(std::string name);
    std::string addFlow(std::string name = {});
    bool        renameFlow(std::string_view flowId, std::string name);
    bool        setRunSelected(std::string_view flowId, bool selected);
    // Returns the adjacent surviving flow ID for the caller's view selection.
    std::string removeFlow(std::string_view flowId);
    std::string copyFlow(std::string_view flowId, std::string name = {});

    NodeId                   addNode(std::string_view flowId, const ToolDefinition &tool, GridPoint preferred);
    bool                     removeNodes(std::string_view flowId, std::span<const NodeId> nodes);
    bool                     moveNodes(std::string_view flowId, std::span<const NodeId> nodes, GridPoint delta);
    CopiedSelection          copySelection(std::string_view flowId, std::span<const NodeId> nodes) const;
    std::map<NodeId, NodeId> pasteSelection(std::string_view targetFlow, const CopiedSelection &selection,
                                            GridPoint preferredOffset);
    bool                     setAlias(std::string_view flowId, NodeId node, std::string alias);

    bool bindFreeInput(std::string_view flowId, const SourcePort &source, const InputPort &target);
    bool setBinding(std::string_view flowId, NodeId node, std::string_view input, InputBinding binding);
    bool setMode(std::string_view flowId, NodeId node, std::string_view input, BindingMode mode);
    bool setSource(std::string_view flowId, NodeId node, std::string_view input, std::optional<SourcePort> source);
    bool unbind(std::string_view flowId, NodeId node, std::string_view input);
    bool setLiteral(std::string_view flowId, NodeId node, std::string_view input, PortValue value);
    // One transaction for a proposal that touches several literal inputs (ROI plus recommended
    // parameters). Every port is validated before anything is written, inputs currently fed from
    // upstream are refused instead of being silently disconnected, and the whole proposal raises
    // exactly one node execution revision.
    bool setLiteralBindings(std::string_view flowId, NodeId node,
                            std::span<const std::pair<std::string, PortValue>> values);
    bool setInitParam(std::string_view flowId, NodeId node, std::string_view parameter, PortValue value);
    // One transaction: validate the candidate configuration, materialise its
    // contract, verify bindings, then commit exactly one revision.
    bool setExpressionConfig(std::string_view flowId, NodeId node, ExpressionConfig config,
                             const ToolDefinition &tool);
    // One transaction for switching a condition node between its formula and
    // upstream-Bool source. Inactive bindings stay stored for switching back.
    bool setConditionMode(std::string_view flowId, NodeId node, BindingMode mode, const ToolDefinition &tool);
    // The object is the input mode: absent means user input (always execute).
    bool setExecutionGuard(std::string_view flowId, NodeId node, std::optional<ExecutionGuard> guard,
                           const ToolDefinition &tool);
    bool setSelectType(std::string_view flowId, NodeId node, TypeSpec type, const ToolDefinition &tool);

    std::size_t             referenceCount(std::string_view flowId, const SourcePort &source) const;
    std::vector<SourcePort> compatibleSources(std::string_view flowId, NodeId node, std::string_view input) const;
    std::vector<InputPort>  compatibleInputs(std::string_view flowId, const SourcePort &source) const;

private:
    void                  commit(ProjectDefinition project);
    void                  requireIdle() const;
    FlowDefinition       &flow(std::string_view id);
    const FlowDefinition &flow(std::string_view id) const;
    ProjectDefinition     project_;
    bool                  busy_ = false;
};

} // namespace visionrt
