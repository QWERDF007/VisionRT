#include <visionrt/contracts/Module.h>

#include <atomic>
#include <string>

namespace {
std::atomic<int> createCalls{0};

class ValueTool final : public visionrt::IToolInstance
{
public:
    explicit ValueTool(std::int64_t value)
        : value_(value)
    {
    }

    visionrt::ToolStatus initialize(const visionrt::Values &) override
    {
        return {};
    }

    visionrt::ToolResult execute(const visionrt::Values &) override
    {
        return {{}, {{"value", value_}}};
    }

    visionrt::ToolStatus release() override
    {
        return {};
    }

private:
    std::int64_t value_;
};

template<std::int64_t Value>
visionrt::IToolInstance *VT_CALL create(const visionrt::ToolServices *, visionrt::ToolStatus *status) noexcept
{
    try
    {
        ++createCalls;
        auto *instance = new ValueTool(Value);
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

void VT_CALL destroy(visionrt::IToolInstance *instance) noexcept
{
    delete instance;
}

visionrt::ToolStatus VT_CALL registerTools(visionrt::IToolRegistry &registry) noexcept
{
    try
    {
        std::string copiedId = "test.registration.first";
        auto        status   = registry.add({copiedId.c_str(), create<11>, destroy});
        if (!status)
            return status;
        copiedId.assign("overwritten registration storage");
        status = registry.add({"test.registration.second", create<22>, destroy});
        if (!status)
            return status;
#ifdef VISIONRT_DUPLICATE_REGISTRATION
        registry.add({"test.registration.first", create<33>, destroy});
#endif
        return {};
    }
    catch (const std::exception &error)
    {
        return {-3, "测试模块注册失败", error.what()};
    }
    catch (...)
    {
        return {-3, "测试模块注册发生未知异常"};
    }
}
}

extern "C" VT_EXPORT const visionrt::ModuleApi *VT_CALL visiontoolsModuleApi() noexcept
{
#ifdef VISIONRT_SHORT_MODULE
    static const visionrt::ModuleApi api{visionrt::MODULE_ABI_MAJOR, 8, registerTools};
#elif defined(VISIONRT_NULL_REGISTRATION)
    static const visionrt::ModuleApi api{visionrt::MODULE_ABI_MAJOR, sizeof(visionrt::ModuleApi), nullptr};
#else
    static const visionrt::ModuleApi api{visionrt::MODULE_ABI_MAJOR, sizeof(visionrt::ModuleApi), registerTools};
#endif
    return &api;
}

extern "C" VT_EXPORT int VT_CALL testRegistrationCreateCalls() noexcept
{
    return createCalls.load();
}
