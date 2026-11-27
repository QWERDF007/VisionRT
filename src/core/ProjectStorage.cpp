#include "core/ProjectStorage.h"

#include "common/Version.h"
#include "core/InputNormalizer.h"
#include "core/JsonFields.h"
#include "core/NodeContracts.h"
#include "core/ProjectArchive.h"
#include "core/ProjectEditor.h"
#include "core/Sequence.h"
#include "core/ToolRegistry.h"
#include "core/ToolValueValidation.h"
#include "core/ValueJson.h"

#include <QJsonArray>
#include <QJsonDocument>
#include <QString>
#include <algorithm>
#include <charconv>
#include <cmath>
#include <limits>
#include <stdexcept>
#include <string_view>
#include <tuple>

namespace visionrt {
namespace {
constexpr int projectFormatVersion = 10;

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
    throw ContractError(message);
}

QJsonObject map(const QJsonValue &node, std::string_view context)
{
    if (!node.isObject())
        invalid(std::string(context) + " 必须是 JSON 对象");
    return node.toObject();
}

std::string text(const QJsonValue &node, std::string_view context, bool nonempty = false)
{
    if (!node.isString())
        invalid(std::string(context) + " 必须是字符串");
    auto value = node.toString().toStdString();
    validateUtf8(value);
    if (nonempty
        && (value.empty()
            || std::any_of(value.begin(), value.end(), [](unsigned char c) { return c < 0x20 || c == 0x7f; })))
        invalid(std::string(context) + " 必须是非空标识符，且不能包含控制字符");
    return value;
}

std::uint64_t sequence(const QJsonValue &node, std::string_view context)
{
    return parseSequence(text(node, context, true), context);
}

PortId portId(const QJsonValue &value, std::string_view context)
{
    const auto number = value.toDouble(-1.0);
    if (!value.isDouble() || number < 0.0 || number > 4294967295.0 || number != std::floor(number))
        invalid(std::string(context) + " 必须是非负 32 位整数");
    return static_cast<PortId>(number);
}

BindingMode decodeBindingMode(const QJsonValue &value, std::string_view context)
{
    const auto name = text(value, context, true);
    if (name == "literal")
        return BindingMode::Literal;
    if (name == "upstream")
        return BindingMode::Upstream;
    invalid(std::string(context) + " 未知绑定模式：" + name);
}

const char *stageWireName(PortStage stage)
{
    switch (stage)
    {
    case PortStage::Init:
        return "init";
    case PortStage::Train:
        return "train";
    case PortStage::Inputs:
        return "inputs";
    case PortStage::Outputs:
        break;
    }
    invalid("不支持的参数绑定阶段");
}

PortStage decodeTargetStage(const QJsonValue &value, std::string_view context)
{
    const auto name = text(value, context, true);
    if (name == "init")
        return PortStage::Init;
    if (name == "train")
        return PortStage::Train;
    if (name == "inputs")
        return PortStage::Inputs;
    invalid(std::string(context) + " 未知目标阶段：" + name);
}

const char *stageDisplayName(PortStage stage)
{
    switch (stage)
    {
    case PortStage::Init:
        return "初始化参数";
    case PortStage::Train:
        return "训练参数";
    case PortStage::Inputs:
        return "输入";
    case PortStage::Outputs:
        return "输出";
    }
    return "参数";
}

TypeMap types(const std::vector<PortDefinition> &ports)
{
    TypeMap result;
    for (const auto &port : ports)
        result.emplace(port.id, port.type);
    return result;
}

void diagnose(ProjectLoadResult &result, ProjectStorageCode code, const FlowDefinition &flow,
              const NodeDefinition &node, std::string field, std::string message, bool changed = false)
{
    result.diagnostics.push_back({code, flow.id, node.id, std::move(field), std::move(message)});
    result.project.dirty = result.project.dirty || changed;
}

void rangeWarning(ProjectLoadResult &result, const FlowDefinition &flow, const NodeDefinition &node,
                  const PortDefinition &port, PortValue &value)
{
    if (isMissing(value))
        return;
    try
    {
        auto normalized = normalizeInput(port, value);
        if (!equalValues(normalized.value, value))
        {
            value = std::move(normalized.value);
            diagnose(result, ProjectStorageCode::RangeWarning, flow, node, portLabel(port.id),
                     "保存的值不符合当前约束，已按声明收敛：" + normalized.adjustment, true);
        }
    }
    catch (const ContractError &error)
    {
        diagnose(result, ProjectStorageCode::RangeWarning, flow, node, portLabel(port.id),
                 "保存的值不符合当前约束，已保留原值：" + std::string(error.what()));
    }
}

PortValue defaultValue(const PortDefinition &port)
{
    validateValue(port.type, port.defaultValue);
    return copyProjectLiteral(port.defaultValue);
}

struct SavedStageParams
{
    TypeMap          types;
    std::set<PortId> literals;
};

struct SavedNodeParams
{
    SavedStageParams init;
    SavedStageParams train;
    SavedStageParams inputs;

    SavedStageParams &forStage(PortStage stage)
    {
        switch (stage)
        {
        case PortStage::Init:
            return init;
        case PortStage::Train:
            return train;
        case PortStage::Inputs:
            return inputs;
        case PortStage::Outputs:
            break;
        }
        invalid("不支持的参数阶段");
    }

