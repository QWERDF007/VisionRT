#include <visionrt/contracts/Module.h>

extern "C" VT_EXPORT const visionrt::ModuleApi *VT_CALL visiontoolsModuleApi() noexcept
{
    static const visionrt::ModuleApi api{1, sizeof(visionrt::ModuleApi), nullptr, nullptr};
    return &api;
}
