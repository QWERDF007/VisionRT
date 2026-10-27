#include <visionrt/contracts/Module.h>

#include <fstream>

namespace {
using namespace visionrt;

struct Payload
{
    std::filesystem::path     marker;
    std::vector<std::uint8_t> pixels{1, 2, 3, 4, 5, 6, 7, 8};

    ~Payload()
    {
        std::ofstream(marker, std::ios::app) << "released\n";
    }
};

class ValueTool final : public IToolInstance
{
public:
    ToolStatus initialize(const Values &parameters) override
    {
        marker_ = std::filesystem::u8path(std::get<std::string>(parameters.at("marker")));
        return {};
    }

    ToolResult execute(const Values &) override
    {
        auto payload    = std::make_shared<Payload>();
        payload->marker = marker_;
        cv::Mat image(2, 3, CV_8UC1, payload->pixels.data(), 4);
        return toolResult({{"image", DataRef<Image>(Image{image}, {payload})},
             {"array", Array(NestedVector<std::int64_t, 2>{{17}, {}}, {payload})},
             {"roi", DataRef<ROI>(ROI{{{2, 16777217.125, 0, 3, 2, 0}}}, {payload})}}, {});
    }

    ToolStatus release() override
    {
        return {};
    }

private:
    std::filesystem::path marker_;
};

IToolInstance *VT_CALL create(const ToolServices *, ToolStatus *status) noexcept
{
    try
    {
        auto *instance = new ValueTool;
        if (status)
            *status = {};
        return instance;
    }
    catch (...)
    {
        if (status)
            *status = {-3, "测试工具创建失败"};
        return nullptr;
    }
}

void VT_CALL destroy(IToolInstance *instance) noexcept
{
    delete instance;
}

ToolStatus VT_CALL registerTools(IToolRegistry &registry) noexcept
{
    try
    {
        return registry.add({"test.value.lifetime", create, destroy});
    }
    catch (...)
    {
        return {-3, "测试工具注册失败"};
    }
}
}

extern "C" VT_EXPORT const visionrt::ModuleApi *VT_CALL visiontoolsModuleApi() noexcept
{
    static const visionrt::ModuleApi api{visionrt::MODULE_ABI_MAJOR, sizeof(visionrt::ModuleApi), registerTools};
    return &api;
}