    const SavedStageParams &forStage(PortStage stage) const
    {
        return const_cast<SavedNodeParams *>(this)->forStage(stage);
    }
};

using StagePortKey = std::pair<PortStage, PortId>;

struct NodeReconciliation
{
    std::map<StagePortKey, SourcePort> removedInputs;
    bool                               declarationChanged = false;
};

void reconcileStageBindings(ProjectLoadResult &result, const FlowDefinition &flow, NodeDefinition &node,
                            PortStage stage, const std::vector<PortDefinition> &activePorts,
                            const TypeMap &declaredTypes, const SavedStageParams &saved,
                            NodeReconciliation &changes, bool &schemaChanged,
                            bool preserveAllRemoved = false)
{
    const std::string prefix = std::string(stageWireName(stage)) + ".";
    const std::string label  = stageDisplayName(stage);
    const auto       &stored = node.stageBindings(stage);
    BindingMap        reconciled;
    for (const auto &port : activePorts)
    {
        const auto   old     = stored.find(port.id);
        const auto   oldType = saved.types.find(port.id);
        const bool   hasLit  = saved.literals.contains(port.id);
        InputBinding binding;
        if (old != stored.end())
        {
            binding = old->second;
            if (hasLit)
            {
                if (oldType != saved.types.end() && compatible(oldType->second, port.type))
                {
                    if (binding.mode == BindingMode::Literal || !binding.source)
                    {
                        if (stage == PortStage::Train)
                        {
                            try
                            {
                                validateToolValue(port, binding.literal);
                            }
                            catch (const ContractError &error)
                            {
                                binding.literal = defaultValue(port);
                                schemaChanged   = true;
                                diagnose(result, ProjectStorageCode::SchemaChanged, flow, node,
                                         prefix + portLabel(port.id),
                                         label + "不符合当前声明约束，已重置为默认值：" + std::string(error.what()),
                                         true);
                            }
                        }
                        else
                        {
                            rangeWarning(result, flow, node, port, binding.literal);
                        }
                    }
                }
                else
                {
                    binding.literal = defaultValue(port);
                    if (!(stage == PortStage::Inputs && isReservedPort(PortStage::Inputs, port.id)))
                    {
                        schemaChanged = true;
                        diagnose(result, ProjectStorageCode::SchemaChanged, flow, node, prefix + portLabel(port.id),
                                 label + "与当前声明类型不兼容，字面量已重置为声明默认值", true);
                    }
                }
            }
            else
            {
                binding.literal = defaultValue(port);
            }
        }
        else
        {
            binding.literal = defaultValue(port);
            if (port.type.type == Type::Image || isMissing(binding.literal))
                binding.mode = BindingMode::Upstream;
            if (!(stage == PortStage::Inputs && isReservedPort(PortStage::Inputs, port.id)))
            {
                schemaChanged = true;
                diagnose(result, ProjectStorageCode::SchemaChanged, flow, node, prefix + portLabel(port.id),
                         "当前声明新增此" + label + "，已填入声明默认值", true);
            }
        }
        if (port.type.type == Type::Image && binding.mode != BindingMode::Upstream)
            binding.mode = BindingMode::Upstream;
        reconciled.emplace(port.id, std::move(binding));
    }

    for (const auto &[id, binding] : stored)
    {
        if (preserveAllRemoved || !declaredTypes.contains(id))
        {
            if (binding.mode == BindingMode::Upstream && binding.source)
                changes.removedInputs.emplace(StagePortKey{stage, id}, *binding.source);
            schemaChanged = true;
            diagnose(result, ProjectStorageCode::SchemaChanged, flow, node, prefix + portLabel(id),
                     "已丢弃声明中不再存在的" + label + "及其绑定", true);
        }
    }

    node.stageBindings(stage) = std::move(reconciled);
}

void reconcileNode(ProjectLoadResult &result, const FlowDefinition &flow, NodeDefinition &node,
                   const ToolDefinition &definition, const SavedNodeParams &saved, NodeReconciliation &changes)
{
    const auto contract   = definition.derivesPorts() ? nodeContract(node, definition) : definition;
    auto       initTypes  = types(contract.initParams);
    auto       trainTypes = types(contract.trainParams);
    auto       inputTypes = types(contract.inputs);

    if (definition.nodeModel == NodeModel::Condition && conditionUsesUpstream(node) && node.expression)
        for (const auto &variable : node.expression->variables)
            if (const auto found = saved.inputs.types.find(variable.portId); found != saved.inputs.types.end())
                inputTypes.insert_or_assign(variable.portId, found->second);

    setOutputDeclarations(node, definition);

    bool schemaChanged = false;
    reconcileStageBindings(result, flow, node, PortStage::Train, contract.trainParams, trainTypes, saved.train,
                           changes, schemaChanged);
    const auto activeInitPorts = definition.derivesPorts() ? std::vector<PortDefinition>{} : contract.initParams;
    reconcileStageBindings(result, flow, node, PortStage::Init, activeInitPorts, initTypes, saved.init, changes,
                           schemaChanged, definition.derivesPorts());

    const auto oldInputs = node.inputs;
    reconcileStageBindings(result, flow, node, PortStage::Inputs, contract.inputs, inputTypes, saved.inputs, changes,
                           schemaChanged);

    // Preserve inactive formula variables on an upstream-mode condition node.
    if (definition.nodeModel == NodeModel::Condition && conditionUsesUpstream(node) && node.expression)
    {
        for (const auto &variable : node.expression->variables)
        {
            if (node.inputs.contains(variable.portId))
                continue;
            const auto old     = oldInputs.find(variable.portId);
            const auto oldType = saved.inputs.types.find(variable.portId);
            if (old != oldInputs.end() && oldType != saved.inputs.types.end()
                && compatible(oldType->second, variable.type))
                node.inputs.emplace(variable.portId, old->second);
        }
    }

    changes.declarationChanged = schemaChanged || (node.toolVersionSnapshot != definition.version);

    node.signatures.trainParams = std::move(trainTypes);
    node.signatures.initParams  = std::move(initTypes);
    node.signatures.inputs      = std::move(inputTypes);

    if (node.toolNameSnapshot != definition.nameZh)
    {
        node.toolNameSnapshot = definition.nameZh;
        diagnose(result, ProjectStorageCode::SchemaChanged, flow, node, "tool.name", "工具显示名称快照已更新", true);
    }
    node.toolVersionSnapshot = definition.version;
}

void diagnoseCycles(ProjectLoadResult &result, const FlowDefinition &flow)
{
    std::map<NodeId, std::size_t> indices;
    for (std::size_t i = 0; i < flow.nodes.size(); ++i)
        indices.emplace(flow.nodes[i].id, i);
    std::vector<std::vector<std::size_t>> edges(flow.nodes.size());
    for (std::size_t i = 0; i < flow.nodes.size(); ++i)
    {
        for (const auto stage : kParameterStages)
        {
            for (const auto &[id, binding] : flow.nodes[i].stageBindings(stage))
            {
                if (binding.mode != BindingMode::Upstream || !binding.source)
                    continue;
                const auto found = indices.find(binding.source->node);
                if (found != indices.end())
                    edges[i].push_back(found->second);
            }
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
                         "节点参与上游依赖循环，绑定已保留");
    };
    for (std::size_t i = 0; i < flow.nodes.size(); ++i)
        if (discovery[i] == unseen)
            visit(visit, i);
}

ProjectLoadResult decodeProject(const ValueJson::Document &document, const ToolRegistry &registry)
{
    if (!document.json().isObject())
        invalid("工程 JSON 根节点必须是对象");
    const auto root = document.json().object();
    json::fields(root, {"format_version", "project", "flows"}, "root");
    if (!root.value("format_version").isDouble())
        invalid("format_version 必须是整数");
    const auto version = std::get<std::int64_t>(document.decode({"format_version"}, {Type::Int}));
    if (version != projectFormatVersion)
        invalid("format_version 不受支持，要求 " + std::to_string(projectFormatVersion)
                + "，实际 " + std::to_string(version));

    const auto metadata = map(root.value("project"), "project");
    json::fields(metadata, {"name", "notes", "app_version", "contracts_version", "next_node_seq", "next_flow_seq"},
                 "project");
    if (!root.value("flows").isArray())
        invalid("flows 必须是数组");

    ProjectLoadResult result;
    auto             &project = result.project;
    project.flows.clear();
    project.dirty = false;
    project.name  = text(metadata.value("name"), "project.name");
    if (metadata.contains("notes"))
        project.notes = text(metadata.value("notes"), "project.notes");
    if (metadata.contains("app_version"))
        project.appVersion = text(metadata.value("app_version"), "project.app_version");
    if (metadata.contains("contracts_version"))
        project.contractsVersion = text(metadata.value("contracts_version"), "project.contracts_version");
    project.nextNodeId = sequence(metadata.value("next_node_seq"), "next_node_seq");
    project.nextFlowId = sequence(metadata.value("next_flow_seq"), "next_flow_seq");

    std::set<std::string, std::less<>>   flowIds, flowNames;
    std::set<NodeId>                     nodeIds;
    std::map<NodeId, NodeReconciliation> reconciliations;
    NodeId                               minimumNode = 0;
    std::uint64_t                        minimumFlow = 1;
    const auto                           flows       = root.value("flows").toArray();

    const auto updateFor = [&result](const ToolDefinition &definition, const std::string &savedVersion) -> ToolUpdate &
    {
        for (auto &update : result.toolUpdates)
            if (update.toolId == definition.nameEn)
            {
                if (update.savedVersion.empty())
                    update.savedVersion = savedVersion;
                return update;
            }
        result.toolUpdates.push_back(
            {definition.nameEn, definition.nameZh, savedVersion, definition.version, {}, 0, {}});
        return result.toolUpdates.back();
    };
    const auto findUpdate = [&result](std::string_view toolId) -> ToolUpdate *
    {
        for (auto &update : result.toolUpdates)
            if (update.toolId == toolId)
                return &update;
        return nullptr;
    };
    const auto portName = [&registry](const NodeDefinition &owner, PortStage stage, PortId port) -> std::string
    {
        const auto *definition = registry.find(owner.toolId);
        if (!definition)
            return portLabel(port);
        const auto  contract = definition->derivesPorts() ? nodeContract(owner, *definition) : *definition;
        const auto &ports    = stage == PortStage::Init   ? contract.initParams
                             : stage == PortStage::Train  ? contract.trainParams
                             : stage == PortStage::Inputs ? contract.inputs
                                                          : contract.outputs;
        const auto found = std::find_if(ports.begin(), ports.end(),
                                        [port](const PortDefinition &item) { return item.id == port; });
        return found == ports.end() ? portLabel(port) : found->nameZh;
    };

    for (qsizetype flowIndex = 0; flowIndex < flows.size(); ++flowIndex)
    {
        const auto rawFlow = map(flows[flowIndex], "flow");
        json::fields(rawFlow, {"id", "name", "run_selected", "nodes", "layout", "connections"}, "flow");
        FlowDefinition flow;
        flow.id = text(rawFlow.value("id"), "flow.id", true);
        if (!flowIds.insert(flow.id).second)
            invalid("flow.id 重复：" + flow.id);
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
                    result.diagnostics.push_back(
                        {ProjectStorageCode::SequenceExhausted, flow.id, {}, {}, "流程标识序号已耗尽，无法新建流程"});
            }
        }
        flow.name = text(rawFlow.value("name"), "flow.name");
        if (!flowNames.insert(flow.name).second)
            result.diagnostics.push_back(
                {ProjectStorageCode::DuplicateFlowName, flow.id, {}, {}, "流程显示名称重复，按名称查找前请先重命名"});
        if (!rawFlow.value("run_selected").isBool())
            invalid("run_selected 必须是布尔值");
        flow.runSelected = rawFlow.value("run_selected").toBool();
        if (!rawFlow.value("nodes").isArray())
            invalid("flow.nodes 必须是数组");
        if (!rawFlow.value("layout").isArray())
            invalid("flow.layout 必须是数组");
        if (!rawFlow.value("connections").isArray())
            invalid("flow.connections 必须是数组");

