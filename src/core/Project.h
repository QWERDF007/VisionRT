#pragma once

#include "core/Tool.h"

#include <QJsonObject>

#include <cstdint>
#include <map>
#include <optional>
#include <set>
#include <string>
#include <vector>

namespace visionrt {
using NodeId = std::uint64_t;
enum class BindingMode
{
    Literal,
    Upstream
};

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

using TypeMap = std::map<PortId, TypeSpec>;

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

// Explicit execution condition of a logical node. `mode` is the single stored
// input mode (absent guard means the default user-input mode); a literal-mode
// guard keeps its old source only for switching back and never highlights or runs.
struct ExecutionGuard
{
    std::optional<SourcePort> source;
    BindingMode               mode = BindingMode::Upstream;
    bool operator==(const ExecutionGuard &) const = default;
};

struct TypeSnapshot
{
    TypeMap initParams;
    TypeMap inputs;
    TypeMap outputs;
};

struct NodeDefinition
{
    NodeId                                           id = 0;
    std::string                                      toolId;
    std::string                                      toolNameSnapshot;
    std::string                                      alias;
    GridPoint                                        position;
    Values                                           initParams;
    std::map<PortId, InputBinding>                   inputs;
    TypeSnapshot                                     signatures;
    std::map<PortId, PortTypeReference>              outputTypeSources;
    std::map<PortId, std::string>                    outputTypeErrors;
    std::optional<ExpressionConfig>                  expression;
    std::optional<ExecutionGuard>                    executionGuard;
    std::optional<TypeSpec>                          selectType;
    std::uint64_t                                    executionRevision = 0;
    QJsonObject                                      unresolved;
    QJsonObject                                      retiredInitParams;
    std::string                                      title() const;
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
    NodeId                      nextNodeId = 0;
    std::uint64_t               nextFlowId = 2;
    std::vector<FlowDefinition> flows{
        {"flow_1", "流程1"}
    };
    std::filesystem::path path;
    bool                  dirty = true;
};

PortValue      copyProjectLiteral(const PortValue &value);
NodeDefinition copyNodeConfiguration(const NodeDefinition &node);
} // namespace visionrt
