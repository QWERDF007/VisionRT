#pragma once

#include "core/ToolRegistry.h"
#include "runtime/Snapshot.h"

#include <functional>
#include <optional>
#include <string>
#include <vector>
#include <variant>

namespace visionrt {
enum class RunMode
{
    Once,
    Loop,
    Node
};
enum class RuntimeState
{
    Idle,
    RunningOnce,
    RunningLoop,
    RunningNode,
    Stopping,
    Resetting,
    Refreshing
};

struct NodeUpdate
{
    NodeId                    node   = 0;
    bool                      active = false;
    ResultSlot::Snapshot      snapshot;
    std::shared_ptr<const NodeDiagnostic> lifecycleDiagnostic;
    std::shared_ptr<const NodeDiagnostic> instanceDiagnostic;
};

struct FlowTiming
{
    std::string flow;
    double      milliseconds = 0;
};

struct RuntimeCompletion
{
    std::uint64_t generation = 0;
    std::uint64_t token      = 0;
};

struct RuntimeReadback
{
    std::uint64_t generation                                = 0;
    std::uint64_t session                                   = 0;
    std::uint64_t round                                     = 0;
    bool          operator==(const RuntimeReadback &) const = default;
};

enum class RuntimePhase
{
    Setup,
    Submission,
    Completion,
    Management,
    Notification
};

struct RuntimeDiagnostic
{
    using Payload = std::variant<ToolStatus, NodeDiagnostic>;

    std::uint64_t generation = 0;
    std::uint64_t sequence   = 0;
    RuntimePhase  phase;
    Payload       payload;

    const ToolStatus &status() const
    {
        if (const auto *node = std::get_if<NodeDiagnostic>(&payload))
            return node->status;
        return std::get<ToolStatus>(payload);
    }
};

struct RuntimeUpdate
{
    RuntimeState                     state      = RuntimeState::Idle;
    std::uint64_t                    generation = 1;
    std::uint64_t                    session    = 0;
    std::uint64_t                    round      = 0;
    // Mode of the running session. Consumers use it to keep single-node timings away from the
    // complete-run statistics instead of guessing from the current UI state.
    RunMode                          mode       = RunMode::Once;
    std::vector<NodeUpdate>          nodes;
    std::vector<FlowTiming>          flows;
    std::optional<double>            roundMilliseconds;
    double                           sessionElapsedMilliseconds = 0;
    // Only events produced since the previous poll, ordered by sequence.
    std::vector<RuntimeDiagnostic>   diagnostics;
    // Events evicted before this poll; reported once by the consumer.
    std::size_t                      droppedDiagnostics = 0;
    std::optional<RuntimeCompletion> completion;
    std::optional<RuntimeReadback>   readback;
};

// No QObject access. Poll at <=30 Hz; the one completion callback only wakes the GUI.
// Completion remains Busy until acknowledged by the GUI after its final mailbox pull.
class RuntimeEngine
{
public:
    explicit RuntimeEngine(std::size_t workers = 0);
    ~RuntimeEngine();
    RuntimeEngine(const RuntimeEngine &)            = delete;
    RuntimeEngine &operator=(const RuntimeEngine &) = delete;
    std::size_t    workers() const noexcept;
    RuntimeState   state() const;
    ToolStatus     start(const ProjectDefinition &project, const ToolRegistry &registry, RunMode mode,
                         std::optional<NodeId> target = {});
    void           requestStop();
    void           setReadbackEnabled(bool enabled);
    bool           acknowledgeReadback(RuntimeReadback readback);
    void           setReadbackCallback(std::function<void(RuntimeReadback)> callback);
    ToolStatus     requestReset();
    // Idle only. Releases Python instances/results before invoking background work.
    ToolStatus     requestRefresh(std::function<ToolStatus()> work);
    ToolStatus     requestSynchronize(const ProjectDefinition &project, const ToolRegistry &registry);
    RuntimeUpdate  pollLatest();
    bool           acknowledgeCompletion(RuntimeCompletion completion);
    void           setCompletionCallback(std::function<void(RuntimeCompletion)> callback);

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};
} // namespace visionrt
