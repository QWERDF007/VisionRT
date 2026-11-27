#include "core/NodeContracts.h"

#include "core/Sequence.h"
#include "core/ToolPaths.h"
#include "visionrt/contracts/ProtocolEnums.h"
#include "visionrt/contracts/TrainingArchive.h"

#include <algorithm>
#include <functional>
#include <limits>
#include <map>
#include <set>

namespace visionrt {
bool conditionUsesUpstream(const NodeDefinition &node)
{
    const auto binding = node.inputs.find(kConditionInputPort);
    return binding != node.inputs.end() && binding->second.mode == BindingMode::Upstream;
}

TypeMap resolvedOutputTypes(const ToolDefinition &contract)
{
    TypeMap result{
        {kStatusCodePort,     {Type::Int}},
        {kStatusMessagePort,  {Type::String}}
    };
    for (const auto &port : contract.outputs)
    {
        if (port.typeFrom)
            throw ContractError("输出类型尚未解析：" + port.nameZh);
        result.insert_or_assign(port.id, port.type);
    }
    return result;
}

void setOutputDeclarations(NodeDefinition &node, const ToolDefinition &definition)
{
    // Fixed declarations keep their unresolved type_from entries here; resolution
    // happens once in resolveOutputTypes after every node is known.
    const auto &contract = definition.derivesPorts() ? nodeContract(node, definition) : definition;
    TypeMap     outputs;
    for (const auto id : {kStatusCodePort, kStatusMessagePort})
        if (const auto previous = node.signatures.outputs.find(id); previous != node.signatures.outputs.end())
            outputs.emplace(id, previous->second);
    node.outputTypeSources.clear();
    for (const auto &port : contract.outputs)
    {
        if (port.typeFrom)
        {
            node.outputTypeSources.emplace(port.id, *port.typeFrom);
            const auto previous = node.signatures.outputs.find(port.id);
            if (previous != node.signatures.outputs.end())
                outputs.emplace(port.id, previous->second);
        }
        else
            outputs.insert_or_assign(port.id, port.type);
    }
    node.signatures.outputs = std::move(outputs);
}

void resolveOutputTypes(ProjectDefinition &project)
{
    struct Resolution
    {
        TypeMap outputs;
        std::map<PortId, std::string> errors;
        const FlowDefinition *flow;
    };
    std::map<NodeId, Resolution> resolutions;
    for (const auto &flow : project.flows)
        for (const auto &node : flow.nodes)
        {
            Resolution resolution{node.signatures.outputs, {}, &flow};
            for (const auto &[port, reference] : node.outputTypeSources)
                resolution.outputs.erase(port);
            if (!resolutions.emplace(node.id, std::move(resolution)).second)
                throw ContractError("节点标识重复：" + std::to_string(node.id));
        }
    std::set<std::pair<NodeId, PortId>> resolving;
    std::function<std::optional<TypeSpec>(const NodeDefinition &, PortId)> resolve;
    resolve = [&](const NodeDefinition &node, PortId port) -> std::optional<TypeSpec>
    {
        auto &resolution = resolutions.at(node.id);
        if (!node.unresolved.isEmpty())
            return {};
        if (const auto found = resolution.outputs.find(port); found != resolution.outputs.end())
            return found->second;
        const auto reference = node.outputTypeSources.find(port);
        if (reference == node.outputTypeSources.end())
            return {};
        auto fail = [&](std::string message) -> std::optional<TypeSpec>
        {
            resolution.errors[port] = std::move(message);
            return {};
        };
        const auto key = std::pair{node.id, port};
        if (!resolving.insert(key).second)
            return fail("Output type reference contains a cycle");
        auto parameter = [&](PortId id)
        {
            const auto value = node.initParams.find(id);
            if (value == node.initParams.end() || value->second.mode != BindingMode::Literal)
                return std::string{};
            const auto *text = std::get_if<std::string>(&value->second.literal);
            return text ? *text : std::string{};
        };
        const auto flowId       = parameter(reference->second.flowParameter.id);
        const auto nodeSeq      = parameter(reference->second.nodeParameter.id);
        const auto portValue    = node.initParams.find(reference->second.portParameter.id);
        const auto *portCell    = portValue == node.initParams.end() || portValue->second.mode != BindingMode::Literal
                                      ? nullptr
                                      : std::get_if<std::int64_t>(&portValue->second.literal);
        const PortId sourcePort = portCell && *portCell > 0 ? static_cast<PortId>(*portCell) : 0;
        const bool   sourcePortOk = sourcePort != 0;
        std::optional<TypeSpec> type;
        if (flowId.empty() || nodeSeq.empty() || !sourcePortOk)
            fail("需要选择源流程、源节点和输出端口");
        else
        {
            try
            {
                const auto            sourceNode = parseSequence(nodeSeq, "源节点序号");
                const FlowDefinition *sourceFlow = nullptr;
                for (const auto &flow : project.flows)
                    if (flow.id == flowId)
                    {
                        sourceFlow = &flow;
                        break;
                    }
                if (!sourceFlow || sourceFlow == resolution.flow)
                    fail("源流程不存在或就是接收流程：" + flowId);
                else
                {
                    const NodeDefinition *source = nullptr;
                    for (const auto &candidate : sourceFlow->nodes)
                        if (candidate.id == sourceNode)
                        {
                            source = &candidate;
                            break;
                        }
                    if (!source)
                        fail("源节点不存在：" + flowId + "/" + nodeSeq);
                    else
                    {
                        type = resolve(*source, sourcePort);
                        if (!type)
                            fail("源输出端口不存在或类型无法解析：" + nodeSeq + "." + portLabel(sourcePort));
                    }
                }
            }
            catch (const ContractError &error)
            {
                fail(error.what());
            }
        }
        resolving.erase(key);
        if (type)
        {
            resolution.outputs.emplace(port, *type);
            resolution.errors.erase(port);
        }
        return type;
    };
    for (const auto &flow : project.flows)
        for (const auto &node : flow.nodes)
            for (const auto &[port, reference] : node.outputTypeSources)
                resolve(node, port);
    std::set<std::string> changedFlows;
    for (const auto &flow : project.flows)
        for (const auto &node : flow.nodes)
            if (node.signatures.outputs != resolutions.at(node.id).outputs)
            {
                if (node.executionRevision == std::numeric_limits<std::uint64_t>::max()
                    || flow.structureRevision == std::numeric_limits<std::uint64_t>::max())
                    throw ContractError("输出类型修订计数已耗尽");
                changedFlows.insert(flow.id);
            }
    for (auto &flow : project.flows)
    {
        for (auto &node : flow.nodes)
        {
            auto &resolution = resolutions.at(node.id);
            if (node.signatures.outputs != resolution.outputs)
            {
                node.signatures.outputs = std::move(resolution.outputs);
                ++node.executionRevision;
                project.dirty = true;
            }
            node.outputTypeErrors = std::move(resolution.errors);
        }
        if (changedFlows.contains(flow.id))
            ++flow.structureRevision;
    }
}

namespace {
// Ports derived from a node model stay bound to the node's own configuration: an unbound input
// keeps Missing, and the tool's status says what an absent value means. Only a declaration carries
// a type default.
PortDefinition materializedPort(PortId id, std::string nameZh, TypeSpec type)
{
    PortDefinition port;
    port.id           = id;
    port.nameZh       = std::move(nameZh);
    port.type         = std::move(type);
    port.defaultValue = Missing{};
    port.group        = "流程控制";
    return port;
}

std::string typeCell(const TypeSpec &type)
{
    return std::to_string(static_cast<int>(type.type));
}

std::string dtypeCell(const TypeSpec &type)
{
    return type.dtype ? std::to_string(static_cast<int>(*type.dtype)) : std::string{};
}

std::string rankCell(const TypeSpec &type)
{
    return type.rank >= 0 ? std::to_string(type.rank) : std::string{};
}
} // namespace

ToolDefinition nodeContract(const NodeDefinition &node, const ToolDefinition &definition)
{
    auto contract = definition;
    if (definition.derivesPorts())
    {
        std::vector<PortDefinition> system;
        for (const auto &port : definition.outputs)
            if (isReservedPort(PortStage::Outputs, port.id))
                system.push_back(port);
        switch (definition.nodeModel)
        {
        case NodeModel::Fixed:
            break;
        case NodeModel::Expression:
        case NodeModel::Condition:
        {
            if (!node.expression)
                throw ContractError("节点缺少表达式配置：" + node.title());
            contract.inputs.clear();
            if (definition.nodeModel == NodeModel::Condition && conditionUsesUpstream(node))
                contract.inputs.push_back(materializedPort(kConditionInputPort, "条件", {Type::Bool}));
            else
            {
                contract.inputs.reserve(node.expression->variables.size());
                for (const auto &variable : node.expression->variables)
                    contract.inputs.push_back(
                        materializedPort(variable.portId, variable.name, variable.type));
            }
            if (definition.nodeModel == NodeModel::Expression)
            {
                contract.outputs = std::move(system);
                for (const auto &result : node.expression->program)
                    if (result.exposed)
                        contract.outputs.push_back(
                            materializedPort(result.portId, result.name, result.type));
            }
            break;
        }
        case NodeModel::Select:
        {
            if (!node.selectType)
                throw ContractError("合流节点尚未选择类型：" + node.title());
            contract.inputs = {materializedPort(SelectPorts::Selector, "选择条件", {Type::Bool}),
                               materializedPort(SelectPorts::IfTrue, "成立值", *node.selectType),
                               materializedPort(SelectPorts::IfFalse, "否则值", *node.selectType)};
            contract.outputs = std::move(system);
            contract.outputs.push_back(materializedPort(SelectPorts::Value, "所选值", *node.selectType));
            break;
        }
        }
        return contract;
    }
    for (auto &port : contract.outputs)
        if (port.typeFrom)
        {
            const auto type = node.signatures.outputs.find(port.id);
            if (type == node.signatures.outputs.end())
            {
                const auto error = node.outputTypeErrors.find(port.id);
                throw ContractError("输出类型无法解析：" + node.title() + "." + port.nameZh + "："
                                    + (error == node.outputTypeErrors.end() ? "未配置类型来源" : error->second));
            }
            port.type = type->second;
            port.typeFrom.reset();
        }
    return contract;
}

Values nodeParameters(const NodeDefinition &node, const ToolDefinition &definition)
{
    Values parameters;
    if (definition.nodeModel == NodeModel::Expression || definition.nodeModel == NodeModel::Condition)
    {
        if (!node.expression)
            throw ContractError("节点缺少表达式配置：" + node.title());
        NestedVector<std::string, 2> variables;
        NestedVector<std::string, 2> program;
        // The table stays vec_vec_string, so the port number travels as its decimal digits: it is a
        // cell value here, never a dictionary key. The engine parses that one column back.
        for (const auto &variable : node.expression->variables)
            variables.push_back({std::to_string(variable.portId), variable.symbol, typeCell(variable.type),
                                 dtypeCell(variable.type), rankCell(variable.type),
                                 variable.allowMissing ? "true" : "false"});
        for (const auto &result : node.expression->program)
            program.push_back({std::to_string(result.portId), result.symbol, result.expression, typeCell(result.type),
                               dtypeCell(result.type), rankCell(result.type), result.exposed ? "true" : "false"});
        parameters.emplace(kVariablesInitPort, Array::fromNested(std::move(variables)));
        parameters.emplace(kProgramInitPort, Array::fromNested(std::move(program)));
        if (definition.nodeModel == NodeModel::Condition)
            parameters.emplace(kConditionModePort, static_cast<std::int64_t>(conditionUsesUpstream(node) ? BindingMode::Upstream : BindingMode::Literal));
        return parameters;
    }
    for (const auto &port : definition.initParams)
    {
        const auto found = node.initParams.find(port.id);
        parameters.emplace(
            port.id, copyProjectLiteral(found == node.initParams.end() ? port.defaultValue : found->second.literal));
    }
    return parameters;
}

std::map<std::string, std::filesystem::path> resolveModelReferences(
    const Values &trainValues, const ToolDefinition &definition, const std::filesystem::path &resourceRoot)
{
    std::map<std::string, std::filesystem::path> result;
    if (!definition.training)
        return result;
    for (const auto &artifact : definition.training->artifacts)
    {
        const auto parameter = trainValues.find(artifact.target.id);
        if (parameter == trainValues.end())
            continue;
        const auto *text = std::get_if<std::string>(&parameter->second);
        if (!text || text->empty())
            continue;
        const auto file = pathFromUtf8(*text);
        result.emplace(artifact.id, (file.is_absolute() ? file : resourceRoot / file).lexically_normal());
    }
    return result;
}

std::map<std::string, std::filesystem::path> nodeModelReferences(
    const NodeDefinition &node, const ToolDefinition &definition, const std::filesystem::path &resourceRoot)
{
    Values trainValues;
    for (const auto &[id, binding] : node.trainParams)
        if (binding.mode == BindingMode::Literal)
            trainValues.emplace(id, binding.literal);
    return resolveModelReferences(trainValues, definition, resourceRoot);
}

Values hydrateNodeTrainingSets(const NodeDefinition &node, const ToolDefinition &definition,
                               const std::filesystem::path &resourceRoot, std::string *error)
{
    Values hydrated;
    for (const auto &[id, binding] : node.trainParams)
        hydrated.emplace(id, copyProjectLiteral(binding.literal));
    if (!definition.training || definition.training->datasets.empty())
        return hydrated;

    auto appendError = [&](const std::string &message)
    {
        if (!error || message.empty())
            return;
        if (!error->empty())
            *error += "；";
        *error += message;
    };

    const auto models = nodeModelReferences(node, definition, resourceRoot);
    std::map<std::filesystem::path, std::vector<TrainingArchiveImage>> cache;
    std::set<std::filesystem::path>                                            failedArchives;

    for (const auto &datasetSpec : definition.training->datasets)
    {
        const auto paramIt = hydrated.find(datasetSpec.parameter);
        if (paramIt == hydrated.end())
            continue;
        const auto *existing = std::get_if<DataRef<TrainingSet>>(&paramIt->second);
        if (!existing)
            continue;
        const auto &srcSet = existing->get();
        bool        needsPixels = false;
        for (const auto &[imgId, img] : srcSet.images)
            if (img.pixels.empty())
            {
                needsPixels = true;
                break;
            }
        if (!needsPixels)
            continue;

        TrainingSet updated = srcSet;
        for (auto &[imgId, img] : updated.images)
            if (!img.pixels.empty())
                img.pixels = img.pixels.clone();

        bool loadedFromArchive = false;
        for (const auto &[artifactId, archivePath] : models)
        {
            if (failedArchives.contains(archivePath))
                continue;
            if (!cache.contains(archivePath))
            {
                try
                {
                    cache.emplace(archivePath, TrainingArchive::readImages(archivePath));
                }
                catch (const std::exception &ex)
                {
                    failedArchives.insert(archivePath);
                    appendError("无法从训练资源包加载原图（" + artifactId + "）：" + ex.what());
                    continue;
                }
            }
            for (const auto &stored : cache.at(archivePath))
            {
                if (stored.datasetPort != datasetSpec.parameter)
                    continue;
                if (const auto target = updated.images.find(stored.imageId); target != updated.images.end())
                {
                    target->second.pixels     = stored.pixels.clone();
                    target->second.colorOrder = stored.colorOrder;
                    loadedFromArchive         = true;
                }
            }
        }
        if (!loadedFromArchive && models.empty())
            appendError("节点尚未配置训练资源包路径，无法还原训练原图");
        paramIt->second = DataRef<TrainingSet>(std::move(updated));
    }
    return hydrated;
}
} // namespace visionrt
