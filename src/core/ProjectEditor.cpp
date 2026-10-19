#include "core/ProjectEditor.h"

#include <algorithm>
#include <limits>
#include <set>
#include <unordered_map>
#include <utility>

namespace visionrt {
namespace {
constexpr auto maximumCounter = std::numeric_limits<std::uint64_t>::max();

void requireIncrement(std::uint64_t value)
{
    if (value == maximumCounter)
        throw ContractError("Configuration counter exhausted");
}

void requireEditable(const NodeDefinition &node)
{
    if (node.unresolved.IsDefined() && !node.unresolved.IsNull())
        throw ContractError("Missing tool definition: parameters are read-only");
}

const NodeDefinition &findNode(const FlowDefinition &flow, NodeId id)
{
    const auto found
        = std::find_if(flow.nodes.begin(), flow.nodes.end(), [id](const auto &node) { return node.id == id; });
    if (found == flow.nodes.end())
        throw ContractError("Node is not in this flow");
    return *found;
}

NodeDefinition &findNode(FlowDefinition &flow, NodeId id)
{
    return const_cast<NodeDefinition &>(findNode(std::as_const(flow), id));
}

const TypeSpec &signature(const TypeMap &types, std::string_view port)
{
    const auto found = types.find(port);
    if (found == types.end())
        throw ContractError("Port has no type signature");
    return found->second;
}

const InputBinding &inputBinding(const NodeDefinition &node, std::string_view input)
{
    const auto found = node.inputs.find(input);
    if (found == node.inputs.end())
        throw ContractError("Input does not exist");
    return found->second;
}

void validateLiteral(const TypeSpec &type, const PortValue &value)
{
    validateValue(type, value);
    if (const auto *images = std::get_if<ImageBatch>(&value); images && !images->images().empty())
        throw ContractError("Image literals may only be Missing or an empty batch");
}

const std::optional<SourcePort> &effectiveSource(const InputBinding &binding)
{
    static const std::optional<SourcePort> none;
    return binding.mode == BindingMode::Upstream ? binding.source : none;
}

bool sameEffectiveValue(const InputBinding &left, const InputBinding &right)
{
    if (effectiveSource(left) || effectiveSource(right))
        return effectiveSource(left) == effectiveSource(right);
    if (left.mode == BindingMode::Literal && right.mode == BindingMode::Literal)
        return equalValues(left.literal, right.literal);
    return (left.mode == BindingMode::Upstream || isMissing(left.literal))
        && (right.mode == BindingMode::Upstream || isMissing(right.literal));
}

std::vector<DependencyEdge> deriveEdges(const FlowDefinition &flow, NodeId replacedNode = 0,
                                        std::string_view replacedInput = {}, const InputBinding *replacement = nullptr)
{
    std::unordered_map<NodeId, const NodeDefinition *> nodes;
    nodes.reserve(flow.nodes.size());
    for (const auto &node : flow.nodes)
    {
        if (!nodes.emplace(node.id, &node).second)
            throw ContractError("Duplicate node ID");
    }
    std::vector<DependencyEdge> edges;
    for (const auto &target : flow.nodes)
    {
        for (const auto &[input, stored] : target.inputs)
        {
            const auto &binding
                = replacement && target.id == replacedNode && input == replacedInput ? *replacement : stored;
            if (!effectiveSource(binding))
                continue;
            const auto &source = *binding.source;
            const auto  found  = nodes.find(source.node);
            if (found == nodes.end())
                throw ContractError("Upstream node is not in this flow");
            if (!compatible(signature(found->second->signatures.outputs, source.port),
                            signature(target.signatures.inputs, input)))
                throw ContractError("Incompatible upstream port signature");
            edges.emplace_back(source.node, target.id);
        }
    }
    std::sort(edges.begin(), edges.end());
    edges.erase(std::unique(edges.begin(), edges.end()), edges.end());
    return edges;
}

bool containsCycle(const std::vector<DependencyEdge> &edges)
{
    struct Links
    {
        std::size_t         incoming = 0;
        std::vector<NodeId> outgoing;
    };

    std::unordered_map<NodeId, Links> links;
    for (const auto &[source, target] : edges)
    {
        links[source].outgoing.push_back(target);
        ++links[target].incoming;
    }
    std::vector<NodeId> ready;
    ready.reserve(links.size());
    for (const auto &[id, item] : links)
        if (!item.incoming)
            ready.push_back(id);
    for (std::size_t index = 0; index < ready.size(); ++index)
        for (auto target : links.at(ready[index]).outgoing)
            if (--links.at(target).incoming == 0)
                ready.push_back(target);
    return ready.size() != links.size();
}

GridPoint translated(GridPoint point, std::int64_t dx, std::int64_t dy)
{
    const auto x = static_cast<std::int64_t>(point.x) + dx;
    const auto y = static_cast<std::int64_t>(point.y) + dy;
    if (x < std::numeric_limits<int>::min() || x > std::numeric_limits<int>::max()
        || y < std::numeric_limits<int>::min() || y > std::numeric_limits<int>::max())
        throw ContractError("Grid coordinate overflow");
    return {static_cast<int>(x), static_cast<int>(y)};
}

using Cell = std::pair<int, int>;

// Manhattan rings, top to bottom and left before right: stable nearest placement.
std::pair<std::int64_t, std::int64_t> nearestTranslation(std::span<const GridPoint> points,
                                                         const FlowDefinition &destination, GridPoint preferred)
{
    std::set<Cell> occupied;
    for (const auto &node : destination.nodes) occupied.emplace(node.position.x, node.position.y);
    std::set<Cell> unique;
    std::int64_t   minX = std::numeric_limits<int>::min(), maxX = std::numeric_limits<int>::max();
    std::int64_t   minY = minX, maxY = maxX;
    for (auto point : points)
    {
        if (!unique.emplace(point.x, point.y).second)
            throw ContractError("Copied nodes overlap; repair their layout first");
        minX = std::max(minX, static_cast<std::int64_t>(std::numeric_limits<int>::min()) - point.x);
        maxX = std::min(maxX, static_cast<std::int64_t>(std::numeric_limits<int>::max()) - point.x);
        minY = std::max(minY, static_cast<std::int64_t>(std::numeric_limits<int>::min()) - point.y);
        maxY = std::min(maxY, static_cast<std::int64_t>(std::numeric_limits<int>::max()) - point.y);
    }
    const auto centerX = std::clamp<std::int64_t>(preferred.x, minX, maxX);
    const auto centerY = std::clamp<std::int64_t>(preferred.y, minY, maxY);
    const auto limit   = std::max(centerX - minX, maxX - centerX) + std::max(centerY - minY, maxY - centerY);
    for (std::int64_t radius = 0; radius <= limit; ++radius)
    {
        const auto lowY  = std::max(-radius, minY - centerY);
        const auto highY = std::min(radius, maxY - centerY);
        for (auto dy = lowY; dy <= highY; ++dy)
        {
            const auto dx = radius - (dy < 0 ? -dy : dy);
            for (int side = 0; side < (dx == 0 ? 1 : 2); ++side)
            {
                const auto x = centerX + (side == 0 ? -dx : dx);
                const auto y = centerY + dy;
                if (x < minX || x > maxX)
                    continue;
                bool free = true;
                for (auto point : points)
                {
                    const auto moved = translated(point, x, y);
                    if (occupied.contains({moved.x, moved.y}))
                    {
                        free = false;
                        break;
                    }
                }
                if (free)
                    return {x, y};
            }
        }
    }
    throw ContractError("No free grid placement exists");
}

std::vector<const NodeDefinition *> selection(const FlowDefinition &source, std::span<const NodeId> ids)
{
    std::set<NodeId>                    wanted(ids.begin(), ids.end());
    std::vector<const NodeDefinition *> result;
    result.reserve(wanted.size());
    for (const auto &node : source.nodes)
        if (wanted.erase(node.id))
            result.push_back(&node);
    if (!wanted.empty())
        throw ContractError("Selected node is not in this flow");
    return result;
}

struct CopiedNodes
{
    std::vector<NodeDefinition> nodes;
    std::map<NodeId, NodeId>    mapping;
};

CopiedNodes duplicateNodes(std::span<const NodeDefinition *const> selected, NodeId next, std::int64_t dx,
                           std::int64_t dy)
{
    if (selected.size() > maximumCounter - next)
        throw ContractError("Node IDs exhausted");
    CopiedNodes result;
    result.nodes.reserve(selected.size());
    for (const auto *node : selected) result.mapping.emplace(node->id, next++);
    for (const auto *original : selected)
    {
        auto node              = *original;
        node.id                = result.mapping.at(original->id);
        node.position          = translated(node.position, dx, dy);
        node.executionRevision = 0;
        for (auto &[port, binding] : node.inputs)
        {
            if (!binding.source)
                continue;
            const auto mapped = result.mapping.find(binding.source->node);
            if (mapped == result.mapping.end())
                binding.source.reset();
            else
                binding.source->node = mapped->second;
        }
        result.nodes.push_back(std::move(node));
    }
    return result;
}

std::string flowName(const ProjectDefinition &project, std::string requested, const std::string &base,
                     std::string_view exclude = {})
{
    const auto exists = [&](std::string_view name)
    {
        return std::any_of(project.flows.begin(), project.flows.end(),
                           [&](const auto &flow) { return flow.id != exclude && flow.name == name; });
    };
    if (!requested.empty())
    {
        validateUtf8(requested);
        if (exists(requested))
            throw ContractError("Flow name must be unique");
        return requested;
    }
    auto name = base;
    for (std::uint64_t suffix = 2; exists(name); ++suffix)
    {
        requireIncrement(suffix);
        name = base + " (" + std::to_string(suffix) + ")";
    }
    return name;
}

std::pair<std::string, std::uint64_t> nextFlowIdentity(const ProjectDefinition &project)
{
    auto next = project.nextFlowId;
    for (;;)
    {
        requireIncrement(next);
        auto id = "flow_" + std::to_string(next);
        ++next;
        if (std::none_of(project.flows.begin(), project.flows.end(), [&](const auto &item) { return item.id == id; }))
            return {std::move(id), next};
    }
}

struct ReferencesToClear
{
    std::vector<InputBinding *>   bindings;
    std::vector<NodeDefinition *> executions;
    std::vector<FlowDefinition *> structures;

