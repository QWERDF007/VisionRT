#include "visionrt/sdk/Module.h"
#include <cstring>
#include <atomic>

namespace {

std::atomic<int> g_instanceCount{0};
std::atomic<int> g_createCalls{0};
std::atomic<int> g_destroyCalls{0};

class CounterTool final : public visionrt::IToolInstance {
public:
    CounterTool() { ++g_instanceCount; }
    ~CounterTool() override { --g_instanceCount; }

    visionrt::ToolStatus initialize(const visionrt::Values& params) override {
        const auto it = params.find("initial");
        if (it != params.end() && std::holds_alternative<std::int64_t>(it->second)) {
            count_ = std::get<std::int64_t>(it->second);
        } else {
            count_ = 0;
        }
        return {};
    }

    visionrt::ToolResult execute(const visionrt::Values&) override {
        ++count_;
        visionrt::Values outputs;
        outputs.emplace("count", count_);
        return {{0, "ok"}, std::move(outputs)};
    }

    visionrt::ToolStatus release() override {
        return {};
    }

private:
    std::int64_t count_ = 0;
};

class FailInitTool final : public visionrt::IToolInstance {
public:
    FailInitTool() { ++g_instanceCount; }
    ~FailInitTool() override { --g_instanceCount; }
    visionrt::ToolStatus initialize(const visionrt::Values&) override {
        return {7, "Simulated plugin initialization failure"};
    }
    visionrt::ToolResult execute(const visionrt::Values&) override {
        return {{0, "ok"}, {}};
    }
    visionrt::ToolStatus release() override {
        return {};
    }
};

visionrt::IToolInstance* VT_CALL pluginCreate(const char* registry_name,
                                            const visionrt::ToolServices*,
                                            visionrt::ToolStatus* status) noexcept {
    ++g_createCalls;
    if (!registry_name) {
        if (status) *status = {-3, "null registry_name"};
        return nullptr;
    }
    if (std::strcmp(registry_name, "test.plugin.counter") == 0) {
        if (status) *status = {};
        return new CounterTool();
    }
    if (std::strcmp(registry_name, "test.plugin.fail_init") == 0) {
        if (status) *status = {};
        return new FailInitTool();
    }
    if (status) *status = {-3, std::string("Unknown registry name: ") + registry_name};
    return nullptr;
}

void VT_CALL pluginDestroy(visionrt::IToolInstance* instance) noexcept {
    ++g_destroyCalls;
    delete instance;
}

const visionrt::ModuleApi g_api{
    visionrt::MODULE_ABI_MAJOR,
    sizeof(visionrt::ModuleApi),
    pluginCreate,
    pluginDestroy
};

} // namespace

extern "C" VT_EXPORT const visionrt::ModuleApi* VT_CALL visiontoolsModuleApi() noexcept {
    return &g_api;
}
