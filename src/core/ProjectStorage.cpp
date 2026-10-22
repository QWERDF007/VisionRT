#include "core/ProjectStorage.h"

#include "core/NodeContracts.h"
#include "core/ToolRegistry.h"
#include "core/ValueJson.h"

#include <core/ToolValueValidation.h>

#include <QFile>
#include <QJsonArray>
#include <QSaveFile>
#include <QString>
#include <algorithm>
#include <charconv>
#include <limits>
#include <stdexcept>
#include <string_view>

namespace visionrt {
namespace {
QString qtPath(const std::filesystem::path &path)
{
    const auto utf8 = path.u8string();
    return QString::fromUtf8(reinterpret_cast<const char *>(utf8.data()), static_cast<qsizetype>(utf8.size()));
}

std::string utf8Path(const std::filesystem::path &path)
{
    const auto utf8 = path.generic_u8string();
    return {reinterpret_cast<const char *>(utf8.data()), utf8.size()};
}

[[noreturn]] void invalid(const std::string &message)
{
    throw std::runtime_error(message);
}

QJsonObject map(const QJsonValue &node, std::string_view context)
{
    if (!node.isObject())
        invalid(std::string(context) + " must be a JSON object");
    return node.toObject();
}

std::string text(const QJsonValue &node, std::string_view context, bool nonempty = false)
{
    if (!node.isString())
        invalid(std::string(context) + " must be a string");
    auto value = node.toString().toStdString();
    validateUtf8(value);
    if (nonempty
        && (value.empty()
            || std::any_of(value.begin(), value.end(), [](unsigned char c) { return c < 0x20 || c == 0x7f; })))
        invalid(std::string(context) + " must be a nonempty identifier");
    return value;
}

std::uint64_t sequence(const QJsonValue &node, std::string_view context)
{
    const auto value = text(node, context, true);
    if (!std::all_of(value.begin(), value.end(), [](char c) { return c >= '0' && c <= '9'; }))
        invalid(std::string(context) + " must be an unsigned decimal uint64");
    std::uint64_t result    = 0;
    const auto    converted = std::from_chars(value.data(), value.data() + value.size(), result);
    if (converted.ec != std::errc{} || converted.ptr != value.data() + value.size())
        invalid(std::string(context) + " exceeds uint64");
    return result;
}

TypeMap decodeTypes(const QJsonValue &node, std::string_view context)
{
    const auto entries = map(node, context);
    TypeMap    result;
    for (auto entry = entries.begin(); entry != entries.end(); ++entry)
        result.emplace(text(entry.key(), context, true), ValueJson::decodeType(map(entry.value(), context)));
    return result;
}

TypeMap types(const std::vector<PortDefinition> &ports)
{
    TypeMap result;
    for (const auto &port : ports) result.emplace(port.id, port.type);
    return result;
}

void diagnose(ProjectLoadResult &result, ProjectStorageCode code, const FlowDefinition &flow,
              const NodeDefinition &node, std::string field, std::string message, bool changed = false)
{
    result.diagnostics.push_back({code, flow.id, node.id, std::move(field), std::move(message)});
    result.project.dirty = result.project.dirty || changed;
}

void rangeWarning(ProjectLoadResult &result, const FlowDefinition &flow, const NodeDefinition &node,
                  const PortDefinition &port, const PortValue &value)
{
    try
    {
        validateToolValue(port, value);
    }
    catch (const ContractError &error)
    {
        diagnose(result, ProjectStorageCode::RangeWarning, flow, node, port.id,
                 "保存的值不符合当前约束，已保留原值：" + std::string(error.what()));
    }
}

PortValue defaultValue(const PortDefinition &port)
{
    validateValue(port.type, port.defaultValue);
    return port.defaultValue;
}

void reconcileNode(ProjectLoadResult &result, const FlowDefinition &flow, NodeDefinition &node, const QJsonObject &raw,
                   const ToolDefinition &definition, std::set<std::string, std::less<>> &changedOutputs)
{
    auto       initTypes    = types(definition.initParams);
    auto       inputTypes   = types(definition.inputs);
    const auto savedOutputs = node.signatures.outputs;
    setOutputDeclarations(node, definition);
    auto outputTypes        = std::move(node.signatures.outputs);
    node.signatures.outputs = savedOutputs;
    for (const auto &[id, previous] : node.signatures.outputs)
    {
        const auto current = outputTypes.find(id);
        if (current == outputTypes.end() || !compatible(previous, current->second))
            changedOutputs.insert(id);
    }
    for (const auto &[id, current] : outputTypes)
        if (!node.signatures.outputs.contains(id))
            changedOutputs.insert(id);
    if (node.signatures.initParams != initTypes || node.signatures.inputs != inputTypes
        || node.signatures.outputs != outputTypes)
        diagnose(result, ProjectStorageCode::SchemaChanged, flow, node, "type_snapshot",
                 "Saved tool signatures were reconciled with the current definition", true);

    Values parameters;
    for (const auto &port : definition.initParams)
    {
        const auto old       = node.initParams.find(port.id);
        const auto signature = node.signatures.initParams.find(port.id);
        const bool retain    = old != node.initParams.end() && signature != node.signatures.initParams.end()
                         && compatible(signature->second, port.type);
        auto value = retain ? old->second : defaultValue(port);
        if (!retain)
            diagnose(result, ProjectStorageCode::SchemaChanged, flow, node, "init_params." + port.id,
                     "Missing or incompatible parameter was replaced with its declared default", true);
        rangeWarning(result, flow, node, port, value);
        parameters.emplace(port.id, std::move(value));
    }
    for (const auto &[id, value] : node.initParams)
    {
        if (!initTypes.contains(id))
            diagnose(result, ProjectStorageCode::SchemaChanged, flow, node, "init_params." + id,
                     "Removed parameter was discarded", true);
    }
    std::map<std::string, InputBinding, std::less<>> inputs;
    for (const auto &port : definition.inputs)
    {
        const auto old       = node.inputs.find(port.id);
        const auto signature = node.signatures.inputs.find(port.id);
        const bool retain    = old != node.inputs.end() && signature != node.signatures.inputs.end()
                         && compatible(signature->second, port.type);
        InputBinding binding;
        if (retain)
        {
            binding = old->second;
            if (!raw.value("inputs").toObject().value(QString::fromStdString(port.id)).toObject().contains("literal"))
            {
                binding.literal = defaultValue(port);
                diagnose(result, ProjectStorageCode::SchemaChanged, flow, node, "inputs." + port.id,
                         "Absent literal draft was filled with its declared default", true);
            }
        }
        else
        {
            binding.literal = defaultValue(port);
            if (old != node.inputs.end())
                binding.mode = old->second.mode;
            diagnose(result, ProjectStorageCode::SchemaChanged, flow, node, "inputs." + port.id,
                     "Missing or incompatible input was reset; its old source was cleared", true);
        }
        if (binding.mode == BindingMode::Literal || !binding.source)
            rangeWarning(result, flow, node, port, binding.literal);
        inputs.emplace(port.id, std::move(binding));
    }
    for (const auto &[id, binding] : node.inputs)
    {
        if (!inputTypes.contains(id))
            diagnose(result, ProjectStorageCode::SchemaChanged, flow, node, "inputs." + id,
                     "Removed input and its binding were discarded", true);
    }
    node.initParams = std::move(parameters);
    node.inputs     = std::move(inputs);
    node.signatures = {std::move(initTypes), std::move(inputTypes), std::move(outputTypes)};
    if (node.toolNameSnapshot != definition.displayName)
    {
        node.toolNameSnapshot = definition.displayName;
        diagnose(result, ProjectStorageCode::SchemaChanged, flow, node, "tool_name_snapshot",
                 "Tool display name snapshot was updated", true);
    }
}

void diagnoseCycles(ProjectLoadResult &result, const FlowDefinition &flow)
{
    std::map<NodeId, std::size_t> indices;
    for (std::size_t i = 0; i < flow.nodes.size(); ++i) indices.emplace(flow.nodes[i].id, i);
    std::vector<std::vector<std::size_t>> edges(flow.nodes.size());
    for (std::size_t i = 0; i < flow.nodes.size(); ++i)
    {
        for (const auto &[id, binding] : flow.nodes[i].inputs)
        {
            if (binding.mode != BindingMode::Upstream || !binding.source)
                continue;
            const auto found = indices.find(binding.source->node);
            if (found != indices.end())
                edges[i].push_back(found->second);
        }
    }
    const auto               unseen = std::numeric_limits<std::size_t>::max();
    std::vector<std::size_t> discovery(flow.nodes.size(), unseen), low(flow.nodes.size()), stack;
    std::vector<bool>        active(flow.nodes.size(), false);
    std::size_t              next  = 0;
    const auto               visit = [&](auto &&self, std::size_t vertex) -> void
    {
        discovery[vertex] = low[vertex] = next++;
        stack.push_back(vertex);
        active[vertex] = true;
        for (const auto target : edges[vertex])
        {
            if (discovery[target] == unseen)
            {
                self(self, target);
                low[vertex] = std::min(low[vertex], low[target]);
            }
            else if (active[target])
                low[vertex] = std::min(low[vertex], discovery[target]);
        }
        if (low[vertex] != discovery[vertex])
            return;
        std::vector<std::size_t> component;
        std::size_t              member;
        do
        {
            member = stack.back();
            stack.pop_back();
            active[member] = false;
            component.push_back(member);
        }
        while (member != vertex);
        const bool cyclic = component.size() > 1
                         || std::find(edges[vertex].begin(), edges[vertex].end(), vertex) != edges[vertex].end();
        if (cyclic)
            for (const auto index : component)
                diagnose(result, ProjectStorageCode::Cycle, flow, flow.nodes[index], {},
                         "Node participates in an upstream cycle; bindings were retained");
    };
    for (std::size_t i = 0; i < flow.nodes.size(); ++i)
        if (discovery[i] == unseen)
            visit(visit, i);
}

ProjectLoadResult decodeProject(const ValueJson::Document &document, const ToolRegistry &registry)
{
    if (!document.json().isObject())
        invalid("工程格式不受支持，请使用 JSON 工程");
    const auto root = document.json().object();
    if (!root.value("format_version").isDouble()
        || std::get<std::int64_t>(document.decode({"format_version"}, {Type::Int})) != 2)
        invalid("工程格式不受支持，请使用 JSON 工程");
    const auto metadata = map(root.value("project"), "project");
    if (!root.value("flows").isArray())
        invalid("flows must be an array");
    ProjectLoadResult result;
    auto             &project = result.project;
    project.flows.clear();
    project.dirty      = false;
    project.name       = text(metadata.value("name"), "project.name");
    project.nextNodeId = sequence(metadata.value("next_node_seq"), "next_node_seq");
    project.nextFlowId = sequence(metadata.value("next_flow_seq"), "next_flow_seq");
    std::set<std::string, std::less<>>                   flowIds, flowNames;
    std::set<NodeId>                                     nodeIds;
    std::map<NodeId, std::set<std::string, std::less<>>> changedOutputs;
    NodeId                                               minimumNode = 0;
    std::uint64_t                                        minimumFlow = 1;
    const auto                                           flows       = root.value("flows").toArray();
    for (qsizetype flowIndex = 0; flowIndex < flows.size(); ++flowIndex)
    {
        const auto     rawFlow = map(flows[flowIndex], "flow");
        FlowDefinition flow;
        flow.id = text(rawFlow.value("flow_id"), "flow_id", true);
        if (!flowIds.insert(flow.id).second)
            invalid("Duplicate flow_id: " + flow.id);
        if (flow.id.starts_with("flow_"))
        {
            const std::string_view suffix(flow.id.data() + 5, flow.id.size() - 5);
            std::uint64_t          number = 0;
            const auto             parsed = std::from_chars(suffix.data(), suffix.data() + suffix.size(), number);
            if (parsed.ec == std::errc{} && parsed.ptr == suffix.data() + suffix.size())
            {
                const bool exhausted = number == std::numeric_limits<std::uint64_t>::max();
                minimumFlow          = std::max(minimumFlow, exhausted ? number : number + 1);
                if (exhausted)
                    result.diagnostics.push_back({ProjectStorageCode::SequenceExhausted,
                                                  flow.id,
                                                  {},
                                                  {},
                                                  "Flow identity sequence is exhausted; creating flows is disabled"});
            }
        }
        flow.name = text(rawFlow.value("name"), "flow.name");
        if (!flowNames.insert(flow.name).second)
            result.diagnostics.push_back({ProjectStorageCode::DuplicateFlowName,
                                          flow.id,
                                          {},
                                          {},
                                          "Duplicate flow display name; rename before name-based lookup"});
        if (!rawFlow.value("run_selected").isBool())
            invalid("run_selected must be a boolean");
        flow.runSelected = rawFlow.value("run_selected").toBool();
        if (!rawFlow.value("nodes").isArray())
            invalid("flow.nodes must be an array");
        std::map<std::pair<int, int>, NodeId> positions;
        const auto                            nodes = rawFlow.value("nodes").toArray();
        for (qsizetype nodeIndex = 0; nodeIndex < nodes.size(); ++nodeIndex)
        {
            const auto        raw = map(nodes[nodeIndex], "node");
            const QStringList nodePath{"flows", QString::number(flowIndex), "nodes", QString::number(nodeIndex)};
            const auto        literal = [&](const QStringList &path, const TypeSpec &type)
            {
                return document.decode(nodePath + path, type);
            };
            NodeDefinition node;
            node.id = sequence(raw.value("node_seq"), "node_seq");
            if (!nodeIds.insert(node.id).second)
                invalid("Duplicate project-wide node_seq");
            const bool exhausted = node.id == std::numeric_limits<NodeId>::max();
            minimumNode          = std::max(minimumNode, exhausted ? node.id : node.id + 1);
            if (exhausted)
                diagnose(result, ProjectStorageCode::SequenceExhausted, flow, node, "node_seq",
                         "Node identity sequence is exhausted; creating nodes is disabled");
            node.toolId           = text(raw.value("tool_id"), "tool_id", true);
            node.toolNameSnapshot = text(raw.value("tool_name_snapshot"), "tool_name_snapshot");
            node.alias            = text(raw.value("alias"), "alias");
            if (!raw.value("pos").isArray() || raw.value("pos").toArray().size() != 2)
                invalid("node.pos must contain two integer coordinates");
            const auto x = std::get<std::int64_t>(literal({"pos", "0"}, {Type::Int}));
            const auto y = std::get<std::int64_t>(literal({"pos", "1"}, {Type::Int}));
            if (x < std::numeric_limits<int>::min() || x > std::numeric_limits<int>::max()
                || y < std::numeric_limits<int>::min() || y > std::numeric_limits<int>::max())
                invalid("node.pos exceeds signed coordinate range");
            node.position                   = {static_cast<int>(x), static_cast<int>(y)};
            const auto [position, inserted] = positions.emplace(std::pair{node.position.x, node.position.y}, node.id);
            if (!inserted)
                diagnose(result, ProjectStorageCode::LayoutConflict, flow, node, "pos",
                         "Node overlaps node " + std::to_string(position->second) + "; position was retained");
            const auto signatures  = map(raw.value("type_snapshot"), "type_snapshot");
            node.signatures        = {decodeTypes(signatures.value("init_params"), "init signature"),
                                      decodeTypes(signatures.value("inputs"), "input signature"),
                                      decodeTypes(signatures.value("outputs"), "output signature")};
            const auto *definition = registry.find(node.toolId);
            const auto  parameters = map(raw.value("init_params"), "init_params");
            for (auto entry = parameters.begin(); entry != parameters.end(); ++entry)
            {
                const auto id        = text(entry.key(), "init parameter ID", true);
                const auto signature = node.signatures.initParams.find(id);
                if (signature == node.signatures.initParams.end())
                    invalid("Saved init parameter has no type_snapshot: " + id);
                node.initParams.emplace(id, literal({"init_params", entry.key()}, signature->second));
            }
            const auto inputs = map(raw.value("inputs"), "inputs");
            for (auto entry = inputs.begin(); entry != inputs.end(); ++entry)
            {
                const auto   id    = text(entry.key(), "input ID", true);
                const auto   saved = map(entry.value(), "input binding");
                InputBinding binding;
                const auto   mode = text(saved.value("mode"), "binding mode");
                if (mode == "upstream")
                    binding.mode = BindingMode::Upstream;
                else if (mode != "literal")
                    invalid("Unknown input binding mode: " + mode);
                const auto signature = node.signatures.inputs.find(id);
                if (signature == node.signatures.inputs.end())
                    invalid("Saved input has no type_snapshot: " + id);
                if (saved.contains("literal"))
                    binding.literal = literal({"inputs", entry.key(), "literal"}, signature->second);
                const auto source = saved.value("source");
                if (!source.isUndefined() && !source.isNull())
                {
                    const auto reference = map(source, "binding source");
                    if (binding.mode != BindingMode::Upstream)
                        invalid("Literal binding must not contain an active source");
                    binding.source = SourcePort{sequence(reference.value("node_seq"), "source.node_seq"),
                                                text(reference.value("port_id"), "source.port_id", true)};
                }
                node.inputs.emplace(id, std::move(binding));
            }
            if (!definition)
            {
                node.unresolved = raw;
                diagnose(result, ProjectStorageCode::DefinitionMissing, flow, node, {},
                         "Tool definition is missing; original configuration was retained: " + node.toolId);
            }
            else
                reconcileNode(result, flow, node, raw, *definition, changedOutputs[node.id]);
            flow.nodes.push_back(std::move(node));
        }
        project.flows.push_back(std::move(flow));
    }
    if (project.nextNodeId < minimumNode || project.nextFlowId < minimumFlow)
    {
        project.nextNodeId = std::max(project.nextNodeId, minimumNode);
        project.nextFlowId = std::max(project.nextFlowId, minimumFlow);
        project.dirty      = true;
        result.diagnostics.push_back({ProjectStorageCode::CounterRepaired,
                                      {},
                                      {},
                                      {},
                                      "Next identity counters were raised above existing identifiers"});
    }
    std::map<NodeId, TypeMap> savedOutputs;
    for (const auto &flow : project.flows)
        for (const auto &node : flow.nodes) savedOutputs.emplace(node.id, node.signatures.outputs);
    resolveOutputTypes(project);
    for (auto &flow : project.flows)
    {
        for (const auto &node : flow.nodes)
        {
            for (const auto &[port, type] : savedOutputs.at(node.id))
                if (!node.signatures.outputs.contains(port) || node.signatures.outputs.at(port) != type)
                    changedOutputs[node.id].insert(port);
            for (const auto &[port, message] : node.outputTypeErrors)
                diagnose(result, ProjectStorageCode::InvalidReference, flow, node, "outputs." + port, message);
        }
        std::map<NodeId, const NodeDefinition *> nodes;
        for (const auto &node : flow.nodes) nodes.emplace(node.id, &node);
        for (auto &node : flow.nodes)
        {
            if (!node.unresolved.isEmpty())
                continue;
            for (auto &[id, binding] : node.inputs)
            {
                if (!binding.source)
                    continue;
                const auto source = nodes.find(binding.source->node);
                if (source != nodes.end() && !source->second->unresolved.isEmpty())
                {
                    diagnose(result, ProjectStorageCode::PendingReference, flow, node, "inputs." + id,
                             "Source definition is missing; reference was retained for recovery");
                    continue;
                }
                bool valid = false;
                if (source != nodes.end())
                {
                    const auto output = source->second->signatures.outputs.find(binding.source->port);
                    valid             = output != source->second->signatures.outputs.end()
                         && compatible(output->second, node.signatures.inputs.at(id))
                         && !changedOutputs[source->second->id].contains(binding.source->port);
                }
                if (!valid)
                {
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

QJsonObject encodeTypes(const TypeMap &types)
{
    QJsonObject result;
    for (const auto &[id, type] : types) result.insert(QString::fromStdString(id), ValueJson::encodeType(type));
    return result;
}

QJsonObject encodeProject(const ProjectDefinition &project)
{
    QJsonArray flows;
    for (const auto &flow : project.flows)
    {
        QJsonArray nodes;
        for (const auto &node : flow.nodes)
        {
            const bool  unresolved = !node.unresolved.isEmpty();
            QJsonObject raw        = node.unresolved;
            raw.insert("node_seq", QString::number(node.id));
            raw.insert("tool_id", QString::fromStdString(node.toolId));
            raw.insert("tool_name_snapshot", QString::fromStdString(node.toolNameSnapshot));
            raw.insert("alias", QString::fromStdString(node.alias));
            raw.insert("pos", QJsonArray{node.position.x, node.position.y});
            if (!unresolved)
            {
                QJsonObject parameters;
                for (const auto &[id, value] : node.initParams)
                {
                    const auto signature = node.signatures.initParams.find(id);
                    if (signature == node.signatures.initParams.end())
                        invalid("Cannot save parameter without type_snapshot: " + id);
                    parameters.insert(QString::fromStdString(id), ValueJson::encode(value, signature->second));
                }
                raw.insert("init_params", parameters);
                raw.insert("type_snapshot", QJsonObject{
                                                {"init_params", encodeTypes(node.signatures.initParams)},
                                                {     "inputs",     encodeTypes(node.signatures.inputs)},
                                                {    "outputs",    encodeTypes(node.signatures.outputs)}
                });
            }
            auto inputs = unresolved ? raw.value("inputs").toObject() : QJsonObject{};
            for (const auto &[id, binding] : node.inputs)
            {
                const auto key   = QString::fromStdString(id);
                auto       saved = inputs.value(key).toObject();
                saved.insert("mode", binding.mode == BindingMode::Upstream ? "upstream" : "literal");
                if (!unresolved)
                {
                    const auto signature = node.signatures.inputs.find(id);
                    if (signature == node.signatures.inputs.end())
                        invalid("Cannot save input without type_snapshot: " + id);
                    saved.insert("literal", ValueJson::encode(binding.literal, signature->second));
                }
                if (binding.mode == BindingMode::Upstream && binding.source)
                    saved.insert("source", QJsonObject{
                                               {"node_seq",        QString::number(binding.source->node)},
                                               { "port_id", QString::fromStdString(binding.source->port)}
                    });
                else
                    saved.remove("source");
                inputs.insert(key, saved);
            }
            raw.insert("inputs", inputs);
            nodes.append(raw);
        }
        flows.append(QJsonObject{
            {     "flow_id",   QString::fromStdString(flow.id)},
            {        "name", QString::fromStdString(flow.name)},
            {"run_selected",                  flow.runSelected},
            {       "nodes",                             nodes}
        });
    }
    return {
        {"format_version",                                                                                      2},
        {       "project", QJsonObject{{"name", QString::fromStdString(project.name)},
 {"next_node_seq", QString::number(project.nextNodeId)},
 {"next_flow_seq", QString::number(project.nextFlowId)}}                               },
        {         "flows",                                                                                  flows}
    };
}

void rewritePath(PortValue &value, const PortDefinition &port, const std::filesystem::path &oldDirectory,
                 const std::filesystem::path &newDirectory)
{
    if (!isPathPort(port))
        return;
    auto *string = std::get_if<Utf8>(&value);
    if (!string || string->str().empty())
        return;
    const auto original = std::filesystem::u8path(string->str());
    if (original.is_relative() && oldDirectory.empty())
        return;
    const auto absolute = (original.is_absolute() ? original : oldDirectory / original).lexically_normal();
    const auto relative = absolute.lexically_relative(newDirectory);
    value               = Utf8(utf8Path(relative.empty() ? absolute : relative));
}
} // namespace

ProjectLoadResult ProjectStorage::load(const std::filesystem::path &path) const
{
    const auto absolute = std::filesystem::absolute(path).lexically_normal();
    QFile      file(qtPath(absolute));
    if (!file.open(QIODevice::ReadOnly))
        invalid("Cannot open project " + utf8Path(absolute) + ": " + file.errorString().toStdString());
    const auto bytes = file.readAll();
    if (file.error() != QFileDevice::NoError)
        invalid("Cannot read project " + utf8Path(absolute) + ": " + file.errorString().toStdString());
    try
    {
        const ValueJson::Document document(bytes);
        auto                      result = decodeProject(document, registry_);
        result.project.path              = absolute;
        return result;
    }
    catch (const std::exception &error)
    {
        invalid("工程格式不受支持，请使用 JSON 工程：" + utf8Path(absolute) + ": " + error.what());
    }
}

ProjectLoadResult ProjectStorage::reconcile(const ProjectDefinition &project) const
{
    const ValueJson::Document document(QJsonDocument(encodeProject(project)).toJson(QJsonDocument::Compact));
    auto                      result = decodeProject(document, registry_);
    result.project.path              = project.path;
    result.project.dirty             = result.project.dirty || project.dirty;
    for (std::size_t f = 0; f < project.flows.size(); ++f)
    {
        auto &updatedFlow             = result.project.flows[f];
        updatedFlow.structureRevision = project.flows[f].structureRevision;
        bool changedStructure         = false;
        for (std::size_t n = 0; n < project.flows[f].nodes.size(); ++n)
        {
            auto       &updated        = updatedFlow.nodes[n];
            const auto &original       = project.flows[f].nodes[n];
            updated.executionRevision  = original.executionRevision;
            const bool contractChanged = original.unresolved.isEmpty() != updated.unresolved.isEmpty()
                                      || original.signatures.initParams != updated.signatures.initParams
                                      || original.signatures.inputs != updated.signatures.inputs
                                      || original.signatures.outputs != updated.signatures.outputs;
            changedStructure      = changedStructure || contractChanged;
            bool changedExecution = contractChanged;
            for (const auto &[id, binding] : updated.inputs)
            {
                const auto previous = original.inputs.find(id);
                if (previous == original.inputs.end())
                    continue;
                const auto effectiveSource = [](const InputBinding &value)
                {
                    return value.mode == BindingMode::Upstream ? value.source : std::nullopt;
                };
                if (effectiveSource(previous->second) != effectiveSource(binding))
                {
                    changedStructure = true;
                    changedExecution = true;
                }
            }
            for (const auto &diagnostic : result.diagnostics)
            {
                if (diagnostic.node != updated.id || diagnostic.code != ProjectStorageCode::SchemaChanged)
                    continue;
                if (diagnostic.field.starts_with("init_params."))
                    changedExecution = true;
                if (diagnostic.field.starts_with("inputs."))
                {
                    const auto input = updated.inputs.find(diagnostic.field.substr(7));
                    if (input != updated.inputs.end() && input->second.mode == BindingMode::Literal)
                        changedExecution = true;
                }
            }
            if (changedExecution)
                ++updated.executionRevision;
        }
        if (changedStructure)
            ++updatedFlow.structureRevision;
    }
    return result;
}

std::vector<ProjectStorageDiagnostic> ProjectStorage::save(ProjectDefinition           &project,
                                                           const std::filesystem::path &destination) const
{
    auto target = destination.empty() ? project.path : destination;
    if (target.empty())
        invalid("An unnamed project requires a save destination");
    if (!target.has_extension())
        target += ".vrt.json";
    if (target.extension() != ".json")
        invalid("工程格式不受支持，请使用 JSON 工程");
    target                                          = std::filesystem::absolute(target).lexically_normal();
    ProjectDefinition                     candidate = project;
    std::vector<ProjectStorageDiagnostic> diagnostics;
    const auto                            oldDirectory = project.path.empty()
                                                           ? std::filesystem::path{}
                                                           : std::filesystem::absolute(project.path).lexically_normal().parent_path();
    if (!project.path.empty() && target != std::filesystem::absolute(project.path).lexically_normal())
    {
        for (auto &flow : candidate.flows)
        {
            for (auto &node : flow.nodes)
            {
                const auto *definition = registry_.find(node.toolId);
                if (!definition || !node.unresolved.isEmpty())
                {
                    diagnostics.push_back(
                        {ProjectStorageCode::UnknownPaths,
                         flow.id,
                         node.id,
                         {},
                         "Unknown tool fields were retained; relative paths may need repair after Save As"});
                    continue;
                }
                for (const auto &port : definition->initParams)
                {
                    const auto value = node.initParams.find(port.id);
                    if (value != node.initParams.end())
                        rewritePath(value->second, port, oldDirectory, target.parent_path());
                }
                for (const auto &port : definition->inputs)
                {
                    const auto value = node.inputs.find(port.id);
                    if (value != node.inputs.end())
                        rewritePath(value->second.literal, port, oldDirectory, target.parent_path());
                }
            }
        }
    }
    candidate.path        = target;
    candidate.dirty       = false;
    const auto serialized = QJsonDocument(encodeProject(candidate)).toJson(QJsonDocument::Indented);
    QSaveFile  file(qtPath(target));
    file.setDirectWriteFallback(false);
    if (!file.open(QIODevice::WriteOnly))
        invalid("Cannot save project " + utf8Path(target) + ": " + file.errorString().toStdString());
    if (file.write(serialized) != serialized.size())
    {
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
