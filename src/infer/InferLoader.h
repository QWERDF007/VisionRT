#pragma once

#include "infer/InferProtocol.h"

namespace visionrt {
class ToolRegistry;
// Registration is CPU-only, after registry.scan(). It never probes a device.
void registerInferTools(ToolRegistry& registry);
// May load the fixed app-directory module and query CUDA. Call on a worker,
// never the GUI thread. No project/configuration controls the module filename.
GpuQueryResult queryGpuDevices();
} // namespace visionrt
