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

    // Only call after a successful ordinary save commit; frozen configuration may be saved busy.
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

    bool setBinding(std::string_view flowId, NodeId node, std::string_view input, InputBinding binding);
    bool setMode(std::string_view flowId, NodeId node, std::string_view input, BindingMode mode);
    bool setSource(std::string_view flowId, NodeId node, std::string_view input, std::optional<SourcePort> source);
    bool unbind(std::string_view flowId, NodeId node, std::string_view input);
    bool setLiteral(std::string_view flowId, NodeId node, std::string_view input, PortValue value);
    bool setInitParam(std::string_view flowId, NodeId node, std::string_view parameter, PortValue value);

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
