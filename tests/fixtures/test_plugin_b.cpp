#include "visionrt/contracts/Module.h"

#include <exception>

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
        return visionrt::toolResult(std::move(out), {0, "ok"});
    }

    visionrt::ToolStatus release() override
    {
        return {};
    }
};

visionrt::IToolInstance *VT_CALL pluginCreate(const visionrt::ToolServices *, visionrt::ToolStatus *status) noexcept
{
    try
    {
        auto *instance = new PluginBTool();
        if (status)
            *status = {};
        return instance;
    }
    catch (const std::exception &error)
    {
        if (status)
            *status = {-3, "测试工具创建失败", error.what()};
        return nullptr;
    }
    catch (...)
    {
        if (status)
            *status = {-3, "测试工具创建发生未知异常"};
        return nullptr;
    }
}

void VT_CALL pluginDestroy(visionrt::IToolInstance *instance) noexcept
{
    delete instance;
}

visionrt::ToolStatus VT_CALL registerTools(visionrt::IToolRegistry &registry) noexcept
{
    return registry.add({"test.plugin.b", pluginCreate, pluginDestroy});
}

const visionrt::ModuleApi g_api{visionrt::MODULE_ABI_MAJOR, sizeof(visionrt::ModuleApi), registerTools};
} // namespace

extern "C" VT_EXPORT const visionrt::ModuleApi *VT_CALL visiontoolsModuleApi() noexcept
{
    return &g_api;
}
