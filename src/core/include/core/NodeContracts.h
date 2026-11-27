#pragma once

#include "core/Export.h"

#include "core/Project.h"
#include "core/ToolDefinition.h"

namespace visionrt {

// Ports a node model derives instead of reading them from a declaration. They keep the same
// numbering rule as every other contract: inputs from 1, business outputs from 2.
inline constexpr PortId kConditionInputPort = 1;
namespace SelectPorts {
inline constexpr PortId Selector = 1;
inline constexpr PortId IfTrue   = 2;
inline constexpr PortId IfFalse  = 3;
inline constexpr PortId Value    = 2;
}

// The cross-flow receiver is a declaration, so its numbers follow the same declaration order. The
// source port travels as an integer value: it names another node's output by number.
namespace ReceiveInitPorts {
inline constexpr PortId SourceFlow = 0;
inline constexpr PortId SourceNode = 1;
inline constexpr PortId SourcePort = 2;
inline constexpr PortId Timeout    = 3;
}
namespace ReceivePorts {
inline constexpr PortId Value               = 2;
inline constexpr PortId SourceStatus        = 3;
inline constexpr PortId SourceStatusMessage = 4;
}

// Single materialisation entry: node configuration plus the immutable tool
// declaration produce the contract used by connection candidates, transaction
// validation, storage reconciliation, instance configuration, Python result
// validation, the frozen run graph and UI type display.
VISIONRT_CORE_API ToolDefinition nodeContract(const NodeDefinition &node, const ToolDefinition &definition);

// Condition nodes choose their active input set from the binding mode of the
// single `condition` input: formula variables or one upstream Bool.
VISIONRT_CORE_API bool        conditionUsesUpstream(const NodeDefinition &node);

// Derived init parameters for the instanced tool. Expression and condition nodes
// derive variables/program from their node configuration instead of persisting
// another copy in the project.
VISIONRT_CORE_API Values nodeParameters(const NodeDefinition &node, const ToolDefinition &definition);

VISIONRT_CORE_API void setOutputDeclarations(NodeDefinition &node, const ToolDefinition &definition);
VISIONRT_CORE_API void resolveOutputTypes(ProjectDefinition &project);
VISIONRT_CORE_API TypeMap resolvedOutputTypes(const ToolDefinition &contract);

VISIONRT_CORE_API std::map<std::string, std::filesystem::path> resolveModelReferences(
    const Values &trainValues, const ToolDefinition &definition, const std::filesystem::path &resourceRoot);
VISIONRT_CORE_API std::map<std::string, std::filesystem::path> nodeModelReferences(
    const NodeDefinition &node, const ToolDefinition &definition, const std::filesystem::path &resourceRoot);

// Hydrates persisted TrainingSet metadata in a node's training parameters by reading original
// images from the node's configured training resource ZIP archive(s) resolved against resourceRoot.
VISIONRT_CORE_API Values hydrateNodeTrainingSets(const NodeDefinition &node, const ToolDefinition &definition,
                                                 const std::filesystem::path &resourceRoot,
                                                 std::string *error = nullptr);
} // namespace visionrt