        const auto                   nodes = rawFlow.value("nodes").toArray();
        std::vector<QJsonObject>     rawNodes;
        std::vector<SavedNodeParams> savedParams;
        rawNodes.reserve(static_cast<std::size_t>(nodes.size()));
        savedParams.reserve(static_cast<std::size_t>(nodes.size()));
        flow.nodes.reserve(static_cast<std::size_t>(nodes.size()));
        std::map<NodeId, std::size_t> flowNodeIndex;

        for (qsizetype nodeIndex = 0; nodeIndex < nodes.size(); ++nodeIndex)
        {
            const auto        raw = map(nodes[nodeIndex], "node");
            json::fields(raw, {"id", "tool", "alias", "params", "expression", "select_type"}, "node");
            const QStringList nodePath{"flows", QString::number(flowIndex), "nodes", QString::number(nodeIndex)};
            const auto        literal = [&](const QStringList &path, const TypeSpec &type)
            {
                return document.decode(nodePath + path, type);
            };

            NodeDefinition  node;
            SavedNodeParams saved;
            node.id = sequence(raw.value("id"), "node.id");
            if (!nodeIds.insert(node.id).second)
                invalid("工程内 node.id 重复：" + std::to_string(node.id));
            const bool exhausted = node.id == std::numeric_limits<NodeId>::max();
            minimumNode          = std::max(minimumNode, exhausted ? node.id : node.id + 1);
            if (exhausted)
                diagnose(result, ProjectStorageCode::SequenceExhausted, flow, node, "id",
                         "节点标识序号已耗尽，无法新建节点");

            const auto toolObj = map(raw.value("tool"), "node.tool");
            json::fields(toolObj, {"id", "name", "version"}, "node.tool");
            node.toolId              = text(toolObj.value("id"), "node.tool.id", true);
            node.toolNameSnapshot    = text(toolObj.value("name"), "node.tool.name");
            node.toolVersionSnapshot = text(toolObj.value("version"), "node.tool.version", true);

            if (raw.contains("alias"))
                node.alias = text(raw.value("alias"), "node.alias");

            if (raw.contains("params"))
            {
                const auto paramsObj = map(raw.value("params"), "node.params");
                json::fields(paramsObj, {"init", "train", "inputs"}, "node.params");

                const auto decodeStageBindings = [&](PortStage stage)
                {
                    const auto wire = QString::fromLatin1(stageWireName(stage));
                    if (!paramsObj.contains(wire))
                        return;
                    const std::string key = stageWireName(stage);
                    if (!paramsObj.value(wire).isArray())
                        invalid("node.params." + key + " 必须是数组");
                    const auto items       = paramsObj.value(wire).toArray();
                    auto      &stageSaved  = saved.forStage(stage);
                    auto      &stageTarget = node.stageBindings(stage);
                    for (qsizetype i = 0; i < items.size(); ++i)
                    {
                        const auto entry = map(items[i], key + " parameter");
                        json::fields(entry, {"port", "type", "value", "mode"}, wire + QStringLiteral(" parameter"));
                        const auto id   = portId(entry.value("port"), key + ".port");
                        const auto type = ValueJson::decodeType(entry.value("type"));
                        if (!stageSaved.types.emplace(id, type).second)
                            invalid(std::string(stageDisplayName(stage)) + "编号重复：" + portLabel(id));
                        InputBinding binding;
                        if (entry.contains("mode"))
                            binding.mode = decodeBindingMode(entry.value("mode"), key + ".mode");
                        binding.literal = literal({"params", wire, QString::number(i), "value"}, type);
                        stageSaved.literals.insert(id);
                        stageTarget.emplace(id, std::move(binding));
                    }
                };
                for (const auto stage : kParameterStages)
                    decodeStageBindings(stage);
            }

            if (raw.contains("expression"))
            {
                const auto config = map(raw.value("expression"), "expression");
                json::fields(config, {"next_port_id", "variables", "program"}, "expression");
                ExpressionConfig expression;
                expression.nextPortId = sequence(config.value("next_port_id"), "next_port_id");
                if (!config.value("variables").isArray() || !config.value("program").isArray())
                    invalid("expression.variables 与 program 必须是数组");
                const auto variables = config.value("variables").toArray();
                for (qsizetype index = 0; index < variables.size(); ++index)
                {
                    const auto item = map(variables[index], "expression variable");
                    json::fields(item, {"port", "name", "symbol", "type", "allow_missing"}, "expression variable");
                    ExpressionVariable variable;
                    variable.portId = portId(item.value("port"), "variable.port");
                    variable.name   = text(item.value("name"), "variable.name", true);
                    variable.symbol = text(item.value("symbol"), "variable.symbol", true);
                    if (!item.value("allow_missing").isBool())
                        invalid("variable.allow_missing 必须是布尔值");
                    variable.allowMissing = item.value("allow_missing").toBool();
                    variable.type         = ValueJson::decodeType(item.value("type"));
                    expression.variables.push_back(std::move(variable));
                }
                const auto program = config.value("program").toArray();
                for (qsizetype index = 0; index < program.size(); ++index)
                {
                    const auto item = map(program[index], "expression result");
                    json::fields(item, {"port", "name", "symbol", "expression", "type", "exposed"}, "expression result");
                    ExpressionResult entry;
                    entry.portId     = portId(item.value("port"), "result.port");
                    entry.name       = text(item.value("name"), "result.name", true);
                    entry.symbol     = text(item.value("symbol"), "result.symbol", true);
                    entry.expression = text(item.value("expression"), "result.expression", true);
                    if (!item.value("exposed").isBool())
                        invalid("result.exposed 必须是布尔值");
                    entry.exposed = item.value("exposed").toBool();
                    entry.type    = ValueJson::decodeType(item.value("type"));
                    expression.program.push_back(std::move(entry));
                }
                node.expression = std::move(expression);
            }

            if (raw.contains("select_type"))
                node.selectType = ValueJson::decodeType(raw.value("select_type"));

            flowNodeIndex.emplace(node.id, flow.nodes.size());
            flow.nodes.push_back(std::move(node));
            rawNodes.push_back(raw);
            savedParams.push_back(std::move(saved));
        }