    void validate() const
    {
        for (const auto *node : executions) requireIncrement(node->executionRevision);
        for (const auto *flow : structures) requireIncrement(flow->structureRevision);
    }

    void apply() const
    {
        for (auto *binding : bindings) binding->source.reset();
        for (auto *node : executions) ++node->executionRevision;
        for (auto *flow : structures) ++flow->structureRevision;
    }
};

ReferencesToClear referencesTo(ProjectDefinition &project, const std::set<NodeId> &removed)
{
    ReferencesToClear result;
    for (auto &flow : project.flows)
    {
        bool structureChanged = false;
        for (auto &node : flow.nodes)
        {
            if (removed.contains(node.id))
                continue;
            bool executionChanged = false;
            for (auto &[port, binding] : node.inputs)
            {
                if (!binding.source || !removed.contains(binding.source->node))
                    continue;
                result.bindings.push_back(&binding);
                executionChanged |= binding.mode == BindingMode::Upstream;
            }
            if (executionChanged)
            {
                result.executions.push_back(&node);
                structureChanged = true;
            }
        }
        if (structureChanged)
            result.structures.push_back(&flow);
    }
    return result;
}
} // namespace

std::vector<DependencyEdge> dependencyEdges(const FlowDefinition &flow)
{
    return deriveEdges(flow);
}

bool hasDependencyCycle(const FlowDefinition &flow)
{
    return containsCycle(dependencyEdges(flow));
}

std::string NodeDefinition::title() const
{
    return (alias.empty() ? toolNameSnapshot : alias) + "-" + std::to_string(id);
}

ProjectEditor::ProjectEditor(ProjectDefinition project)
    : project_(std::move(project))
{
}

void ProjectEditor::requireIdle() const
{
    if (busy_)
        throw ContractError("Project configuration is locked while busy");
}

const FlowDefinition &ProjectEditor::flow(std::string_view id) const
{
    const auto found
        = std::find_if(project_.flows.begin(), project_.flows.end(), [id](const auto &item) { return item.id == id; });
    if (found == project_.flows.end())
        throw ContractError("Flow does not exist");
    return *found;
}

FlowDefinition &ProjectEditor::flow(std::string_view id)
{
    return const_cast<FlowDefinition &>(std::as_const(*this).flow(id));
}

void ProjectEditor::replaceProject(ProjectDefinition project)
{
    requireIdle();
    if (project.flows.empty())
        throw ContractError("A project needs at least one flow");
    project_ = std::move(project);
}

NodeId ProjectEditor::addNode(std::string_view flowId, const ToolDefinition &tool, GridPoint preferred)
{
    requireIdle();
    auto &target = flow(flowId);
    requireIncrement(project_.nextNodeId);
    requireIncrement(target.structureRevision);
    if (tool.id.empty())
        throw ContractError("Tool ID is empty");
    NodeDefinition node;
    node.id               = project_.nextNodeId;
    node.toolId           = tool.id;
    node.toolNameSnapshot = tool.displayName;
    for (const auto &parameter : tool.initParams)
    {
        validateLiteral(parameter.type, parameter.defaultValue);
        if (!node.signatures.initParams.emplace(parameter.id, parameter.type).second)
            throw ContractError("Duplicate initialization parameter");
        node.initParams.emplace(parameter.id, parameter.defaultValue);
    }
    for (const auto &input : tool.inputs)
    {
        validateLiteral(input.type, input.defaultValue);
        if (node.signatures.initParams.contains(input.id)
            || !node.signatures.inputs.emplace(input.id, input.type).second)
            throw ContractError("Duplicate input parameter");
        node.inputs.emplace(input.id, InputBinding{BindingMode::Literal, input.defaultValue, {}});
    }
    for (const auto &output : tool.outputs)
    {
        validateTypeSpec(output.type);
        if (!node.signatures.outputs.emplace(output.id, output.type).second)
            throw ContractError("Duplicate output port");
    }
    const GridPoint origin{};
    const auto [x, y] = nearestTranslation(std::span(&origin, 1), target, preferred);
    node.position     = translated(origin, x, y);
    target.nodes.push_back(std::move(node));
    ++target.structureRevision;
    project_.dirty = true;
    return project_.nextNodeId++;
}

bool ProjectEditor::setBinding(std::string_view flowId, NodeId id, std::string_view input, InputBinding binding)
{
    requireIdle();
    auto &target = flow(flowId);
    auto &node   = findNode(target, id);
    requireEditable(node);
    const auto &current = inputBinding(node, input);
    const auto &type    = signature(node.signatures.inputs, input);
    if (binding.mode != BindingMode::Literal && binding.mode != BindingMode::Upstream)
        throw ContractError("Invalid input mode");
    validateLiteral(type, binding.literal);
    if (binding.source
        && !compatible(signature(findNode(target, binding.source->node).signatures.outputs, binding.source->port),
                       type))
        throw ContractError("Incompatible upstream port signature");
    if (current.mode == binding.mode && current.source == binding.source
        && equalValues(current.literal, binding.literal))
        return false;
    const bool structureChanged = effectiveSource(current) != effectiveSource(binding);
    const bool executionChanged = !sameEffectiveValue(current, binding);
    if (structureChanged && effectiveSource(binding) && containsCycle(deriveEdges(target, id, input, &binding)))
        throw ContractError("Binding would create a dependency cycle");
    if (structureChanged)
        requireIncrement(target.structureRevision);
    if (executionChanged)
        requireIncrement(node.executionRevision);
    node.inputs.find(input)->second = std::move(binding);
    if (structureChanged)
        ++target.structureRevision;
    if (executionChanged)
        ++node.executionRevision;
    project_.dirty = true;
    return true;
}

bool ProjectEditor::setMode(std::string_view flowId, NodeId id, std::string_view input, BindingMode mode)
{
    requireIdle();
    auto binding = inputBinding(findNode(flow(flowId), id), input);
    binding.mode = mode;
    return setBinding(flowId, id, input, std::move(binding));
}

bool ProjectEditor::setSource(std::string_view flowId, NodeId id, std::string_view input,
                              std::optional<SourcePort> source)
{
    requireIdle();
    auto binding   = inputBinding(findNode(flow(flowId), id), input);
    binding.source = std::move(source);
    return setBinding(flowId, id, input, std::move(binding));
}

bool ProjectEditor::unbind(std::string_view flowId, NodeId id, std::string_view input)
{
    return setSource(flowId, id, input, {});
}

bool ProjectEditor::setLiteral(std::string_view flowId, NodeId id, std::string_view input, PortValue value)
{
    requireIdle();
    auto binding    = inputBinding(findNode(flow(flowId), id), input);
    binding.literal = std::move(value);
    return setBinding(flowId, id, input, std::move(binding));
}

bool ProjectEditor::setInitParam(std::string_view flowId, NodeId id, std::string_view parameter, PortValue value)
{
    requireIdle();
    auto &node = findNode(flow(flowId), id);
    requireEditable(node);
    const auto found = node.initParams.find(parameter);
    if (found == node.initParams.end())
        throw ContractError("Initialization parameter does not exist");
    validateLiteral(signature(node.signatures.initParams, parameter), value);
    if (equalValues(found->second, value))
        return false;
    requireIncrement(node.executionRevision);
    found->second = std::move(value);
    ++node.executionRevision;
    project_.dirty = true;
    return true;
}

std::size_t ProjectEditor::referenceCount(std::string_view flowId, const SourcePort &source) const
{
    const auto &target = flow(flowId);
    signature(findNode(target, source.node).signatures.outputs, source.port);
    std::size_t result = 0;
    for (const auto &node : target.nodes)
        for (const auto &[id, binding] : node.inputs)
            if (effectiveSource(binding) == source)
                ++result;
    return result;
}

std::vector<SourcePort> ProjectEditor::compatibleSources(std::string_view flowId, NodeId id,
                                                         std::string_view input) const
{
    const auto &target = flow(flowId);
    const auto &node   = findNode(target, id);
    requireEditable(node);
    const auto &type      = signature(node.signatures.inputs, input);
    auto        candidate = inputBinding(node, input);
    candidate.mode        = BindingMode::Upstream;
    std::vector<SourcePort> result;
    for (const auto &source : target.nodes)
    {
        if (source.id == id || (source.unresolved.IsDefined() && !source.unresolved.IsNull()))
            continue;
        for (const auto &[port, output] : source.signatures.outputs)
        {
            if (!compatible(output, type))
                continue;
            candidate.source = SourcePort{source.id, port};
            if (!containsCycle(deriveEdges(target, id, input, &candidate)))
                result.push_back(*candidate.source);
        }
    }
    return result;
}

std::vector<InputPort> ProjectEditor::compatibleInputs(std::string_view flowId, const SourcePort &source) const
{
    const auto &target     = flow(flowId);
    const auto &sourceNode = findNode(target, source.node);
    requireEditable(sourceNode);
    const auto            &type = signature(sourceNode.signatures.outputs, source.port);
    std::vector<InputPort> result;
    for (const auto &node : target.nodes)
    {
        if (node.id == source.node || (node.unresolved.IsDefined() && !node.unresolved.IsNull()))
            continue;
        for (const auto &[input, binding] : node.inputs)
        {
            if (binding.source || !compatible(type, signature(node.signatures.inputs, input)))
                continue;
            auto candidate   = binding;
            candidate.mode   = BindingMode::Upstream;
            candidate.source = source;
            if (!containsCycle(deriveEdges(target, node.id, input, &candidate)))
                result.push_back({node.id, input});
        }
    }
    return result;
}

bool ProjectEditor::renameProject(std::string name)
{
    requireIdle();
    validateUtf8(name);
    if (name.empty())
        throw ContractError("Project name is empty");
    if (project_.name == name)
        return false;
    project_.name  = std::move(name);
    project_.dirty = true;
    return true;
}

std::string ProjectEditor::addFlow(std::string name)
{
    requireIdle();
    auto [id, next] = nextFlowIdentity(project_);
    FlowDefinition candidate;
    candidate.id   = id;
    candidate.name = flowName(project_, std::move(name), "流程" + std::to_string(next - 1));
    project_.flows.push_back(std::move(candidate));
    project_.nextFlowId = next;
    project_.dirty      = true;
    return id;
}

bool ProjectEditor::renameFlow(std::string_view flowId, std::string name)
{
    requireIdle();
    auto &target = flow(flowId);
    if (name.empty())
        throw ContractError("Flow name is empty");
    name = flowName(project_, std::move(name), {}, flowId);
    if (target.name == name)
        return false;
    target.name    = std::move(name);
    project_.dirty = true;
    return true;
}

bool ProjectEditor::setRunSelected(std::string_view flowId, bool selected)
{
    requireIdle();
    auto &target = flow(flowId);
    if (target.runSelected == selected)
        return false;
    target.runSelected = selected;
    project_.dirty     = true;
    return true;
}

std::string ProjectEditor::removeFlow(std::string_view flowId)
{
    requireIdle();
    auto &target = flow(flowId);
    if (project_.flows.size() == 1)
        throw ContractError("Cannot delete the last flow");
    const auto       index    = static_cast<std::size_t>(&target - project_.flows.data());
    auto             adjacent = project_.flows[index + 1 < project_.flows.size() ? index + 1 : index - 1].id;
    std::set<NodeId> removed;
    for (const auto &node : target.nodes) removed.insert(node.id);
    const auto references = referencesTo(project_, removed);
    references.validate();
    references.apply();
    project_.flows.erase(project_.flows.begin() + static_cast<std::ptrdiff_t>(index));
    project_.dirty = true;
    return adjacent;
}

std::string ProjectEditor::copyFlow(std::string_view flowId, std::string name)
{
    requireIdle();
    const auto &source = flow(flowId);
    auto [id, next]    = nextFlowIdentity(project_);
    FlowDefinition candidate;
    candidate.id   = id;
    candidate.name = flowName(project_, std::move(name), source.name + " 副本");
    std::vector<const NodeDefinition *> selected;
    std::vector<GridPoint>              points;
    selected.reserve(source.nodes.size());
    points.reserve(source.nodes.size());
    for (const auto &node : source.nodes)
    {
        selected.push_back(&node);
        points.push_back(node.position);
    }
    // New flows must not introduce imported layout collisions.
    nearestTranslation(points, candidate, {});
    auto copied      = duplicateNodes(selected, project_.nextNodeId, 0, 0);
    candidate.nodes  = std::move(copied.nodes);
    const auto count = selected.size();
    project_.flows.push_back(std::move(candidate));
    project_.nextFlowId = next;
    project_.nextNodeId += count;
    project_.dirty = true;
    return id;
}

bool ProjectEditor::removeNodes(std::string_view flowId, std::span<const NodeId> nodes)
{
    requireIdle();
    auto      &target   = flow(flowId);
    const auto selected = selection(target, nodes);
    if (selected.empty())
        return false;
    std::set<NodeId> removed;
    for (const auto *node : selected) removed.insert(node->id);
    auto references = referencesTo(project_, removed);
    if (std::find(references.structures.begin(), references.structures.end(), &target) == references.structures.end())
        references.structures.push_back(&target);
    references.validate();
    references.apply();
    std::erase_if(target.nodes, [&](const auto &node) { return removed.contains(node.id); });
    project_.dirty = true;
    return true;
}

bool ProjectEditor::moveNodes(std::string_view flowId, std::span<const NodeId> nodes, GridPoint delta)
{
    requireIdle();
    auto      &target   = flow(flowId);
    const auto selected = selection(target, nodes);
    if (selected.empty() || delta == GridPoint{})
        return false;
    std::set<NodeId> moving;
    std::set<Cell>   destinations;
    for (const auto *node : selected)
    {
        moving.insert(node->id);
        const auto position = translated(node->position, delta.x, delta.y);
        if (!destinations.emplace(position.x, position.y).second)
            throw ContractError("Moved nodes overlap");
    }
    for (const auto &node : target.nodes)
        if (!moving.contains(node.id) && destinations.contains({node.position.x, node.position.y}))
            throw ContractError("Move collides with an existing node");
    for (auto &node : target.nodes)
        if (moving.contains(node.id))
            node.position = translated(node.position, delta.x, delta.y);
    project_.dirty = true;
    return true;
}

std::map<NodeId, NodeId> ProjectEditor::copyNodes(std::string_view sourceFlow, std::span<const NodeId> nodes,
                                                  std::string_view targetFlow, GridPoint preferredOffset)
{
    requireIdle();
    const auto selected = selection(flow(sourceFlow), nodes);
    auto      &target   = flow(targetFlow);
    if (selected.empty())
        return {};
    requireIncrement(target.structureRevision);
    std::vector<GridPoint> points;
    points.reserve(selected.size());
    for (const auto *node : selected) points.push_back(node->position);
    const auto [dx, dy] = nearestTranslation(points, target, preferredOffset);
    auto copied         = duplicateNodes(selected, project_.nextNodeId, dx, dy);
    target.nodes.reserve(target.nodes.size() + copied.nodes.size());
    for (auto &node : copied.nodes) target.nodes.push_back(std::move(node));
    project_.nextNodeId += copied.nodes.size();
    ++target.structureRevision;
    project_.dirty = true;
    return std::move(copied.mapping);
}

bool ProjectEditor::setAlias(std::string_view flowId, NodeId id, std::string alias)
{
    requireIdle();
    auto &node = findNode(flow(flowId), id);
    validateUtf8(alias);
    if (node.alias == alias)
        return false;
    node.alias     = std::move(alias);
    project_.dirty = true;
    return true;
}

} // namespace visionrt
