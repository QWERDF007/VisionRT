#include <visionrt/contracts/Module.h>
#include <cstdlib>

// ABI 10 shares the module header but not the PortValue variant: 11 added DataRef<TrainingSet>, so
// a module built against 10 would read every value at the wrong index. It must be refused before
// any factory runs, which is why the factory aborts if it is ever reached.
extern "C" VT_EXPORT const visionrt::ModuleApi *VT_CALL visiontoolsModuleApi() noexcept
{
    static const visionrt::ModuleApi api{10, sizeof(visionrt::ModuleApi),
        +[](visionrt::IToolRegistry &) noexcept -> visionrt::ToolStatus { std::abort(); }};
    return &api;
}