        // Apply layout.
        const auto                            layoutItems = rawFlow.value("layout").toArray();
        std::set<NodeId>                      laidOut;
        std::map<std::pair<int, int>, NodeId> positions;
        for (qsizetype i = 0; i < layoutItems.size(); ++i)
        {
            const auto item = map(layoutItems[i], "layout");
            json::fields(item, {"node", "col", "row"}, "layout");
            const auto id    = sequence(item.value("node"), "layout.node");
            const auto found = flowNodeIndex.find(id);
            if (found == flowNodeIndex.end())
                invalid("layout 引用了流程内不存在的节点：" + std::to_string(id));
            if (!laidOut.insert(id).second)
                invalid("layout 节点重复：" + std::to_string(id));
            const QStringList itemPath{"flows", QString::number(flowIndex), "layout", QString::number(i)};
            const auto col = std::get<std::int64_t>(document.decode(itemPath + QStringList{"col"}, {Type::Int}));
            const auto row = std::get<std::int64_t>(document.decode(itemPath + QStringList{"row"}, {Type::Int}));
            if (col < std::numeric_limits<int>::min() || col > std::numeric_limits<int>::max()
                || row < std::numeric_limits<int>::min() || row > std::numeric_limits<int>::max())
                invalid("layout 坐标超出有符号整数范围");
            auto &node                      = flow.nodes[found->second];
            node.position                   = {static_cast<int>(col), static_cast<int>(row)};
            const auto [position, inserted] = positions.emplace(std::pair{node.position.x, node.position.y}, node.id);
            if (!inserted)
                diagnose(result, ProjectStorageCode::LayoutConflict, flow, node, "layout",
                         "节点与节点 " + std::to_string(position->second) + " 重叠，位置已保留");
        }
        for (const auto &node : flow.nodes)
            if (!laidOut.contains(node.id))
                invalid("layout 缺少节点坐标：" + std::to_string(node.id));

