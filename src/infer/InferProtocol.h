#pragma once

#include "core/Tool.h"

namespace visionrt {
struct GpuDevice { int index; std::string name; };
struct GpuQueryResult {
    ToolStatus status;
    std::vector<GpuDevice> devices;
    std::string modulePath;
};

namespace infer {
struct ImageTrace;
struct TensorDescription;
struct GeometryProbe;
struct ProbeEnvironment {
    int cudaRuntimeVersion = 0;
    int cudaDriverVersion = 0;
    std::string sdkVersion;
    std::vector<std::string> registeredModels;
};
// Private same-product C++ ABI: MSVC x64 /MD, matching Value/Tool headers.
// C names prevent name mangling; this is not a third-party plugin ABI.
using CreateTool = IToolInstance* (*)(const ToolServices*, ToolStatus*) noexcept;
using DestroyTool = void (*)(IToolInstance*) noexcept;
using QueryDevices = void (*)(GpuQueryResult*) noexcept;
inline ToolResult missingResult(ToolStatus status) {
    return {std::move(status), {{"images", Missing{}}, {"rois", Missing{}},
        {"scores", Missing{}}, {"classes", Missing{}}, {"class_names", Missing{}}}};
}
}
} // namespace visionrt

#ifdef VISIONRT_INFER_EXPORTS
#define VISIONRT_INFER_API extern "C" __declspec(dllexport)
#else
#define VISIONRT_INFER_API extern "C"
#endif
VISIONRT_INFER_API visionrt::IToolInstance* visionrtInferCreate(
    const visionrt::ToolServices*, visionrt::ToolStatus*) noexcept;
VISIONRT_INFER_API void visionrtInferDestroy(visionrt::IToolInstance*) noexcept;
VISIONRT_INFER_API void visionrtInferQueryDevices(visionrt::GpuQueryResult*) noexcept;
// Probe-only entry points reuse the production tool, never a parallel decoder.
VISIONRT_INFER_API visionrt::ToolResult visionrtInferExecuteWithTrace(
    visionrt::IToolInstance*, const visionrt::Values*,
    std::vector<visionrt::infer::ImageTrace>*) noexcept;
VISIONRT_INFER_API void visionrtInferInspect(
    visionrt::IToolInstance*, std::vector<visionrt::infer::TensorDescription>*,
    visionrt::infer::ProbeEnvironment*, visionrt::ToolStatus*) noexcept;
VISIONRT_INFER_API void visionrtInferProbeGeometry(
    visionrt::infer::GeometryProbe*, visionrt::ToolStatus*) noexcept;
