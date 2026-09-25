#pragma once

namespace visionrt {
class ToolRegistry;

// Register implementations against the tool YAML already loaded by registry.scan().
void registerCpuTools(ToolRegistry& registry);
} // namespace visionrt
