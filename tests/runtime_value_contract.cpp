#include "core/ProjectEditor.h"
#include "runtime/RuntimeEngine.h"

#include <algorithm>
#include <chrono>
#include <condition_variable>
#include <functional>
#include <array>
#include <iostream>
#include <mutex>
#include <set>
#include <thread>

using namespace visionrt;
using namespace std::chrono_literals;

namespace {
void require(bool condition, const char *message)
{
    if (!condition)
        throw std::runtime_error(message);
}

// Mirrors a tool that forwards every declared business output while ignoring the
// host-owned status input.
Values businessOutputs(const Values &inputs)
{
    Values outputs;
    for (const auto &[id, value] : inputs)
        if (!isHostOwnedPort(id))
            outputs.emplace(id, value);
    return outputs;
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
        { return std::make_unique<Function>([values](const Values &) { return toolResult(values, {}); }); });
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
                    return toolResult(businessOutputs(inputs), {});
                });
        });
    ProjectEditor editor;
    const auto    producer = editor.addNode("flow_1", *registry.find(source.id), {});
    const auto    consumer = editor.addNode("flow_1", *registry.find(mutator.id), {0, 1});
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

void explicitToolStoragePolicies()
{
    cv::Mat        storage(5, 8, CV_16UC1, cv::Scalar(7));
    DataRef<Image> images(Image{storage(cv::Rect(2, 1, 4, 3))});
    ToolDefinition source;
    source.id          = "test.policy.source";
    source.displayName = "存储策略数据源";
    source.version     = "1";
    source.outputs     = {
        {"images", "图像", {Type::Image}}
    };
    ToolRegistry registry;
    registry.addDefinition(source);
    registry.registerCpp(source,
                         [images](const ToolServices &) {
                             return std::make_unique<Function>([images](const Values &)
                                                               { return toolResult({{"images", images}}, {}); });
                         });
    ProjectEditor       editor;
    const auto          producer = editor.addNode("flow_1", *registry.find(source.id), {});
    std::vector<NodeId> branches;
    for (const auto &policy : {std::string("inplace"), std::string("clone"), std::string("fresh")})
    {
        auto definition   = source;
        definition.id     = "test.policy." + policy;
        definition.inputs = source.outputs;
        registry.addDefinition(definition);
        registry.registerCpp(
            definition,
            [images, policy](const ToolServices &)
            {
                return std::make_unique<Function>(
                    [images, policy](const Values &inputs)
                    {
                        const auto &input = std::get<DataRef<Image>>(inputs.at("images"));
                        require(&input.get() == &images.get() && input.get()[0].data == images.get()[0].data,
                                "host chose a storage policy instead of forwarding the shared input");
                        require(input.get()[0].step[0] == 16, "host discarded padded image layout");
                        if (policy == "inplace")
                        {
                            ++input.get()[0].at<std::uint16_t>(0, 0);
                            return toolResult(businessOutputs(inputs), {});
                        }
                        cv::Mat output
                            = policy == "clone" ? input.get()[0].clone() : cv::Mat(3, 4, CV_16UC1, cv::Scalar(42));
                        if (policy == "clone")
                            output.at<std::uint16_t>(0, 0) += 10;
                        return toolResult({{"images", DataRef<Image>(Image{std::move(output)})}}, {});
                    });
            });
        const auto node = editor.addNode("flow_1", *registry.find(definition.id), {static_cast<int>(branches.size() * 10), static_cast<int>((branches.size() + 1) * 10)});
        editor.setBinding("flow_1", node, "images",
                          {
                              BindingMode::Upstream, Missing{},
                              SourcePort{branches.empty() ? producer : branches.front(), "images"}
        });
        branches.push_back(node);
    }
    RuntimeEngine engine(3);
    require(bool(engine.start(editor.project(), registry, RunMode::Once)), "storage policy graph rejected");
    const auto  result      = finish(engine);
    const auto  publication = snapshot(result, producer);
    const auto  inPlace     = snapshot(result, branches[0]);
    const auto  cloned      = snapshot(result, branches[1]);
    const auto  fresh       = snapshot(result, branches[2]);
    const auto &shared      = std::get<DataRef<Image>>(inPlace->outputs.at("images"));
    const auto &copied      = std::get<DataRef<Image>>(cloned->outputs.at("images"));
    const auto &created     = std::get<DataRef<Image>>(fresh->outputs.at("images"));
    require(&shared.get() == &images.get() && shared.get()[0].data == images.get()[0].data,
            "in-place tool output lost shared storage");
    require(&copied.get() != &images.get() && copied.get()[0].data != images.get()[0].data
                && &created.get() != &images.get() && created.get()[0].data != images.get()[0].data
                && copied.get()[0].data != created.get()[0].data,
            "explicit clone and new-output tools did not own independent storage");
    require(storage.at<std::uint16_t>(1, 2) == 8 && copied.get()[0].at<std::uint16_t>(0, 0) == 18
                && created.get()[0].at<std::uint16_t>(0, 0) == 42,
            "tool-selected storage policies produced incorrect pixels");
    require(copied.get()[0].at<std::uint16_t>(2, 3) == 7
                && std::get<DataRef<Image>>(publication->outputs.at("images")).get()[0].at<std::uint16_t>(0, 0) == 8,
            "explicit clone changed unrelated pixels or froze the old source result");
    std::cout << "Storage policies: shared=" << static_cast<const void *>(images.get()[0].data)
              << " clone=" << static_cast<const void *>(copied.get()[0].data)
              << " fresh=" << static_cast<const void *>(created.get()[0].data) << " pixels=8/18/42 stride=16\n";
}