        // Apply connections before reconciling nodes so Condition upstream mode is known.
        const auto                                      connectionItems = rawFlow.value("connections").toArray();
        std::set<std::tuple<NodeId, PortStage, PortId>> connectedTargets;
        for (qsizetype i = 0; i < connectionItems.size(); ++i)
        {
            const auto item = map(connectionItems[i], "connection");
            json::fields(item, {"from", "to"}, "connection");
            const auto fromObj = map(item.value("from"), "connection.from");
            const auto toObj   = map(item.value("to"), "connection.to");
            json::fields(fromObj, {"node", "port"}, "connection.from");
            json::fields(toObj, {"node", "stage", "port"}, "connection.to");
            const auto fromNode = sequence(fromObj.value("node"), "connection.from.node");
            const auto fromPort = portId(fromObj.value("port"), "connection.from.port");
            const auto toNode   = sequence(toObj.value("node"), "connection.to.node");
            const auto toStage  = decodeTargetStage(toObj.value("stage"), "connection.to.stage");
            const auto toPort   = portId(toObj.value("port"), "connection.to.port");
            const auto found    = flowNodeIndex.find(toNode);
            if (found == flowNodeIndex.end())
                invalid("connections 目标节点不在当前流程中：" + std::to_string(toNode));
            if (!connectedTargets.emplace(toNode, toStage, toPort).second)
                invalid("connections 目标端口存在重复连线：" + std::to_string(toNode) + "." + stageWireName(toStage)
                        + "." + portLabel(toPort));
            auto &binding  = flow.nodes[found->second].stageBindings(toStage)[toPort];
            binding.mode   = BindingMode::Upstream;
            binding.source = SourcePort{fromNode, fromPort};
        }

