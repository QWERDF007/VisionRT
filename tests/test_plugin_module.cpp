#include "visionrt/contracts/Module.h"

#include <atomic>
#include <chrono>
#include <cstring>
#include <thread>

namespace {

std::atomic<int> g_instanceCount{0};
std::atomic<int> g_createCalls{0};
std::atomic<int> g_destroyCalls{0};

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

visionrt::IToolInstance *VT_CALL pluginCreate(const char           *registry_name, const visionrt::ToolServices *,
                                              visionrt::ToolStatus *status) noexcept
{
    ++g_createCalls;
    if (!registry_name)
    {
        if (status)
            *status = {-3, "null registry_name"};
        return nullptr;
    }
    if (std::strcmp(registry_name, "test.plugin.counter") == 0)
    {
        if (status)
            *status = {};
        return new CounterTool();
    }
    if (std::strcmp(registry_name, "test.plugin.fail_init") == 0)
    {
        if (status)
            *status = {};
        return new FailInitTool();
    }
    if (std::strcmp(registry_name, "test.plugin.function") == 0)
    {
        if (status)
            *status = {};
        return new FunctionTool();
    }
    if (std::strcmp(registry_name, "test.plugin.sleep") == 0)
    {
        if (status)
            *status = {};
        return new SleepTool();
    }
    if (std::strcmp(registry_name, "test.plugin.create_null_success") == 0)
    {
        if (status)
            *status = {};
        return nullptr;
    }
    if (std::strcmp(registry_name, "test.plugin.create_nonnull_fail") == 0)
    {
        if (status)
            *status = {5, "Simulated factory creation failure"};
        return new CounterTool();
    }
    if (std::strcmp(registry_name, "test.plugin.init_throw") == 0)
    {
        if (status)
            *status = {};
        return new ThrowInitTool();
    }
    if (std::strcmp(registry_name, "test.plugin.release_fail") == 0)
    {
        if (status)
            *status = {};
        return new FailReleaseTool();
    }
    if (status)
        *status = {-3, std::string("Unknown registry name: ") + registry_name};
    return nullptr;
}

void VT_CALL pluginDestroy(visionrt::IToolInstance *instance) noexcept
{
    ++g_destroyCalls;
    delete instance;
}

const visionrt::ModuleApi g_api{visionrt::MODULE_ABI_MAJOR, sizeof(visionrt::ModuleApi), pluginCreate, pluginDestroy};

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
