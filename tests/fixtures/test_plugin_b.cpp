#include "visionrt/contracts/Module.h"

#include <cstring>

extern "C"
{
#ifdef _WIN32
__declspec(dllimport) int dep_b_val();
#else
int dep_b_val();
#endif
}

namespace {
class PluginBTool final : public visionrt::IToolInstance
{
public:
    visionrt::ToolStatus initialize(const visionrt::Values &) override
    {
        return {};
    }

    visionrt::ToolResult execute(const visionrt::Values &) override
    {
        visionrt::Values out;
        out.emplace("val", static_cast<std::int64_t>(dep_b_val()));
        return {
            {0, "ok"},
            std::move(out)
        };
    }

    visionrt::ToolStatus release() override
    {
        return {};
    }
};

visionrt::IToolInstance *VT_CALL pluginCreate(const char           *registry_name, const visionrt::ToolServices *,
                                              visionrt::ToolStatus *status) noexcept
{
    if (registry_name && std::strcmp(registry_name, "test.plugin.b") == 0)
    {
        if (status)
            *status = {};
        return new PluginBTool();
    }
    if (status)
        *status = {-3, "unknown registry name"};
    return nullptr;
}

void VT_CALL pluginDestroy(visionrt::IToolInstance *instance) noexcept
{
    delete instance;
}

const visionrt::ModuleApi g_api{visionrt::MODULE_ABI_MAJOR, sizeof(visionrt::ModuleApi), pluginCreate, pluginDestroy};
} // namespace

extern "C" VT_EXPORT const visionrt::ModuleApi *VT_CALL visiontoolsModuleApi() noexcept
{
    return &g_api;
}