        // Reconcile every node against the current tool registry.
        for (std::size_t idx = 0; idx < flow.nodes.size(); ++idx)
        {
            auto       &node       = flow.nodes[idx];
            const auto *definition = registry.find(node.toolId);
            if (!definition)
            {
                node.unresolved             = rawNodes[idx];
                node.signatures.initParams  = savedParams[idx].init.types;
                node.signatures.trainParams = savedParams[idx].train.types;
                node.signatures.inputs      = savedParams[idx].inputs.types;
                diagnose(result, ProjectStorageCode::DefinitionMissing, flow, node, {},
                         "工具定义缺失，节点不可执行，请删除该节点或安装对应插件：" + node.toolId);
            }
            else
            {
                const auto savedVersion = node.toolVersionSnapshot;
                auto      &changes      = reconciliations[node.id];
                reconcileNode(result, flow, node, *definition, savedParams[idx], changes);
                if (changes.declarationChanged)
                    updateFor(*definition, savedVersion).nodes.push_back(node.title());
            }
        }

        project.flows.push_back(std::move(flow));
    }

    if (project.nextNodeId < minimumNode || project.nextFlowId < minimumFlow)
    {
        project.nextNodeId = std::max(project.nextNodeId, minimumNode);
        project.nextFlowId = std::max(project.nextFlowId, minimumFlow);
        project.dirty      = true;
        result.diagnostics.push_back({ProjectStorageCode::CounterRepaired, {}, {}, {}, "标识分配计数已按现有标识修复"});
    }

    const bool dirtyBeforeResolve = project.dirty;
    resolveOutputTypes(project);
    project.dirty = dirtyBeforeResolve;

    for (auto &flow : project.flows)
    {
        for (const auto &node : flow.nodes)
            for (const auto &[port, message] : node.outputTypeErrors)
                diagnose(result, ProjectStorageCode::InvalidReference, flow, node,
                         "outputs." + portLabel(port), message);

        std::map<NodeId, const NodeDefinition *> nodes;
        for (const auto &node : flow.nodes)
            nodes.emplace(node.id, &node);

        for (auto &node : flow.nodes)
        {
            if (!node.unresolved.isEmpty())
                continue;
            auto sources = reconciliations[node.id].removedInputs;
            for (const auto stage : kParameterStages)
                for (const auto &[id, binding] : node.stageBindings(stage))
                    if (binding.mode == BindingMode::Upstream && binding.source)
                        sources.emplace(StagePortKey{stage, id}, *binding.source);

            for (const auto &[key, upstream] : sources)
            {
                const auto [stage, id]   = key;
                const std::string field  = std::string(stageWireName(stage)) + "." + portLabel(id);
                const auto        source = nodes.find(upstream.node);
                if (source != nodes.end() && !source->second->unresolved.isEmpty())
                {
                    diagnose(result, ProjectStorageCode::PendingReference, flow, node, field,
                             "上游工具定义缺失，引用已保留，请先处理缺失插件节点");
                    continue;
                }
                const auto  check      = validateBinding(flow, upstream.node, upstream.port, node.id, id, {}, stage);
                const auto *sourceNode = source == nodes.end() ? nullptr : source->second;
                const bool  cut = check.reason == BindingError::MissingPort || check.reason == BindingError::TypeMismatch;
                if (!cut)
                {
                    if (check.reason == BindingError::SameOrUpperRow)
                        diagnose(result, ProjectStorageCode::InvalidReference, flow, node, field,
                                 "上游节点未在当前节点上方（违反上游 Y < 下游 Y 层级约束），绑定已保留供修复，不可执行");
                    else if (!check)
                        diagnose(result, ProjectStorageCode::InvalidReference, flow, node, field,
                                 "上游不存在或跨流程，绑定已保留供修复，不可执行");
                    auto *updated = findUpdate(node.toolId);
                    if (!updated && sourceNode)
                        updated = findUpdate(sourceNode->toolId);
                    if (updated)
                        ++updated->kept;
                    continue;
                }

                const bool outputMissing = sourceNode && !sourceNode->signatures.outputs.contains(upstream.port);
                const bool inputRemoved  = reconciliations[node.id].removedInputs.contains(key);
                const NodeDefinition *owner = nullptr;
                if (outputMissing)
                    owner = sourceNode;
                else if (inputRemoved)
                    owner = &node;
                else if (sourceNode && findUpdate(sourceNode->toolId))
                    owner = sourceNode;
                else
                    owner = &node;

                ToolUpdate *update = findUpdate(owner->toolId);
                if (!update)
                {
                    if (const auto *def = registry.find(owner->toolId))
                    {
                        update = &updateFor(*def, owner->toolVersionSnapshot);
                        if (std::find(update->nodes.begin(), update->nodes.end(), owner->title()) == update->nodes.end())
                            update->nodes.push_back(owner->title());
                    }
                }
                if (update)
                    update->removed.push_back(
                        {sourceNode ? sourceNode->title() + "." + portName(*sourceNode, PortStage::Outputs, upstream.port)
                                    : portLabel(upstream.port),
                         node.title() + "." + portName(node, stage, id),
                         check.reason == BindingError::TypeMismatch
                             ? std::string("端口类型已变更")
                             : std::string("端口已从工具声明中删除")});

                if (const auto binding = node.stageBindings(stage).find(id); binding != node.stageBindings(stage).end())
                {
                    const auto sig     = node.signatures.stageTypes(stage).find(id);
                    const bool isImage = sig != node.signatures.stageTypes(stage).end()
                                      && sig->second.type == Type::Image;
                    binding->second.mode = isImage ? BindingMode::Upstream : BindingMode::Literal;
                    binding->second.source.reset();
                }
                diagnose(result, ProjectStorageCode::InvalidReference, flow, node, field,
                         "两端端口已无法对接（端口已删除或类型已变更），连线已断开", true);
            }
        }
        diagnoseCycles(result, flow);
    }
    return result;
}

