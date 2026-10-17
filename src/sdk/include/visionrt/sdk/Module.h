#pragma once

#include "visionrt/sdk/Export.h"
#include "visionrt/sdk/Tool.h"
#include <cstdint>

namespace visionrt {

constexpr std::uint32_t MODULE_ABI_MAJOR = 1;

struct ModuleApi {
    std::uint32_t abi_major;
    std::uint32_t struct_bytes;
    IToolInstance* (VT_CALL *create)(const char* registry_name,
                                   const ToolServices* services,
                                   ToolStatus* status) noexcept;
    void (VT_CALL *destroy)(IToolInstance*) noexcept;
};

} // namespace visionrt

extern "C" VT_EXPORT const visionrt::ModuleApi* VT_CALL visiontoolsModuleApi() noexcept;
