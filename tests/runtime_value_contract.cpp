#include "core/ProjectEditor.h"
#include "runtime/RuntimeEngine.h"

#include <algorithm>
#include <chrono>
#include <functional>
#include <iostream>
#include <thread>

using namespace visionrt;
using namespace std::chrono_literals;

namespace {
void require(bool condition, const char *message)
{
    if (!condition)
        throw std::runtime_error(message);
}

class Function final : public IToolInstance
{
public:
    explicit Function(std::function<ToolResult(const Values &)> function)
        : function_(std::move(function))
    {
    }

    ToolStatus initialize(const Values &) override
    {
        return {};
    }

    ToolResult execute(const Values &inputs) override
    {
        return function_(inputs);
    }

    ToolStatus release() override
    {
        return {};
    }

private:
    std::function<ToolResult(const Values &)> function_;
};

RuntimeUpdate finish(RuntimeEngine &engine)
{
    const auto deadline = std::chrono::steady_clock::now() + 5s;
    while (std::chrono::steady_clock::now() < deadline)
    {
        auto update = engine.pollLatest();
        if (update.completion)
        {
            require(engine.acknowledgeCompletion(*update.completion), "completion acknowledgement rejected");
            return update;
        }
        std::this_thread::sleep_for(1ms);
    }
    throw std::runtime_error("shared-value runtime did not complete");
}

ResultSlot::Snapshot snapshot(const RuntimeUpdate &update, NodeId id)
{
    const auto found
        = std::find_if(update.nodes.begin(), update.nodes.end(), [id](const auto &entry) { return entry.node == id; });
    require(found != update.nodes.end() && found->snapshot, "node publication missing");
    if (!found->snapshot->status)
        throw std::runtime_error(found->snapshot->status.message + ": " + found->snapshot->status.detail);
    return found->snapshot;
}

void nativeGraphSharing(std::size_t batchSize)
{
    cv::Mat parent(static_cast<int>(batchSize) * 4, 6, CV_8UC1, cv::Scalar(7));
    Image   initialImages;
    for (std::size_t batch = 0; batch < batchSize; ++batch)
        initialImages.push_back(parent(cv::Rect(1, static_cast<int>(batch) * 4 + 1, 3, 2)));
    Values values{
        {"images", DataRef<Image>(std::move(initialImages))},
        {"numbers", Array(NestedVector<std::int64_t, 2>{{9007199254740993LL}, {}})},
        {"regions", DataRef<ROI>(ROI{{{3., 1., 2., 3.}}})}
    };
    ToolDefinition source;
    source.id          = "test.shared.source";
    source.displayName = "共享数据源";
    source.version     = "1";
    source.kind        = ToolKind::Cpp;
    source.outputs     = {
        { "images", "图像",                  {Type::Image}},
        {"numbers", "数值", {Type::Array, DType::Int64, 2}},
        {"regions", "区域",                    {Type::ROI}}
    };
    auto mutator        = source;
    mutator.id          = "test.shared.mutator";
    mutator.displayName = "原地修改";
    mutator.inputs      = source.outputs;
    ToolRegistry registry;
    registry.addDefinition(source);
    registry.addDefinition(mutator);
    registry.registerCpp(
        source, [values](const ToolServices &)
        { return std::make_unique<Function>([values](const Values &) { return ToolResult{{}, values}; }); });
    registry.registerCpp(
        mutator,
        [&](const ToolServices &)
        {
            return std::make_unique<Function>(
                [&](const Values &inputs)
                {
                    auto &images  = std::get<DataRef<Image>>(inputs.at("images")).get();
                    auto &numbers = std::get<Array>(inputs.at("numbers")).get<NestedVector<std::int64_t, 2>>();
                    auto &regions = std::get<DataRef<ROI>>(inputs.at("regions")).get();
                    require(&images == &std::get<DataRef<Image>>(values.at("images")).get(),
                            "runtime copied Image collection");
                    for (std::size_t batch = 0; batch < batchSize; ++batch)
                        require(images[batch].data == parent.ptr(static_cast<int>(batch) * 4 + 1) + 1
                                    && images[batch].step[0] == parent.step[0],
                                "runtime copied padded pixels");
                    require(&numbers == &std::get<Array>(values.at("numbers")).get<NestedVector<std::int64_t, 2>>(),
                            "runtime copied Array container");
                    require(&regions == &std::get<DataRef<ROI>>(values.at("regions")).get(),
                            "runtime copied ROI container");
                    ++images[0].at<std::uint8_t>(0, 0);
                    images.push_back(cv::Mat::ones(1, 1, CV_8UC1));
                    numbers[0].push_back(42);
                    numbers.emplace_back();
                    regions.push_back({
                        {0., 1., 2.}
                    });
                    return ToolResult{{}, inputs};
                });
        });
    ProjectEditor editor;
    const auto    producer = editor.addNode("flow_1", *registry.find(source.id), {});
    const auto    consumer = editor.addNode("flow_1", *registry.find(mutator.id), {1, 0});
    for (const auto &port : mutator.inputs)
        editor.setBinding("flow_1", consumer, port.id,
                          {
                              BindingMode::Upstream, Missing{},
                               SourcePort{producer, port.id}
        });
    RuntimeEngine engine(2);
    require(engine.start(editor.project(), registry, RunMode::Once).code == 0, "shared graph rejected");
    const auto first       = finish(engine);
    const auto oldSource   = snapshot(first, producer);
    const auto firstResult = snapshot(first, consumer);
    require(parent.at<std::uint8_t>(1, 1) == 8, "tool mutation did not reach producer storage");
    require(std::get<DataRef<Image>>(oldSource->outputs.at("images")).get().size() == batchSize + 1,
            "source publication froze mutable Image collection");
    require(std::get<Array>(oldSource->outputs.at("numbers")).get<NestedVector<std::int64_t, 2>>().size() == 3,
            "source publication froze mutable Array");
    require(firstResult->inputUses.size() == 3, "input provenance lost");
    require(std::all_of(firstResult->inputUses.begin(), firstResult->inputUses.end(), [&](const auto &input)
                        { return input.sourceNode == producer && input.sourcePublication == oldSource->publication; }),
            "input provenance changed with shared writes");
    require(engine.start(editor.project(), registry, RunMode::Node, consumer).code == 0,
            "shared writes invalidated publication without configuration changes");
    const auto second = finish(engine);
    const auto next   = snapshot(second, consumer);
    require(next->publication != firstResult->publication, "new execution did not publish a fresh result");
    require(snapshot(second, producer)->publication == oldSource->publication, "manual consumer reran its source");
    require(parent.at<std::uint8_t>(1, 1) == 9, "second tool mutation did not share producer storage");
    require(std::get<DataRef<Image>>(oldSource->outputs.at("images")).get().size() == batchSize + 2,
            "old publication incorrectly claims frozen payloads");
    require(std::get<DataRef<ROI>>(oldSource->outputs.at("regions")).get().size() == 3, "ROI shared mutation lost");
    std::cout << "Runtime forwards " << batchSize
              << " writable images, Array/ROI storage, padded pixels and publication provenance\n";
}

}

int main()
try
{
    for (const auto count : {1, 8, 32}) nativeGraphSharing(count);
    return 0;
}
catch (const std::exception &error)
{
    std::cerr << error.what() << '\n';
    return 1;
}