QJsonObject encodeProject(const ProjectDefinition &project)
{
    QJsonArray flows;
    for (const auto &flow : project.flows)
    {
        QJsonArray nodes;
        QJsonArray layout;
        QJsonArray connections;

        for (const auto &node : flow.nodes)
        {
            layout.append(QJsonObject{
                {"node", QString::number(node.id)},
                { "col",           node.position.x},
                { "row",           node.position.y}
            });

            for (const auto stage : kParameterStages)
            {
                for (const auto &[id, binding] : node.stageBindings(stage))
                {
                    if (binding.mode == BindingMode::Upstream && binding.source)
                    {
                        connections.append(QJsonObject{
                            {"from", QJsonObject{{"node", QString::number(binding.source->node)},
                                                 {"port", static_cast<double>(binding.source->port)}}},
                            {  "to", QJsonObject{{"node", QString::number(node.id)},
                                                 {"stage", QString::fromLatin1(stageWireName(stage))},
                                                 {"port", static_cast<double>(id)}}}
                        });
                    }
                }
            }

            const bool unresolved = !node.unresolved.isEmpty();
            if (unresolved)
            {
                QJsonObject raw = node.unresolved;
                raw.insert("id", QString::number(node.id));
                raw.insert("alias", QString::fromStdString(node.alias));
                nodes.append(raw);
                continue;
            }

            QJsonObject entry{
                {   "id", QString::number(node.id)},
                { "tool", QJsonObject{{"id", QString::fromStdString(node.toolId)},
                                      {"name", QString::fromStdString(node.toolNameSnapshot)},
                                      {"version", QString::fromStdString(node.toolVersionSnapshot.empty()
                                                                             ? std::string("1.0.0")
                                                                             : node.toolVersionSnapshot)}}},
                {"alias", QString::fromStdString(node.alias)}
            };

            QJsonObject paramsObj;
            for (const auto stage : kParameterStages)
            {
                const auto &bindings = node.stageBindings(stage);
                if (bindings.empty())
                    continue;
                const auto &signatures = node.signatures.stageTypes(stage);
                QJsonArray  stageArr;
                for (const auto &[id, binding] : bindings)
                {
                    const auto signature = signatures.find(id);
                    if (signature == signatures.end())
                        invalid(std::string("无法保存缺少类型声明的") + stageDisplayName(stage) + "：" + portLabel(id));
                    const bool wired = binding.mode == BindingMode::Upstream && binding.source.has_value();
                    if (wired && isMissing(binding.literal))
                        continue;
                    if (stage == PortStage::Inputs && isReservedPort(PortStage::Inputs, id)
                        && binding.mode == BindingMode::Literal && isMissing(binding.literal))
                        continue;
                    QJsonObject item{
                        { "port", static_cast<double>(id)},
                        { "type", ValueJson::encodeType(signature->second)},
                        {"value", ValueJson::encode(binding.literal, signature->second, ValueJson::Purpose::Stored)}
                    };
                    if (binding.mode == BindingMode::Upstream && !binding.source)
                        item.insert("mode", QStringLiteral("upstream"));
                    stageArr.append(item);
                }
                if (!stageArr.isEmpty())
                    paramsObj.insert(QString::fromLatin1(stageWireName(stage)), stageArr);
            }

            if (!paramsObj.isEmpty())
                entry.insert("params", paramsObj);

            if (node.expression)
            {
                QJsonArray variables;
                for (const auto &variable : node.expression->variables)
                    variables.append(QJsonObject{
                        {         "port",         static_cast<double>(variable.portId)},
                        {         "name",           QString::fromStdString(variable.name)},
                        {       "symbol",         QString::fromStdString(variable.symbol)},
                        {         "type",          ValueJson::encodeType(variable.type)},
                        {"allow_missing",                          variable.allowMissing}
                    });
                QJsonArray program;
                for (const auto &res : node.expression->program)
                    program.append(QJsonObject{
                        {      "port",             static_cast<double>(res.portId)},
                        {      "name",               QString::fromStdString(res.name)},
                        {    "symbol",             QString::fromStdString(res.symbol)},
                        {"expression",         QString::fromStdString(res.expression)},
                        {      "type",              ValueJson::encodeType(res.type)},
                        {   "exposed",                                  res.exposed}
                    });
                entry.insert("expression", QJsonObject{
                                               {"next_port_id", QString::number(node.expression->nextPortId)},
                                               {   "variables",                                    variables},
                                               {     "program",                                      program}
                });
            }

            if (node.selectType)
                entry.insert("select_type", ValueJson::encodeType(*node.selectType));

            nodes.append(entry);
        }

        flows.append(QJsonObject{
            {          "id", QString::fromStdString(flow.id)},
            {        "name", QString::fromStdString(flow.name)},
            {"run_selected", flow.runSelected},
            {       "nodes", nodes},
            {      "layout", layout},
            { "connections", connections}
        });
    }

    QJsonObject projectMeta{
        {             "name", QString::fromStdString(project.name)},
        {            "notes", QString::fromStdString(project.notes)},
        {      "app_version", QString::fromStdString(project.appVersion)},
        {"contracts_version", QString::fromStdString(project.contractsVersion)},
        {    "next_node_seq", QString::number(project.nextNodeId)},
        {    "next_flow_seq", QString::number(project.nextFlowId)}
    };

    return {
        {"format_version", projectFormatVersion},
        {       "project",          projectMeta},
        {         "flows",                flows}
    };
}

} // namespace

