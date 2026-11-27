#pragma once

#include "core/Export.h"

#include "core/Tool.h"

#include <QJsonObject>

#include <array>
#include <cstdint>
#include <map>
#include <optional>
#include <set>
#include <string>
#include <utility>
#include <vector>

namespace visionrt {
using NodeId = std::uint64_t;
struct SourcePort
{
    NodeId node = 0;
    PortId port = 0;
    bool   operator==(const SourcePort &) const = default;
};

struct InputBinding
{
    BindingMode               mode    = BindingMode::Literal;
    PortValue                 literal = Missing{};
    std::optional<SourcePort> source;
};

struct GridPoint
{
    int  x                                   = 0;
    int  y                                   = 0;
    bool operator==(const GridPoint &) const = default;
};

using TypeMap    = std::map<PortId, TypeSpec>;
using BindingMap = std::map<PortId, InputBinding>;

inline constexpr std::array<PortStage, 3> kParameterStages{
    PortStage::Init,
    PortStage::Train,
    PortStage::Inputs,
};

// Node-owned dynamic port configuration. portId values are stable node-local
// identifiers allocated from nextPortId; symbols are the formula identifiers and
// names are free-form UI text.
struct ExpressionVariable
{
    PortId      portId = 0;
    std::string name;
    std::string symbol;
    TypeSpec    type;
    bool        allowMissing = false;
    bool        operator==(const ExpressionVariable &) const = default;
};

struct ExpressionResult
{
    PortId      portId = 0;
    std::string name;
    std::string symbol;
    std::string expression;
    TypeSpec    type;
    bool        exposed = true;
    bool        operator==(const ExpressionResult &) const = default;
};

struct ExpressionConfig
{
    // Expression results are business outputs, so 0 and 1 stay reserved for the host status pair.
    std::uint64_t                   nextPortId = 2;
    std::vector<ExpressionVariable> variables;
    std::vector<ExpressionResult>   program;
    bool operator==(const ExpressionConfig &) const = default;
};

struct TypeSnapshot
{
    TypeMap initParams;
    TypeMap inputs;
    TypeMap outputs;
    TypeMap trainParams;

    const TypeMap &stageTypes(PortStage stage) const
    {
        switch (stage)
        {
        case PortStage::Init:
            return initParams;
        case PortStage::Train:
            return trainParams;
        case PortStage::Inputs:
            return inputs;
        case PortStage::Outputs:
            return outputs;
        }
        throw ContractError("未知端口阶段");
    }

    TypeMap &stageTypes(PortStage stage)
    {
        return const_cast<TypeMap &>(std::as_const(*this).stageTypes(stage));
    }
};

struct NodeDefinition
{
    NodeId                                           id = 0;
    std::string                                      toolId;
    std::string                                      toolNameSnapshot;
    // The declaration version the node was last reconciled against. Absent in projects saved
    // before the host recorded it, and then simply unknown.
    std::string                                      toolVersionSnapshot;
    std::string                                      alias;
    GridPoint                                        position;
    BindingMap                                       initParams;
    BindingMap                                       trainParams;
    BindingMap                                       inputs;
    TypeSnapshot                                     signatures;
    std::map<PortId, PortTypeReference>              outputTypeSources;
    std::map<PortId, std::string>                    outputTypeErrors;
    std::optional<ExpressionConfig>                  expression;
    std::optional<TypeSpec>                          selectType;
    std::uint64_t                                    executionRevision = 0;
    // Changes when an initialized resource is replaced while its configured path stays the same.
    std::uint64_t                                    initializationRevision = 0;
    QJsonObject                                      unresolved;
    VISIONRT_CORE_API std::string                                      title() const;

    const BindingMap &stageBindings(PortStage stage) const
    {
        switch (stage)
        {
        case PortStage::Init:
            return initParams;
        case PortStage::Train:
            return trainParams;
        case PortStage::Inputs:
            return inputs;
        case PortStage::Outputs:
            break;
        }
        throw ContractError("端口阶段不支持输入绑定");
    }

    BindingMap &stageBindings(PortStage stage)
    {
        return const_cast<BindingMap &>(std::as_const(*this).stageBindings(stage));
    }
};

struct FlowDefinition
{
    std::string                 id;
    std::string                 name;
    bool                        runSelected = true;
    std::vector<NodeDefinition> nodes;
    std::uint64_t               structureRevision = 0;
};

struct ProjectDefinition
{
    std::string                 name       = "未命名项目";
    std::string                 notes;
    std::string                 appVersion;
    std::string                 contractsVersion;
    NodeId                      nextNodeId = 0;
    std::uint64_t               nextFlowId = 2;
    std::vector<FlowDefinition> flows{
        {"flow_1", "流程1"}
    };
    std::filesystem::path path;
    // A new empty project holds nothing to lose; the first edit marks it dirty.
    bool                  dirty = false;
};

VISIONRT_CORE_API PortValue      copyProjectLiteral(const PortValue &value);
VISIONRT_CORE_API NodeDefinition copyNodeConfiguration(const NodeDefinition &node);
} // namespace visionrt
