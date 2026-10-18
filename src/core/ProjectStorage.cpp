#include "core/ProjectStorage.h"
#include "core/ToolRegistry.h"
#include <visionrt/sdk/ValueYaml.h>

#include <QFile>
#include <QSaveFile>
#include <QString>
#include <algorithm>
#include <charconv>
#include <limits>
#include <stdexcept>
#include <string_view>

namespace visionrt {
namespace {
QString qtPath(const std::filesystem::path& path) {
    const auto utf8 = path.u8string();
    return QString::fromUtf8(reinterpret_cast<const char*>(utf8.data()),
                             static_cast<qsizetype>(utf8.size()));
}
std::string utf8Path(const std::filesystem::path& path) {
    const auto utf8 = path.generic_u8string();
    return {reinterpret_cast<const char*>(utf8.data()), utf8.size()};
}
[[noreturn]] void invalid(const std::string& message) {
    throw std::runtime_error(message);
}
void map(const YAML::Node& node, std::string_view context) {
    if (!node.IsMap()) invalid(std::string(context) + " must be a mapping");
    std::set<std::string, std::less<>> keys;
    for (const auto& entry : node) {
        if (!entry.first.IsScalar()) invalid(std::string(context) + " has a non-scalar key");
        if (!keys.insert(entry.first.Scalar()).second)
            invalid(std::string(context) + " has a duplicate key: " + entry.first.Scalar());
    }
}
std::string text(const YAML::Node& node, std::string_view context, bool nonempty = false) {
    if (!node.IsScalar()) invalid(std::string(context) + " must be a string");
    auto value = node.Scalar();
    validateUtf8(value);
    if (nonempty && (value.empty() || std::any_of(value.begin(), value.end(), [](unsigned char c) {
            return c < 0x20 || c == 0x7f;
        }))) invalid(std::string(context) + " must be a nonempty identifier");
    return value;
}
std::uint64_t sequence(const YAML::Node& node, std::string_view context) {
    const auto value = text(node, context, true);
    if (!std::all_of(value.begin(), value.end(), [](char c) { return c >= '0' && c <= '9'; }))
        invalid(std::string(context) + " must be an unsigned decimal uint64");
    std::uint64_t result = 0;
    const auto converted = std::from_chars(value.data(), value.data() + value.size(), result);
    if (converted.ec != std::errc{} || converted.ptr != value.data() + value.size())
        invalid(std::string(context) + " exceeds uint64");
    return result;
}
TypeMap decodeTypes(const YAML::Node& node, std::string_view context) {
    map(node, context);
    TypeMap result;
    for (const auto& entry : node)
        result.emplace(text(entry.first, context, true), decodeTypeSpec(entry.second));
    return result;
}
TypeMap types(const std::vector<PortDefinition>& ports) {
    TypeMap result;
    for (const auto& port : ports) result.emplace(port.id, port.type);
    return result;
}
void diagnose(ProjectLoadResult& result, ProjectStorageCode code, const FlowDefinition& flow,
              const NodeDefinition& node, std::string field, std::string message, bool changed = false) {
    result.diagnostics.push_back({code, flow.id, node.id, std::move(field), std::move(message)});
    result.project.dirty = result.project.dirty || changed;
}
void rangeWarning(ProjectLoadResult& result, const FlowDefinition& flow, const NodeDefinition& node,
                  const PortDefinition& port, const PortValue& value) {
    const auto outside = [&]<typename T>(const T* number) {
        if (!number) return false;
        const auto* minimum = port.minimum ? std::get_if<T>(&*port.minimum) : nullptr;
        const auto* maximum = port.maximum ? std::get_if<T>(&*port.maximum) : nullptr;
        return (minimum && *number < *minimum) || (maximum && *number > *maximum);
    };
    if (outside(std::get_if<std::int64_t>(&value)) || outside(std::get_if<double>(&value)))
        diagnose(result, ProjectStorageCode::RangeWarning, flow, node, port.id,
                 "Saved value is outside the current editor range; the value was retained");
}
PortValue defaultValue(const PortDefinition& port) {
    validateValue(port.type, port.defaultValue);
    return port.defaultValue;
}

void reconcileNode(ProjectLoadResult& result, const FlowDefinition& flow, NodeDefinition& node,
                   const YAML::Node& raw, const ToolDefinition& definition,
                   std::set<std::string, std::less<>>& changedOutputs) {
    auto initTypes = types(definition.initParams);
    auto inputTypes = types(definition.inputs);
    auto outputTypes = types(definition.outputs);
    for (const auto& [id, previous] : node.signatures.outputs) {
        const auto current = outputTypes.find(id);
        if (current == outputTypes.end() || !compatible(previous, current->second))
            changedOutputs.insert(id);
    }
    for (const auto& [id, current] : outputTypes)
        if (!node.signatures.outputs.contains(id)) changedOutputs.insert(id);
    if (node.signatures.initParams != initTypes || node.signatures.inputs != inputTypes ||
        node.signatures.outputs != outputTypes)
        diagnose(result, ProjectStorageCode::SchemaChanged, flow, node, "type_snapshot",
                 "Saved tool signatures were reconciled with the current definition", true);

    Values parameters;
    for (const auto& port : definition.initParams) {
        const auto old = node.initParams.find(port.id);
        const auto signature = node.signatures.initParams.find(port.id);
        const bool retain = old != node.initParams.end() && signature != node.signatures.initParams.end() &&
                            compatible(signature->second, port.type);
        auto value = retain ? old->second : defaultValue(port);
        if (!retain)
            diagnose(result, ProjectStorageCode::SchemaChanged, flow, node, "init_params." + port.id,
                     "Missing or incompatible parameter was replaced with its declared default", true);
        rangeWarning(result, flow, node, port, value);
        parameters.emplace(port.id, std::move(value));
    }
    for (const auto& [id, value] : node.initParams) {
        if (!initTypes.contains(id))
            diagnose(result, ProjectStorageCode::SchemaChanged, flow, node, "init_params." + id,
                     "Removed parameter was discarded", true);
    }
    std::map<std::string, InputBinding, std::less<>> inputs;
    for (const auto& port : definition.inputs) {
        const auto old = node.inputs.find(port.id);
        const auto signature = node.signatures.inputs.find(port.id);
        const bool retain = old != node.inputs.end() && signature != node.signatures.inputs.end() &&
                            compatible(signature->second, port.type);
        InputBinding binding;
        if (retain) {
            binding = old->second;
            if (!raw["inputs"][port.id]["literal"].IsDefined()) {
                binding.literal = defaultValue(port);
                diagnose(result, ProjectStorageCode::SchemaChanged, flow, node, "inputs." + port.id,
                         "Absent literal draft was filled with its declared default", true);
            }
        } else {
            binding.literal = defaultValue(port);
            if (old != node.inputs.end()) binding.mode = old->second.mode;
            diagnose(result, ProjectStorageCode::SchemaChanged, flow, node, "inputs." + port.id,
                     "Missing or incompatible input was reset; its old source was cleared", true);
        }
        rangeWarning(result, flow, node, port, binding.literal);
        inputs.emplace(port.id, std::move(binding));
    }
    for (const auto& [id, binding] : node.inputs) {
        if (!inputTypes.contains(id))
            diagnose(result, ProjectStorageCode::SchemaChanged, flow, node, "inputs." + id,
                     "Removed input and its binding were discarded", true);
    }
    node.initParams = std::move(parameters);
    node.inputs = std::move(inputs);
    node.signatures = {std::move(initTypes), std::move(inputTypes), std::move(outputTypes)};
    if (node.toolNameSnapshot != definition.displayName) {
        node.toolNameSnapshot = definition.displayName;
        diagnose(result, ProjectStorageCode::SchemaChanged, flow, node, "tool_name_snapshot",
                 "Tool display name snapshot was updated", true);
    }
}

void diagnoseCycles(ProjectLoadResult& result, const FlowDefinition& flow) {
    std::map<NodeId, std::size_t> indices;
    for (std::size_t i = 0; i < flow.nodes.size(); ++i) indices.emplace(flow.nodes[i].id, i);
    std::vector<std::vector<std::size_t>> edges(flow.nodes.size());
    for (std::size_t i = 0; i < flow.nodes.size(); ++i) {
        for (const auto& [id, binding] : flow.nodes[i].inputs) {
            if (binding.mode != BindingMode::Upstream || !binding.source) continue;
            const auto found = indices.find(binding.source->node);
            if (found != indices.end()) edges[i].push_back(found->second);
        }
    }
    const auto unseen = std::numeric_limits<std::size_t>::max();
    std::vector<std::size_t> discovery(flow.nodes.size(), unseen), low(flow.nodes.size()), stack;
    std::vector<bool> active(flow.nodes.size(), false);
    std::size_t next = 0;
    const auto visit = [&](auto&& self, std::size_t vertex) -> void {
        discovery[vertex] = low[vertex] = next++;
        stack.push_back(vertex);
        active[vertex] = true;
        for (const auto target : edges[vertex]) {
            if (discovery[target] == unseen) {
                self(self, target);
                low[vertex] = std::min(low[vertex], low[target]);
            } else if (active[target]) low[vertex] = std::min(low[vertex], discovery[target]);
        }
        if (low[vertex] != discovery[vertex]) return;
        std::vector<std::size_t> component;
        std::size_t member;
        do {
            member = stack.back();
            stack.pop_back();
            active[member] = false;
            component.push_back(member);
        } while (member != vertex);
        const bool cyclic = component.size() > 1 ||
            std::find(edges[vertex].begin(), edges[vertex].end(), vertex) != edges[vertex].end();
        if (cyclic) for (const auto index : component)
            diagnose(result, ProjectStorageCode::Cycle, flow, flow.nodes[index], {},
                     "Node participates in an upstream cycle; bindings were retained");
    };
    for (std::size_t i = 0; i < flow.nodes.size(); ++i)
        if (discovery[i] == unseen) visit(visit, i);
}

ProjectLoadResult decodeProject(const YAML::Node& root, const ToolRegistry& registry) {
    map(root, "project root");
    if (sequence(root["format_version"], "format_version") != 1)
        invalid("Unsupported project format_version");
    map(root["project"], "project");
    if (!root["flows"].IsSequence()) invalid("flows must be a sequence");
    ProjectLoadResult result;
    auto& project = result.project;
    project.flows.clear();
    project.dirty = false;
    project.name = text(root["project"]["name"], "project.name");
    project.nextNodeId = sequence(root["project"]["next_node_seq"], "next_node_seq");
    project.nextFlowId = sequence(root["project"]["next_flow_seq"], "next_flow_seq");
    std::set<std::string, std::less<>> flowIds, flowNames;
    std::set<NodeId> nodeIds;
    std::map<NodeId, std::set<std::string, std::less<>>> changedOutputs;
    NodeId minimumNode = 0;
    std::uint64_t minimumFlow = 1;
    for (const auto& rawFlow : root["flows"]) {
        map(rawFlow, "flow");
        FlowDefinition flow;
        flow.id = text(rawFlow["flow_id"], "flow_id", true);
        if (!flowIds.insert(flow.id).second) invalid("Duplicate flow_id: " + flow.id);
        if (flow.id.starts_with("flow_")) {
            const std::string_view suffix(flow.id.data() + 5, flow.id.size() - 5);
            std::uint64_t number = 0;
            const auto parsed = std::from_chars(suffix.data(), suffix.data() + suffix.size(), number);
            if (parsed.ec == std::errc{} && parsed.ptr == suffix.data() + suffix.size()) {
                const bool exhausted = number == std::numeric_limits<std::uint64_t>::max();
                minimumFlow = std::max(minimumFlow, exhausted ? number : number + 1);
                if (exhausted)
                    result.diagnostics.push_back({ProjectStorageCode::SequenceExhausted, flow.id, {}, {},
                                                  "Flow identity sequence is exhausted; creating flows is disabled"});
            }
        }
        flow.name = text(rawFlow["name"], "flow.name");
        if (!flowNames.insert(flow.name).second)
            result.diagnostics.push_back({ProjectStorageCode::DuplicateFlowName, flow.id, {}, {},
                                          "Duplicate flow display name; rename before name-based lookup"});
        if (!rawFlow["run_selected"].IsScalar()) invalid("run_selected must be a boolean");
        flow.runSelected = rawFlow["run_selected"].as<bool>();
        if (!rawFlow["nodes"].IsSequence()) invalid("flow.nodes must be a sequence");
        std::map<std::pair<int, int>, NodeId> positions;
        for (const auto& raw : rawFlow["nodes"]) {
            map(raw, "node");
            NodeDefinition node;
            node.id = sequence(raw["node_seq"], "node_seq");
            if (!nodeIds.insert(node.id).second) invalid("Duplicate project-wide node_seq");
            const bool exhausted = node.id == std::numeric_limits<NodeId>::max();
            minimumNode = std::max(minimumNode, exhausted ? node.id : node.id + 1);
            if (exhausted)
                diagnose(result, ProjectStorageCode::SequenceExhausted, flow, node, "node_seq",
                         "Node identity sequence is exhausted; creating nodes is disabled");
            node.toolId = text(raw["tool_id"], "tool_id", true);
            node.toolNameSnapshot = text(raw["tool_name_snapshot"], "tool_name_snapshot");
            node.alias = text(raw["alias"], "alias");
            if (!raw["pos"].IsSequence() || raw["pos"].size() != 2)
                invalid("node.pos must contain two integer coordinates");
            node.position = {raw["pos"][0].as<int>(), raw["pos"][1].as<int>()};
            const auto [position, inserted] = positions.emplace(
                std::pair{node.position.x, node.position.y}, node.id);
            if (!inserted) {
                diagnose(result, ProjectStorageCode::LayoutConflict, flow, node, "pos",
                         "Node overlaps node " + std::to_string(position->second) + "; position was retained");
            }
            map(raw["type_snapshot"], "type_snapshot");
            node.signatures = {decodeTypes(raw["type_snapshot"]["init_params"], "init signature"),
                               decodeTypes(raw["type_snapshot"]["inputs"], "input signature"),
                               decodeTypes(raw["type_snapshot"]["outputs"], "output signature")};
            const auto* definition = registry.find(node.toolId);
            map(raw["init_params"], "init_params");
            for (const auto& entry : raw["init_params"]) {
                const auto id = text(entry.first, "init parameter ID", true);
                const auto signature = node.signatures.initParams.find(id);
                if (signature == node.signatures.initParams.end()) {
                    if (definition) invalid("Saved init parameter has no type_snapshot: " + id);
                } else node.initParams.emplace(id, decodeValue(entry.second, signature->second));
            }
            map(raw["inputs"], "inputs");
            for (const auto& entry : raw["inputs"]) {
                const auto id = text(entry.first, "input ID", true);
                map(entry.second, "input binding");
                InputBinding binding;
                const auto mode = text(entry.second["mode"], "binding mode");
                if (mode == "upstream") binding.mode = BindingMode::Upstream;
                else if (mode != "literal") invalid("Unknown input binding mode: " + mode);
                const auto signature = node.signatures.inputs.find(id);
                if (signature == node.signatures.inputs.end()) {
                    if (definition) invalid("Saved input has no type_snapshot: " + id);
                } else if (entry.second["literal"].IsDefined()) {
                    binding.literal = decodeValue(entry.second["literal"], signature->second);
                }
                const auto source = entry.second["source"];
                if (source.IsDefined() && !source.IsNull()) {
                    map(source, "binding source");
                    binding.source = SourcePort{sequence(source["node_seq"], "source.node_seq"),
                                                text(source["port_id"], "source.port_id", true)};
                }
                node.inputs.emplace(id, std::move(binding));
            }
            if (!definition) {
                node.unresolved = YAML::Clone(raw);
                diagnose(result, ProjectStorageCode::DefinitionMissing, flow, node, {},
                         "Tool definition is missing; original configuration was retained: " + node.toolId);
            } else reconcileNode(result, flow, node, raw, *definition, changedOutputs[node.id]);
            flow.nodes.push_back(std::move(node));
        }
        project.flows.push_back(std::move(flow));
    }
    if (project.nextNodeId < minimumNode || project.nextFlowId < minimumFlow) {
        project.nextNodeId = std::max(project.nextNodeId, minimumNode);
        project.nextFlowId = std::max(project.nextFlowId, minimumFlow);
        project.dirty = true;
        result.diagnostics.push_back({ProjectStorageCode::CounterRepaired, {}, {}, {},
                                      "Next identity counters were raised above existing identifiers"});
    }
    for (auto& flow : project.flows) {
        std::map<NodeId, const NodeDefinition*> nodes;
        for (const auto& node : flow.nodes) nodes.emplace(node.id, &node);
        for (auto& node : flow.nodes) {
            if (node.unresolved.IsMap()) continue;
            for (auto& [id, binding] : node.inputs) {
                if (!binding.source) continue;
                const auto source = nodes.find(binding.source->node);
                if (source != nodes.end() && source->second->unresolved.IsMap()) {
                    diagnose(result, ProjectStorageCode::PendingReference, flow, node, "inputs." + id,
                             "Source definition is missing; reference was retained for recovery");
                    continue;
                }
                bool valid = false;
                if (source != nodes.end()) {
                    const auto output = source->second->signatures.outputs.find(binding.source->port);
                    valid = output != source->second->signatures.outputs.end() &&
                            compatible(output->second, node.signatures.inputs.at(id)) &&
                            !changedOutputs[source->second->id].contains(binding.source->port);
                }
                if (!valid) {
                    binding.source.reset();
                    diagnose(result, ProjectStorageCode::InvalidReference, flow, node, "inputs." + id,
                             "Source is absent, changed, cross-flow, or incompatible; reference was cleared", true);
                }
            }
        }
        diagnoseCycles(result, flow);
    }
    return result;
}

YAML::Node encodeTypes(const TypeMap& types) {
    YAML::Node result(YAML::NodeType::Map);
    for (const auto& [id, type] : types) result[id] = encodeTypeSpec(type);
    return result;
}
YAML::Node encodeProject(const ProjectDefinition& project) {
    YAML::Node root(YAML::NodeType::Map);
    root["format_version"] = 1;
    root["project"]["name"] = project.name;
    root["project"]["next_node_seq"] = project.nextNodeId;
    root["project"]["next_flow_seq"] = project.nextFlowId;
    root["flows"] = YAML::Node(YAML::NodeType::Sequence);
    for (const auto& flow : project.flows) {
        YAML::Node rawFlow(YAML::NodeType::Map);
        rawFlow["flow_id"] = flow.id;
        rawFlow["name"] = flow.name;
        rawFlow["run_selected"] = flow.runSelected;
        rawFlow["nodes"] = YAML::Node(YAML::NodeType::Sequence);
        for (const auto& node : flow.nodes) {
            const bool unresolved = node.unresolved.IsMap();
            YAML::Node raw = unresolved ? YAML::Clone(node.unresolved) : YAML::Node(YAML::NodeType::Map);
            raw["node_seq"] = node.id;
            raw["tool_id"] = node.toolId;
            raw["tool_name_snapshot"] = node.toolNameSnapshot;
            raw["alias"] = node.alias;
            raw["pos"] = YAML::Node(YAML::NodeType::Sequence);
            raw["pos"].push_back(node.position.x);
            raw["pos"].push_back(node.position.y);
            if (!unresolved) {
                raw["init_params"] = YAML::Node(YAML::NodeType::Map);
                for (const auto& [id, value] : node.initParams) {
                    const auto signature = node.signatures.initParams.find(id);
                    if (signature == node.signatures.initParams.end())
                        invalid("Cannot save parameter without type_snapshot: " + id);
                    raw["init_params"][id] = encodeValue(value, signature->second);
                }
                raw["type_snapshot"]["init_params"] = encodeTypes(node.signatures.initParams);
                raw["type_snapshot"]["inputs"] = encodeTypes(node.signatures.inputs);
                raw["type_snapshot"]["outputs"] = encodeTypes(node.signatures.outputs);
                raw["inputs"] = YAML::Node(YAML::NodeType::Map);
            }
            for (const auto& [id, binding] : node.inputs) {
                auto saved = raw["inputs"][id];
                saved["mode"] = binding.mode == BindingMode::Upstream ? "upstream" : "literal";
                if (!unresolved) {
                    const auto signature = node.signatures.inputs.find(id);
                    if (signature == node.signatures.inputs.end())
                        invalid("Cannot save input without type_snapshot: " + id);
                    saved["literal"] = encodeValue(binding.literal, signature->second);
                }
                if (binding.source) {
                    saved["source"]["node_seq"] = binding.source->node;
                    saved["source"]["port_id"] = binding.source->port;
                } else saved.remove("source");
            }
            rawFlow["nodes"].push_back(raw);
        }
        root["flows"].push_back(rawFlow);
    }
    return root;
}

void rewritePath(PortValue& value, const PortDefinition& port,
                 const std::filesystem::path& oldDirectory, const std::filesystem::path& newDirectory) {
    if (port.type.type != Type::String || (port.editor != "file" && port.editor != "directory")) return;
    auto* string = std::get_if<Utf8>(&value);
    if (!string || string->str().empty()) return;
    const auto original = std::filesystem::u8path(string->str());
    if (original.is_relative() && oldDirectory.empty()) return;
    const auto absolute = (original.is_absolute() ? original : oldDirectory / original).lexically_normal();
    const auto relative = absolute.lexically_relative(newDirectory);
    value = Utf8(utf8Path(relative.empty() ? absolute : relative));
}
} // namespace

ProjectLoadResult ProjectStorage::load(const std::filesystem::path& path) const {
    const auto absolute = std::filesystem::absolute(path).lexically_normal();
    QFile file(qtPath(absolute));
    if (!file.open(QIODevice::ReadOnly))
        invalid("Cannot open project " + utf8Path(absolute) + ": " + file.errorString().toStdString());
    const auto bytes = file.readAll();
    if (file.error() != QFileDevice::NoError)
        invalid("Cannot read project " + utf8Path(absolute) + ": " + file.errorString().toStdString());
    try {
        auto result = decodeProject(YAML::Load(bytes.toStdString()), registry_);
        result.project.path = absolute;
        return result;
    } catch (const std::exception& error) {
        invalid("Cannot load project " + utf8Path(absolute) + ": " + error.what());
    }
}

ProjectLoadResult ProjectStorage::reconcile(const ProjectDefinition& project) const {
    auto result = decodeProject(encodeProject(project), registry_);
    result.project.path = project.path;
    result.project.dirty = result.project.dirty || project.dirty;
    for (std::size_t f = 0; f < project.flows.size(); ++f) {
        auto& updatedFlow = result.project.flows[f];
        updatedFlow.structureRevision = project.flows[f].structureRevision;
        bool changedStructure = false;
        for (std::size_t n = 0; n < project.flows[f].nodes.size(); ++n) {
            auto& updated = updatedFlow.nodes[n];
            const auto& original = project.flows[f].nodes[n];
            updated.executionRevision = original.executionRevision;
            const bool contractChanged = original.unresolved.IsMap() != updated.unresolved.IsMap() ||
                original.signatures.initParams != updated.signatures.initParams ||
                original.signatures.inputs != updated.signatures.inputs ||
                original.signatures.outputs != updated.signatures.outputs;
            changedStructure = changedStructure || contractChanged;
            bool changedExecution = contractChanged;
            for (const auto& [id, binding] : updated.inputs) {
                const auto previous = original.inputs.find(id);
                if (previous == original.inputs.end()) continue;
                const auto effectiveSource = [](const InputBinding& value) {
                    return value.mode == BindingMode::Upstream ? value.source : std::nullopt;
                };
                if (effectiveSource(previous->second) != effectiveSource(binding)) {
                    changedStructure = true;
                    changedExecution = true;
                }
            }
            for (const auto& diagnostic : result.diagnostics) {
                if (diagnostic.node != updated.id || diagnostic.code != ProjectStorageCode::SchemaChanged) continue;
                if (diagnostic.field.starts_with("init_params.")) changedExecution = true;
                if (diagnostic.field.starts_with("inputs.")) {
                    const auto input = updated.inputs.find(diagnostic.field.substr(7));
                    if (input != updated.inputs.end() && input->second.mode == BindingMode::Literal)
                        changedExecution = true;
                }
            }
            if (changedExecution) ++updated.executionRevision;
        }
        if (changedStructure) ++updatedFlow.structureRevision;
    }
    return result;
}

std::vector<ProjectStorageDiagnostic> ProjectStorage::save(
    ProjectDefinition& project, const std::filesystem::path& destination) const {
    auto target = destination.empty() ? project.path : destination;
    if (target.empty()) invalid("An unnamed project requires a save destination");
    if (!target.has_extension()) target += ".vrt";
    target = std::filesystem::absolute(target).lexically_normal();
    ProjectDefinition candidate = project;
    std::vector<ProjectStorageDiagnostic> diagnostics;
    const auto oldDirectory = project.path.empty() ? std::filesystem::path{} :
        std::filesystem::absolute(project.path).lexically_normal().parent_path();
    if (project.path.empty() || target != std::filesystem::absolute(project.path).lexically_normal()) {
        for (auto& flow : candidate.flows) {
            for (auto& node : flow.nodes) {
                const auto* definition = registry_.find(node.toolId);
                if (!definition || node.unresolved.IsMap()) {
                    diagnostics.push_back({ProjectStorageCode::UnknownPaths, flow.id, node.id, {},
                                           "Unknown tool fields were retained; relative paths may need repair after Save As"});
                    continue;
                }
                for (const auto& port : definition->initParams) {
                    const auto value = node.initParams.find(port.id);
                    if (value != node.initParams.end())
                        rewritePath(value->second, port, oldDirectory, target.parent_path());
                }
                for (const auto& port : definition->inputs) {
                    const auto value = node.inputs.find(port.id);
                    if (value != node.inputs.end())
                        rewritePath(value->second.literal, port, oldDirectory, target.parent_path());
                }
            }
        }
    }
    candidate.path = target;
    candidate.dirty = false;
    const auto serialized = YAML::Dump(encodeProject(candidate));
    QSaveFile file(qtPath(target));
    file.setDirectWriteFallback(false);
    if (!file.open(QIODevice::WriteOnly))
        invalid("Cannot save project " + utf8Path(target) + ": " + file.errorString().toStdString());
    if (file.write(serialized.data(), static_cast<qint64>(serialized.size())) != static_cast<qint64>(serialized.size())) {
        const auto error = file.errorString().toStdString();
        file.cancelWriting();
        invalid("Cannot write project " + utf8Path(target) + ": " + error);
    }
    if (!file.commit())
        invalid("Cannot commit project " + utf8Path(target) + ": " + file.errorString().toStdString());
    project = std::move(candidate);
    return diagnostics;
}
} // namespace visionrt
