// A pair of tools whose only purpose is to prove that the workbench binds a declared capability by
// what the declaration says, not by a tool name or a fixed port number. The business tool uses
// business numbers the production tools never use for the same roles, and the helper answers on
// 30..35 instead of the legacy 2..5.
#include "visionrt/contracts/Module.h"

#include <cstdint>
#include <string>
#include <utility>
#include <variant>

namespace {

constexpr double kDefaultRadius = 18.0;

class MeasureProbe final : public visionrt::IToolInstance
{
public:
    visionrt::ToolStatus initialize(const visionrt::ToolConfiguration &configuration) override
    {
        return {};
    }

    visionrt::ToolResult execute(const visionrt::Values &) override
    {
        visionrt::Values outputs;
        outputs.emplace(2, static_cast<std::int64_t>(1));
        outputs.emplace(36, visionrt::Circle::fromEncoded(visionrt::ROI{}));
        return visionrt::toolResult(std::move(outputs), {0, "ok"});
    }

    visionrt::ToolStatus release() override
    {
        return {};
    }
};

class PickProbe final : public visionrt::IToolInstance
{
public:
    visionrt::ToolStatus initialize(const visionrt::ToolConfiguration &configuration) override
    {
        return {};
    }

    visionrt::ToolResult execute(const visionrt::Values &values) override
    {
        const auto  found = values.find(2);
        const auto *regions
            = found == values.end() ? nullptr : std::get_if<visionrt::Circle>(&found->second);
        if (!regions || regions->get().size() != 1 || regions->get().front().size() != 1)
            return visionrt::toolResult({}, {-2, "搜索区域缺失"});
        const auto center = visionrt::RegionView(regions->get().front().front()).circle().center();
        double     radius = kDefaultRadius;
        if (const auto found = values.find(7); found != values.end())
            if (const auto *declared = std::get_if<std::int64_t>(&found->second))
                radius = static_cast<double>(*declared);
        const visionrt::EncodedRegion region{static_cast<double>(visionrt::RoiKind::Circle), center.x, center.y,
                                             radius};
        visionrt::Values              outputs;
        outputs.emplace(30, visionrt::Circle::fromEncoded(visionrt::ROI{{region}}));
        outputs.emplace(31, visionrt::Circle::fromEncoded(visionrt::ROI{{region}}));
        outputs.emplace(32, true);
        outputs.emplace(33, std::string("候选"));
        outputs.emplace(34, center.x > 240 ? 5000.0 : 12.5);
        outputs.emplace(35, std::string("dark_to_light"));
        outputs.emplace(36, std::string("已选"));
        return visionrt::toolResult(std::move(outputs), {0, "ok"});
    }

    visionrt::ToolStatus release() override
    {
        return {};
    }
};

visionrt::IToolInstance *VT_CALL createMeasure(visionrt::ToolStatus *status) noexcept
{
    if (status)
        *status = {};
    return new MeasureProbe();
}

visionrt::IToolInstance *VT_CALL createPick(visionrt::ToolStatus *status) noexcept
{
    if (status)
        *status = {};
    return new PickProbe();
}

void VT_CALL destroy(visionrt::IToolInstance *instance) noexcept
{
    delete instance;
}

visionrt::ToolStatus VT_CALL registerTools(visionrt::IToolRegistry &registry) noexcept
{
    const auto measure = registry.add({"ProbeMeasure", createMeasure, destroy});
    if (!measure)
        return measure;
    const auto pick = registry.add({"ProbePick", createPick, destroy});
    if (!pick)
        return pick;
    return {};
}

const visionrt::ModuleApi g_api{visionrt::MODULE_ABI_MAJOR, sizeof(visionrt::ModuleApi), registerTools};
} // namespace

extern "C" VT_EXPORT const visionrt::ModuleApi *VT_CALL visiontoolsModuleApi() noexcept
{
    return &g_api;
}
