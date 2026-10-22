#include <visionrt/contracts/Registry.h>

#include <cstdint>

struct LegacyModuleApi
{
    std::uint32_t abi_major;
    std::uint32_t struct_bytes;
    visionrt::IToolInstance *(VT_CALL *create)(const char *, const visionrt::ToolServices *,
                                               visionrt::ToolStatus *) noexcept;
    void(VT_CALL *destroy)(visionrt::IToolInstance *) noexcept;
};

extern "C" VT_EXPORT const LegacyModuleApi *VT_CALL visiontoolsModuleApi() noexcept
{
    static const LegacyModuleApi api{2, sizeof(LegacyModuleApi), nullptr, nullptr};
    return &api;
}
