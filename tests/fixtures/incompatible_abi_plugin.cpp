#include <visionrt/contracts/Module.h>
#include <cstdlib>

// ABI 9 shares the module header, but its factories expect the removed service argument.
// A rejected module must never reach registration, and therefore cannot call any old factory.
extern "C" VT_EXPORT const visionrt::ModuleApi *VT_CALL visiontoolsModuleApi() noexcept
{
    static const visionrt::ModuleApi api{9, sizeof(visionrt::ModuleApi),
        +[](visionrt::IToolRegistry &) noexcept -> visionrt::ToolStatus { std::abort(); }};
    return &api;
}
