#pragma once

#include "core/Project.h"
#include "core/ToolDefinition.h"

namespace visionrt {

// Single materialisation entry: node configuration plus the immutable tool
// declaration produce the contract used by connection candidates, transaction
// validation, storage reconciliation, instance configuration, Python result
// validation, the frozen run graph and UI type display.
ToolDefinition nodeContract(const NodeDefinition &node, const ToolDefinition &definition);

// Derived init parameters for the instanced tool. Expression and condition nodes
// derive variables/program from their node configuration instead of persisting
// another copy in the project.
Values nodeParameters(const NodeDefinition &node, const ToolDefinition &definition);

void setOutputDeclarations(NodeDefinition &node, const ToolDefinition &definition);
void resolveOutputTypes(ProjectDefinition &project);
TypeMap resolvedOutputTypes(const ToolDefinition &contract);

} // namespace visionrt
