// A pair of tools whose only purpose is to prove that the workbench binds a declared capability by
// what the declaration says, not by a tool name or a fixed port number. The business tool uses
// business numbers the production tools never use for the same roles, and the helper answers on
// 30..35 instead of the legacy 2..5.
#include "visionrt/contracts/Module.h"

#include <chrono>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <string>
#include <thread>
#include <utility>
#include <variant>

namespace {

constexpr double kDefaultRadius = 18.0;

class MeasureProbe final : public visionrt::IToolInstance
{
public:
    visionrt::ToolStatus initialize(const visionrt::Values &) override
    {
        return {};
    }

    visionrt::ToolResult execute(const visionrt::Values &) override
    {
        visionrt::Values outputs;
        outputs.emplace(2, static_cast<std::int64_t>(1));
        outputs.emplace(36, visionrt::DataRef<visionrt::ROI>(visionrt::ROI{}));
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
    visionrt::ToolStatus initialize(const visionrt::Values &) override
    {
        return {};
    }

    visionrt::ToolResult execute(const visionrt::Values &values) override
    {
        const auto  found = values.find(2);
        const auto *regions
            = found == values.end() ? nullptr : std::get_if<visionrt::DataRef<visionrt::ROI>>(&found->second);
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
        outputs.emplace(30, visionrt::DataRef<visionrt::ROI>(visionrt::ROI{{region}}));
        outputs.emplace(31, visionrt::DataRef<visionrt::ROI>(visionrt::ROI{{region}}));
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

// A trainer fixture: it proves that the workbench hands a tool the draft it actually holds — the
// published frame's pixels and the typed field values — rather than a path or an editor object.
class FitProbe final : public visionrt::IToolInstance
{
public:
    visionrt::ToolStatus initialize(const visionrt::Values &) override
    {
        return {};
    }

    visionrt::ToolResult execute(const visionrt::Values &values) override
    {
        const auto  found = values.find(1);
        const auto *set
            = found == values.end() ? nullptr : std::get_if<visionrt::DataRef<visionrt::TrainingSet>>(&found->second);
        if (!set)
            return visionrt::toolResult({}, {-2, "训练集缺失"});
        const auto *field = set->get().records.empty() ? nullptr : &set->get().records.begin()->second;
        if (!field)
            return visionrt::toolResult({}, {-2, "训练记录缺失"});
        const auto number = [&](visionrt::PortId port, std::int64_t fallback)
        {
            const auto  entry = values.find(port);
            const auto *value = entry == values.end() ? nullptr : std::get_if<std::int64_t>(&entry->second);
            return value ? *value : fallback;
        };
        const auto fieldId   = number(2, 0);
        const auto parameter = number(3, 0);
        const auto literal   = number(4, 0);
        if (!field->fields.contains(static_cast<visionrt::TrainingFieldId>(fieldId)))
            return visionrt::toolResult({}, {-3, "字段编号未传达到训练器"});
        for (const auto &[id, record] : set->get().records)
            if (const auto text = record.fields.find(2); text != record.fields.end())
                if (const auto *label = std::get_if<std::string>(&text->second); label && *label == "失败")
                    return visionrt::toolResult({}, {-4, "训练器按样本要求失败"});

        for (const auto &[id, record] : set->get().records)
            if (const auto text = record.fields.find(2); text != record.fields.end())
                if (const auto *label = std::get_if<std::string>(&text->second); label && *label == "等待")
                    std::this_thread::sleep_for(std::chrono::milliseconds(150));
        const std::string model = "PROBEMODEL";
        visionrt::Values  outputs;
        outputs.emplace(2, visionrt::Array::dense(visionrt::DType::UInt8, {static_cast<std::int64_t>(model.size())},
                                                  std::vector<std::uint8_t>(model.begin(), model.end())));
        outputs.emplace(3, static_cast<std::int64_t>(parameter + literal));
        for (const auto &[id, record] : set->get().records)
        {
            const auto found = record.fields.find(static_cast<visionrt::TrainingFieldId>(fieldId));
            if (found == record.fields.end())
                continue;
            const auto *label = std::get_if<std::string>(&found->second);
            if (label && *label == "非法")
                outputs[3] = std::string("invalid integer output");
            if (label && *label == "越界")
                outputs[3] = std::int64_t{200};
            if (label && *label == "空模型")
                outputs[2] = visionrt::Array::empty(visionrt::DType::UInt8, 1);
        }
        outputs.emplace(4, std::string("fitted field=") + std::to_string(fieldId)
                               + " records=" + std::to_string(set->get().records.size()));
        return visionrt::toolResult(std::move(outputs), {0, "ok"});
    }

    visionrt::ToolStatus release() override
    {
        return {};
    }
};

// The business tool the publisher writes into. Its model target is initialization parameter 7, a
// non-zero number no host rule knows, and its parameter write lands on an ordinary literal input.
class TrainableProbe final : public visionrt::IToolInstance
{
public:
    visionrt::ToolStatus initialize(const visionrt::Values &values) override
    {
        const auto  found = values.find(7);
        const auto *path  = found == values.end() ? nullptr : std::get_if<std::string>(&found->second);
        if (!path)
            return {};
        // The model is only the file the host derived from the published revision; reading it here
        // proves the derived path reaches the business instance after a publish or a reopen.
        std::ifstream model(std::filesystem::path(std::u8string(path->begin(), path->end())), std::ios::binary);
        validModel_ = std::string(std::istreambuf_iterator<char>(model), {}) == "PROBEMODEL";
        return {};
    }

    visionrt::ToolResult execute(const visionrt::Values &) override
    {
        visionrt::Values outputs;
        outputs.emplace(2, static_cast<std::int64_t>(validModel_ ? 1 : 0));
        return visionrt::toolResult(std::move(outputs), {0, "ok"});
    }

    visionrt::ToolStatus release() override
    {
        return {};
    }

private:
    bool validModel_ = false;
};

visionrt::IToolInstance *VT_CALL createFit(visionrt::ToolStatus *status) noexcept
{
    if (status)
        *status = {};
    return new FitProbe();
}

visionrt::IToolInstance *VT_CALL createTrainable(visionrt::ToolStatus *status) noexcept
{
    if (status)
        *status = {};
    return new TrainableProbe();
}

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
    const auto fit = registry.add({"ProbeFit", createFit, destroy});
    if (!fit)
        return fit;
    return registry.add({"ProbeTrainable", createTrainable, destroy});
}

const visionrt::ModuleApi g_api{visionrt::MODULE_ABI_MAJOR, sizeof(visionrt::ModuleApi), registerTools};
} // namespace

extern "C" VT_EXPORT const visionrt::ModuleApi *VT_CALL visiontoolsModuleApi() noexcept
{
    return &g_api;
}
