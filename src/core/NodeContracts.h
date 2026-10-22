#pragma once

#include "core/Project.h"

namespace visionrt {
void setOutputDeclarations(NodeDefinition &node, const ToolDefinition &definition);
void resolveOutputTypes(ProjectDefinition &project);
ToolDefinition nodeContract(const NodeDefinition &node, const ToolDefinition &definition);
TypeMap        resolvedOutputTypes(const ToolDefinition &contract);
}