void toolCoordinatedBranches()
{
    struct Coordination
    {
        std::mutex                pixels;
        std::mutex                arrivals;
        std::condition_variable   ready;
        std::size_t               phase = 0;
        std::set<std::thread::id> workers;
    };

    const auto     coordination = std::make_shared<Coordination>();
    cv::Mat        storage(5, 8, CV_16UC1, cv::Scalar(7));
    DataRef<Image> images(Image{storage(cv::Rect(2, 1, 4, 3))});
    ToolDefinition source;
    source.id          = "test.coordinated.source";
    source.displayName = "工具共同协调";
    source.version     = "1";
    source.outputs     = {
        {"images", "图像", {Type::Image}}
    };
    ToolRegistry registry;
    registry.addDefinition(source);
    registry.registerCpp(source,
                         [images](const ToolServices &) {
                             return std::make_unique<Function>([images](const Values &)
                                                               { return toolResult({{"images", images}}, {}); });
                         });
    ProjectEditor       editor;
    const auto          producer = editor.addNode("flow_1", *registry.find(source.id), {});
    std::vector<NodeId> branches;
    for (const auto &policy : {std::string("writer_a"), std::string("writer_b"), std::string("independent")})
    {
        auto definition   = source;
        definition.id     = "test.coordinated." + policy;
        definition.inputs = source.outputs;
        registry.addDefinition(definition);
        registry.registerCpp(
            definition,
            [images, coordination, policy](const ToolServices &)
            {
                return std::make_unique<Function>(
                    [images, coordination, policy](const Values &inputs)
                    {
                        const auto &input = std::get<DataRef<Image>>(inputs.at("images"));
                        require(&input.get() == &images.get() && input.get()[0].data == images.get()[0].data,
                                "host isolated shared branches instead of forwarding their handles");
                        {
                            std::unique_lock lock(coordination->arrivals);
                            const auto       phase = coordination->phase;
                            require(coordination->workers.insert(std::this_thread::get_id()).second,
                                    "ready branches did not enter distinct executor workers");
                            if (coordination->workers.size() == 3)
                            {
                                coordination->workers.clear();
                                ++coordination->phase;
                                coordination->ready.notify_all();
                            }
                            else
                                require(coordination->ready.wait_for(lock, 3s,
                                                                     [&] { return coordination->phase != phase; }),
                                        "host serialized independent ready branches before tool-owned coordination");
                        }
                        if (policy == "independent")
                            return toolResult({{"images", DataRef<Image>(Image{cv::Mat(3, 4, CV_16UC1, cv::Scalar(50000))})}}, {});
                        std::lock_guard lock(coordination->pixels);
                        for (int increment = 0; increment < 100; ++increment) ++input.get()[0].at<std::uint16_t>(0, 0);
                        return toolResult(businessOutputs(inputs), {});
                    });
            });
        const auto node = editor.addNode("flow_1", *registry.find(definition.id), {static_cast<int>(branches.size() * 10), static_cast<int>((branches.size() + 1) * 10)});
        editor.setBinding("flow_1", node, "images",
                          {
                              BindingMode::Upstream, Missing{},
                               SourcePort{producer, "images"}
        });
        branches.push_back(node);
    }
    RuntimeEngine        engine(3);
    ResultSlot::Snapshot firstPublication;
    for (std::size_t round = 1; round <= 64; ++round)
    {
        require(bool(engine.start(editor.project(), registry, RunMode::Once)), "coordinated graph rejected");
        const auto result      = finish(engine);
        const auto publication = snapshot(result, producer);
        if (!firstPublication)
            firstPublication = publication;
        for (std::size_t branch = 0; branch < branches.size(); ++branch)
        {
            const auto  outcome = snapshot(result, branches[branch]);
            const auto &output  = std::get<DataRef<Image>>(outcome->outputs.at("images"));
            require(outcome->inputUses.size() == 1 && outcome->inputUses[0].sourceNode == producer
                        && outcome->inputUses[0].sourcePublication == publication->publication,
                    "tool coordination changed upstream publication provenance");
            if (branch < 2)
                require(&output.get() == &images.get() && output.get()[0].data == images.get()[0].data
                            && output.get()[0].at<std::uint16_t>(0, 0) == 7 + round * 200,
                        "shared tool mutex did not produce deterministic in-place updates");
            else
                require(&output.get() != &images.get() && output.get()[0].data != images.get()[0].data
                            && output.get()[0].at<std::uint16_t>(0, 0) == 50000,
                        "independent output was not isolated from shared writers");
        }
        require(coordination->phase == round && coordination->workers.empty(),
                "a graph round did not rendezvous all three tool branches");
    }
    require(storage.at<std::uint16_t>(1, 2) == 12807 && storage.at<std::uint16_t>(1, 3) == 7
                && std::get<DataRef<Image>>(firstPublication->outputs.at("images")).get()[0].at<std::uint16_t>(0, 0)
                       == 12807,
            "old shared publication did not observe tool-controlled writes");
    std::cout << "Tool-owned coordination: rounds=64 parallel_workers=3 common_pixel_mutex=1"
                 " shared_writes=12800 final=12807 independent=50000\n";
}
}


