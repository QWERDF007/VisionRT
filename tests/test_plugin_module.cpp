#include "visionrt/contracts/Module.h"

#include <atomic>
#include <chrono>
#include <exception>
#include <thread>
#include <type_traits>

namespace {

std::atomic<int> g_instanceCount{0};
std::atomic<int> g_destroyCalls{0};
std::atomic<int> g_counterReleaseCalls{0};
std::atomic<int> g_functionInitializeCalls{0};

class SleepTool final : public visionrt::IToolInstance
{
public:
    SleepTool()
    {
        ++g_instanceCount;
    }

    ~SleepTool() override
    {
        --g_instanceCount;
    }

    visionrt::ToolStatus initialize(const visionrt::Values &) override
    {
        return {};
    }

    visionrt::ToolResult execute(const visionrt::Values &) override
    {
        std::this_thread::sleep_for(std::chrono::milliseconds(80));
        visionrt::Values out;
        out.emplace("y", std::int64_t{42});
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

class CounterTool final : public visionrt::IToolInstance
{
public:
    CounterTool()
    {
        ++g_instanceCount;
    }

    ~CounterTool() override
    {
        --g_instanceCount;
    }

    visionrt::ToolStatus initialize(const visionrt::Values &params) override
    {
        const auto it = params.find("initial");
        if (it != params.end() && std::holds_alternative<std::int64_t>(it->second))
        {
            count_ = std::get<std::int64_t>(it->second);
        }
        else
        {
            count_ = 0;
        }
        return {};
    }

    visionrt::ToolResult execute(const visionrt::Values &) override
    {
        ++count_;
        visionrt::Values outputs;
        outputs.emplace("count", count_);
        return {
            {0, "ok"},
            std::move(outputs)
        };
    }

    visionrt::ToolStatus release() override
    {
        ++g_counterReleaseCalls;
        return {};
    }

private:
    std::int64_t count_ = 0;
};

class FailInitTool final : public visionrt::IToolInstance
{
public:
    FailInitTool()
    {
        ++g_instanceCount;
    }

    ~FailInitTool() override
    {
        --g_instanceCount;
    }

    visionrt::ToolStatus initialize(const visionrt::Values &) override
    {
        return {7, "Simulated plugin initialization failure"};
    }

    visionrt::ToolResult execute(const visionrt::Values &) override
    {
        return {
            {0, "ok"},
            {}
        };
    }

    visionrt::ToolStatus release() override
    {
        return {};
    }
};

class FunctionTool final : public visionrt::IToolInstance
{
public:
    FunctionTool()
    {
        ++g_instanceCount;
    }

    ~FunctionTool() override
    {
        --g_instanceCount;
    }

    visionrt::ToolStatus initialize(const visionrt::Values &) override
    {
        ++g_functionInitializeCalls;
        return {};
    }

    visionrt::ToolResult execute(const visionrt::Values &in) override
    {
        visionrt::Values out;
        std::int64_t     x  = 0;
        const auto       it = in.find("x");
        if (it != in.end() && std::holds_alternative<std::int64_t>(it->second))
        {
            x = std::get<std::int64_t>(it->second);
        }
        out.emplace("y", x * 2);
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

class ThrowInitTool final : public visionrt::IToolInstance
{
public:
    ThrowInitTool()
    {
        ++g_instanceCount;
    }

    ~ThrowInitTool() override
    {
        --g_instanceCount;
    }

    visionrt::ToolStatus initialize(const visionrt::Values &) override
    {
        throw std::runtime_error("Simulated init exception in plugin");
    }

    visionrt::ToolResult execute(const visionrt::Values &) override
    {
        return {
            {0, "ok"},
            {}
        };
    }

    visionrt::ToolStatus release() override
    {
        return {};
    }
};

class FailReleaseTool final : public visionrt::IToolInstance
{
public:
    FailReleaseTool()
    {
        ++g_instanceCount;
    }

    ~FailReleaseTool() override
    {
        --g_instanceCount;
    }

    visionrt::ToolStatus initialize(const visionrt::Values &) override
    {
        return {};
    }

    visionrt::ToolResult execute(const visionrt::Values &) override
    {
        return {
            {0, "ok"},
            {}
        };
    }

    visionrt::ToolStatus release() override
    {
        return {6, "Simulated release failure"};
    }
};

template<class Tool = void, std::int64_t StatusCode = 0>
visionrt::IToolInstance *VT_CALL pluginCreate(const visionrt::ToolServices *, visionrt::ToolStatus *status) noexcept
{
    try
    {
        visionrt::IToolInstance *instance = nullptr;
        if constexpr (!std::is_void_v<Tool>)
            instance = new Tool();
        if (status)
            *status = {StatusCode, StatusCode == 0 ? "" : "Simulated factory creation failure",
                       StatusCode == 0 ? "" : "factory diagnostic"};
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
    ++g_destroyCalls;
    delete instance;
}

visionrt::ToolStatus VT_CALL registerTools(visionrt::IToolRegistry &registry) noexcept
{
    const visionrt::ToolFactoryDescriptor descriptors[]{
        {"test.plugin.counter", pluginCreate<CounterTool>, pluginDestroy},
        {"test.plugin.fail_init", pluginCreate<FailInitTool>, pluginDestroy},
        {"test.plugin.function", pluginCreate<FunctionTool>, pluginDestroy},
        {"test.plugin.sleep", pluginCreate<SleepTool>, pluginDestroy},
        {"test.plugin.create_null_success", pluginCreate<>, pluginDestroy},
        {"test.plugin.create_nonnull_fail", pluginCreate<CounterTool, 5>, pluginDestroy},
        {"test.plugin.init_throw", pluginCreate<ThrowInitTool>, pluginDestroy},
        {"test.plugin.release_fail", pluginCreate<FailReleaseTool>, pluginDestroy},
    };
    for (const auto &descriptor : descriptors)
        if (auto status = registry.add(descriptor); !status)
            return status;
    return {};
}

const visionrt::ModuleApi g_api{visionrt::MODULE_ABI_MAJOR, sizeof(visionrt::ModuleApi), registerTools};

} // namespace

extern "C" VT_EXPORT const visionrt::ModuleApi *VT_CALL visiontoolsModuleApi() noexcept
{
    return &g_api;
}

extern "C" VT_EXPORT int VT_CALL testPluginInstanceCount() noexcept
{
    return g_instanceCount.load();
}

extern "C" VT_EXPORT int VT_CALL testPluginDestroyCalls() noexcept
{
    return g_destroyCalls.load();
}

extern "C" VT_EXPORT int VT_CALL testPluginCounterReleaseCalls() noexcept
{
    return g_counterReleaseCalls.load();
}

extern "C" VT_EXPORT int VT_CALL testPluginFunctionInitializeCalls() noexcept
{
    return g_functionInitializeCalls.load();
}
