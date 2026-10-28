#include "core/ProjectEditor.h"
#include "runtime/RuntimeEngine.h"

#include <array>
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <iostream>
#include <mutex>
#include <optional>

using namespace visionrt;
using namespace std::chrono_literals;

namespace {
void require(bool condition, const char *message)
{
    if (!condition)
        throw std::runtime_error(message);
}

struct Lifetime
{
    std::atomic<int> created   = 0;
    std::atomic<int> released  = 0;
    std::atomic<int> destroyed = 0;
};

class ReleaseFailure final : public IToolInstance
{
public:
    ReleaseFailure(Lifetime &lifetime, std::string message)
        : lifetime_(lifetime)
        , message_(std::move(message))
    {
        ++lifetime_.created;
    }

    ~ReleaseFailure() override
    {
        ++lifetime_.destroyed;
    }

    ToolStatus initialize(const Values &) override
    {
        return {};
    }

    ToolResult execute(const Values &) override
    {
        return {};
    }

    ToolStatus release() override
    {
        ++lifetime_.released;
        return {7, message_, "original release detail"};
    }

private:
    Lifetime   &lifetime_;
    std::string message_;
};
}

int main()
try
{
    Lifetime     lifetime;
    ToolRegistry registry;
    for (const auto &[id, message] : std::array<std::pair<const char *, const char *>, 2>{
             {{"test.diagnostic.a", "工具A释放失败"}, {"test.diagnostic.b", "工具B释放失败"}}
    })
    {
        ToolDefinition definition;
        definition.id          = id;
        definition.displayName = id;
        registry.addDefinition(definition);
        registry.registerCpp(definition, [&, message](const ToolServices &)
                             { return std::make_unique<ReleaseFailure>(lifetime, message); });
    }
    std::mutex                       mutex;
    std::condition_variable          completed;
    std::optional<RuntimeCompletion> notification;
    RuntimeEngine                    engine(1);
    engine.setCompletionCallback(
        [&](RuntimeCompletion completion)
        {
            {
                std::lock_guard lock(mutex);
                notification = completion;
            }
            completed.notify_one();
        });
    const auto settle = [&]
    {
        std::unique_lock lock(mutex);
        require(completed.wait_for(lock, 3s, [&] { return notification.has_value(); }),
                "diagnostic management did not complete");
        const auto completion = *notification;
        notification.reset();
        lock.unlock();
        auto update = engine.pollLatest();
        require(update.completion && update.completion->token == completion.token
                    && update.completion->generation == completion.generation && update.state != RuntimeState::Idle,
                "final diagnostic update was not available before Idle acknowledgement");
        require(engine.acknowledgeCompletion(completion), "diagnostic completion acknowledgement rejected");
        return update;
    };
    ProjectEditor initial;
    const auto    flow = initial.project().flows.front().id;
    for (const auto *id : {"test.diagnostic.a", "test.diagnostic.b", "test.diagnostic.a", "test.diagnostic.a"})
        initial.addNode(flow, *registry.find(id), {0, 0});
    require(engine.requestSynchronize(initial.project(), registry).code == 0, "initial node creation rejected");
    require(settle().diagnostics.empty() && lifetime.created == 4, "initial node creation raised a diagnostic");
    ProjectEditor empty;
    require(engine.requestSynchronize(empty.project(), registry).code == 0, "initial node removal rejected");
    const auto first = settle();
    require(first.diagnostics.size() == 4 && first.droppedDiagnostics == 0,
            "real release diagnostics were lost before first delivery");
    for (std::size_t index = 0; index < first.diagnostics.size(); ++index)
    {
        const auto &event = first.diagnostics[index];
        const auto *node  = std::get_if<NodeDiagnostic>(&event.payload);
        require(event.generation == first.generation && event.sequence == index + 1
                    && event.phase == RuntimePhase::Management && node
                    && node->node == initial.project().flows.front().nodes[index].id
                    // The tool's own release verdict is reported as-is; the host only forwards it.
                    && node->phase == NodePhase::Release && event.status().code == 7
                    && event.status().message == (index == 1 ? "工具B释放失败" : "工具A释放失败")
                    && event.status().detail == "original release detail",
                "release event lost its identity, node, stage, status or detail");
    }
    for (int poll = 0; poll < 100; ++poll)
    {
        const auto unchanged = engine.pollLatest();
        require(unchanged.diagnostics.empty() && unchanged.droppedDiagnostics == 0,
                "unchanged polling replayed an already consumed release event");
    }
    ProjectEditor overflow;
    const auto    overflowFlow = overflow.project().flows.front().id;
    for (int index = 0; index < 101; ++index)
        overflow.addNode(overflowFlow, *registry.find("test.diagnostic.a"), {index, 0});
    require(engine.requestSynchronize(overflow.project(), registry).code == 0, "overflow node creation rejected");
    require(settle().diagnostics.empty(), "overflow node creation raised a diagnostic");
    require(engine.requestSynchronize(empty.project(), registry).code == 0, "overflow node removal rejected");
    const auto truncated = settle();
    std::cout << "Diagnostic overflow: delivered_before=4 produced=101 retained=" << truncated.diagnostics.size()
              << " unread_dropped=" << truncated.droppedDiagnostics << '\n';
    require(truncated.diagnostics.size() == 100 && truncated.droppedDiagnostics == 1,
            "delivered diagnostics were counted as unread events lost to overflow");
    for (std::size_t index = 0; index < truncated.diagnostics.size(); ++index)
    {
        const auto &event = truncated.diagnostics[index];
        const auto *node  = std::get_if<NodeDiagnostic>(&event.payload);
        require(event.generation == first.generation && event.sequence == index + 6 && node
                    && node->node == overflow.project().flows.front().nodes[index + 1].id,
                "overflow lost retained event order or the matching node identity");
    }
    for (int poll = 0; poll < 100; ++poll)
    {
        const auto unchanged = engine.pollLatest();
        require(unchanged.diagnostics.empty() && unchanged.droppedDiagnostics == 0,
                "overflow replayed events or reported the same loss twice");
    }
    require(lifetime.created == 105 && lifetime.released == 105 && lifetime.destroyed == 105,
            "diagnostic delivery retained or released a tool more than once");
    require(engine.requestReset().code == 0, "diagnostic generation reset rejected");
    const auto reset = settle();
    require(reset.generation == first.generation + 1 && reset.diagnostics.empty(), "reset retained old diagnostics");
    require(
        engine.requestRefresh([] { return ToolStatus{9, "新代目录刷新失败", "original refresh detail"}; }).code == 0,
        "new generation refresh rejected");
    const auto next = settle();
    require(next.diagnostics.size() == 1 && next.droppedDiagnostics == 0
                && next.diagnostics[0].generation == reset.generation && next.diagnostics[0].sequence == 1
                && next.diagnostics[0].status().message == "新代目录刷新失败"
                && next.diagnostics[0].status().detail == "original refresh detail",
            "old diagnostic cursor swallowed the first event of a new generation");
    require(
        engine.requestRefresh([] { return ToolStatus{9, "新代目录刷新失败", "original refresh detail"}; }).code == 0,
        "second same-generation refresh rejected");
    const auto refreshed = settle();
    require(refreshed.diagnostics.size() == 1, "same-text refresh event disappeared");
    std::cout << "Diagnostic refresh identity: generation=" << refreshed.generation
              << " previous_sequence=" << next.diagnostics[0].sequence
              << " next_sequence=" << refreshed.diagnostics[0].sequence << '\n';
    require(refreshed.generation == next.generation && refreshed.diagnostics[0].sequence == 2,
            "refresh reused a diagnostic identity in the same generation");
    ProjectEditor execution;
    execution.addNode(execution.project().flows.front().id, *registry.find("test.diagnostic.a"), {0, 0});
    require(engine.start(execution.project(), registry, RunMode::Once).code == 0, "diagnostic identity run rejected");
    require(settle().diagnostics.empty(), "successful execution emitted an unexpected management diagnostic");
    require(engine.requestSynchronize(empty.project(), registry).code == 0, "post-run node removal rejected");
    const auto afterRun = settle();
    require(afterRun.generation == next.generation && afterRun.diagnostics.size() == 1
                && afterRun.diagnostics[0].sequence == 3 && afterRun.diagnostics[0].status().code == 7,
            "new execution reused a diagnostic identity in the same generation");
    require(lifetime.created == 106 && lifetime.released == 106 && lifetime.destroyed == 106,
            "post-run diagnostic identity test leaked or double-released a tool");
    std::cout << "Diagnostic lifecycle: created=106 released=106 destroyed=106 idle_polls=200 new_generation="
              << next.generation << " sequences=1,2,3\n";
    return 0;
}
catch (const std::exception &error)
{
    std::cerr << error.what() << '\n';
    return 1;
}