ProjectLoadResult ProjectStorage::load(const std::filesystem::path &path) const
{
    const auto absolute  = std::filesystem::absolute(path).lexically_normal();
    const auto extension = qtPath(absolute.extension()).toLower();
    if (extension != ".vrtapp")
        invalid("工程格式不受支持，请使用 .vrtapp 工程：" + utf8Path(absolute));
    try
    {
        const auto                bytes = ProjectArchive::readProgramJson(absolute);
        const ValueJson::Document document(bytes);
        auto                      result = decodeProject(document, registry_);
        result.project.path              = absolute;
        return result;
    }
    catch (const ContractError &error)
    {
        invalid("工程 " + utf8Path(absolute) + " 无效：" + error.what());
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
            auto       &updated            = updatedFlow.nodes[n];
            const auto &original           = project.flows[f].nodes[n];
            updated.executionRevision      = original.executionRevision;
            updated.initializationRevision = original.initializationRevision;
            const bool contractChanged     = original.unresolved.isEmpty() != updated.unresolved.isEmpty()
                                          || original.signatures.initParams != updated.signatures.initParams
                                          || original.signatures.trainParams != updated.signatures.trainParams
                                          || original.signatures.inputs != updated.signatures.inputs
                                          || original.signatures.outputs != updated.signatures.outputs;
            changedStructure      = changedStructure || contractChanged;
            bool changedExecution = contractChanged;
            for (const auto stage : kParameterStages)
            {
                const auto &updatedBindings  = updated.stageBindings(stage);
                const auto &originalBindings = original.stageBindings(stage);
                if (updatedBindings.size() != originalBindings.size())
                    changedExecution = true;
                for (const auto &[id, binding] : updatedBindings)
                {
                    const auto previous = originalBindings.find(id);
                    if (previous == originalBindings.end())
                    {
                        changedExecution = true;
                        continue;
                    }
                    if (effectiveSource(previous->second) != effectiveSource(binding))
                    {
                        changedStructure = true;
                        changedExecution = true;
                    }
                    if (previous->second.mode != binding.mode
                        || (binding.mode == BindingMode::Literal
                            && !equalValues(previous->second.literal, binding.literal)))
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

std::filesystem::path projectDestination(std::filesystem::path destination)
{
    if (!destination.has_extension())
        destination += ".vrtapp";
    if (qtPath(destination.extension()).toLower() != ".vrtapp")
        invalid("工程保存路径必须使用 .vrtapp 扩展名：" + utf8Path(destination));
    return std::filesystem::absolute(destination).lexically_normal();
}

void ProjectStorage::save(ProjectDefinition &project, const std::filesystem::path &destination) const
{
    auto target = destination.empty() ? project.path : destination;
    if (target.empty())
        invalid("未命名工程必须指定保存位置");
    target                           = projectDestination(target);
    ProjectDefinition candidate      = project;
    candidate.path                   = target;
    candidate.dirty                  = false;
    candidate.appVersion             = GetVersionString();
    candidate.contractsVersion       = GetContractsVersion().version;
    const auto serialized            = QJsonDocument(encodeProject(candidate)).toJson(QJsonDocument::Indented);
    ProjectArchive::writeProgramJson(target, serialized);
    project = std::move(candidate);
}
} // namespace visionrt
