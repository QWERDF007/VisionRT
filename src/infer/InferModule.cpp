#include "infer/InferProtocol.h"
#include "infer/YoloModel.h"

#include <cuda_runtime_api.h>
#include <inferrt/model/ModelFactory.hpp>
#include <exception>

#ifndef VISIONRT_INFERRT_VERSION
#define VISIONRT_INFERRT_VERSION "unknown (package did not report version)"
#endif

visionrt::IToolInstance* visionrtInferCreate(const visionrt::ToolServices* services,
                                           visionrt::ToolStatus* status) noexcept {
    try {
        if (!services || !status) return nullptr;
        *status = {};
        return new visionrt::infer::YoloTool(*services);
    } catch (const std::exception& error) {
        if (status) *status = {-3, std::string("VisionRTInfer factory: ") + error.what()};
    } catch (...) {
        if (status) *status = {-3, "VisionRTInfer factory: unknown exception"};
    }
    return nullptr;
}

void visionrtInferDestroy(visionrt::IToolInstance* tool) noexcept { delete tool; }

void visionrtInferQueryDevices(visionrt::GpuQueryResult* result) noexcept {
    if (!result) return;
    try {
        result->devices.clear();
        int count = 0;
        const auto code = cudaGetDeviceCount(&count);
        if (code != cudaSuccess) {
            result->status = {-3, std::string("CUDA device query: ") + cudaGetErrorString(code)};
            return;
        }
        if (count == 0) {
            result->status = {-3, "CUDA device query: no NVIDIA CUDA devices"};
            return;
        }
        result->devices.reserve(count);
        for (int index = 0; index < count; ++index) {
            cudaDeviceProp properties{};
            const auto propertyCode = cudaGetDeviceProperties(&properties, index);
            if (propertyCode != cudaSuccess) {
                result->devices.clear();
                result->status = {-3, std::string("CUDA device properties: ") + cudaGetErrorString(propertyCode)};
                return;
            }
            result->devices.push_back({index, properties.name});
        }
        result->status = {};
    } catch (const std::exception& error) {
        result->devices.clear();
        result->status = {-3, std::string("CUDA device query: ") + error.what()};
    } catch (...) {
        result->devices.clear();
        result->status = {-3, "CUDA device query: unknown exception"};
    }
}

visionrt::ToolResult visionrtInferExecuteWithTrace(visionrt::IToolInstance* tool,
    const visionrt::Values* inputs, std::vector<visionrt::infer::ImageTrace>* traces) noexcept {
    if (!tool || !inputs || !traces)
        return visionrt::infer::missingResult({-1, "Invalid probe execution arguments"});
    return static_cast<visionrt::infer::YoloTool*>(tool)->executeWithTrace(*inputs, *traces);
}

void visionrtInferInspect(visionrt::IToolInstance* tool,
    std::vector<visionrt::infer::TensorDescription>* tensors,
    visionrt::infer::ProbeEnvironment* environment, visionrt::ToolStatus* status) noexcept {
    if (!tool || !tensors || !environment || !status) return;
    try {
        *tensors = static_cast<visionrt::infer::YoloTool*>(tool)->tensorDescriptions();
        environment->sdkVersion = VISIONRT_INFERRT_VERSION;
        environment->registeredModels = irt::model::getRegisteredModelNames();
        auto code = cudaRuntimeGetVersion(&environment->cudaRuntimeVersion);
        if (code == cudaSuccess) code = cudaDriverGetVersion(&environment->cudaDriverVersion);
        *status = code == cudaSuccess ? visionrt::ToolStatus{} :
            visionrt::ToolStatus{-3, std::string("CUDA version query: ") + cudaGetErrorString(code)};
    } catch (const std::exception& error) {
        *status = {-1, std::string("InferRT probe inspection: ") + error.what()};
    } catch (...) { *status = {-1, "InferRT probe inspection: unknown exception"}; }
}

void visionrtInferProbeGeometry(visionrt::infer::GeometryProbe* result,
                               visionrt::ToolStatus* status) noexcept {
    if (!result || !status) return;
    try {
        *result = visionrt::infer::probePreprocessing();
        *status = {};
    } catch (const std::exception& error) {
        *status = {-1, std::string("Geometry pixel probe: ") + error.what()};
    } catch (...) { *status = {-1, "Geometry pixel probe: unknown exception"}; }
}