// An ROI seed is stored in reference coordinates while the cursor is measured in the current image.
// The bounds helper is the single place that maps one frame into the other, so a translated,
// rotated or scaled alignment must never move a nearby seed away from the pointer.
void assistSeedFrame()
{
    const std::vector<double> rectangle{2.0, 10.0, 20.0, 4.0, 4.0, 0.0};
    const std::array<double, 9> identity{1, 0, 0, 0, 1, 0, 0, 0, 1};
    const auto                 same = transformedRoiBounds(rectangle, identity);
    require(same[0] == cv::Point2d(10.0, 20.0) && same[1] == cv::Point2d(14.0, 24.0),
            "identity alignment changed the reference geometry");

    const std::array<double, 9> translated{1, 0, 100, 0, 1, -50, 0, 0, 1};
    const auto                 moved = transformedRoiBounds(rectangle, translated);
    require(moved[0] == cv::Point2d(110.0, -30.0) && moved[1] == cv::Point2d(114.0, -26.0),
            "translated alignment did not move the seed into the image frame");

    const std::array<double, 9> scaled{2, 0, 0, 0, 2, 0, 0, 0, 1};
    const auto                 grown = transformedRoiBounds(rectangle, scaled);
    require(grown[0] == cv::Point2d(20.0, 40.0) && grown[1] == cv::Point2d(28.0, 48.0),
            "scaled alignment did not scale the seed");

    // A quarter turn keeps the size and moves the box: the pointer must still hit it where it is.
    const std::array<double, 9> rotated{0, -1, 0, 1, 0, 0, 0, 0, 1};
    const auto                 turned = transformedRoiBounds(rectangle, rotated);
    require(std::abs(turned[0].x + 24.0) < 1e-9 && std::abs(turned[0].y - 10.0) < 1e-9
                && std::abs(turned[1].x + 20.0) < 1e-9 && std::abs(turned[1].y - 14.0) < 1e-9,
            "rotated alignment did not report the seed bounds in the image frame");

    // A circle keeps its shape, so its radius follows the alignment scale instead of collapsing.
    const std::vector<double> circle{3.0, 0.0, 0.0, 5.0};
    const auto                ring = transformedRoiBounds(circle, scaled);
    require(ring[0] == cv::Point2d(-10.0, -10.0) && ring[1] == cv::Point2d(10.0, 10.0),
            "circle bounds ignored the alignment scale");

    // A non-uniform alignment turns the seed into an ellipse: the hit area must follow the axis
    // that was actually scaled instead of reporting a circle of the larger radius.
    const std::array<double, 9> stretched{2, 0, 0, 0, 1, 0, 0, 0, 1};
    const auto                  oval = transformedRoiBounds(circle, stretched);
    require(oval[0] == cv::Point2d(-10.0, -5.0) && oval[1] == cv::Point2d(10.0, 5.0),
            "non-uniform alignment reported a circle instead of the true ellipse bounds");

    // A singular alignment has no image for the seed. The contract must name it, so the assist path
    // can treat it as "not under the cursor" instead of hitting a coordinate that does not exist.
    const std::array<double, 9> singular{1, 0, 0, 0, 1, 0, 1, 0, 0};
    bool                        diagnosed = false;
    try { transformedRoiBounds(circle, singular); }
    catch (const ContractError &error)
    {
        diagnosed = std::string_view(error.what()).find("singular") != std::string_view::npos;
    }
    require(diagnosed, "a projective singularity was not diagnosed");
    std::cout << "Assist seeds are compared in the current image frame\n";
}

int main()
try
{
    for (const auto count : {1, 8, 32}) nativeGraphSharing(count);
    explicitToolStoragePolicies();
    toolCoordinatedBranches();
    assistSeedFrame();
    return 0;
}
catch (const std::exception &error)
{
    std::cerr << error.what() << '\n';
    return 1;
}
