#pragma once

#include "core/ToolRegistry.h"
#include "runtime/Snapshot.h"

#include <functional>
#include <optional>
#include <string>
#include <vector>

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

struct RuntimeDiagnostic
{
    std::uint64_t generation = 0;
    std::uint64_t sequence   = 0;
    std::string   message;
    std::optional<NodeDiagnostic> nodeDiagnostic;
};

struct RuntimeUpdate
{
    RuntimeState                     state      = RuntimeState::Idle;
    std::uint64_t                    generation = 1;
    std::uint64_t                    session    = 0;
    std::uint64_t                    round      = 0;
    std::vector<NodeUpdate>          nodes;
    std::vector<FlowTiming>          flows;
    double                           roundMilliseconds = 0;
    // Only events produced since the previous poll, ordered by sequence.
    std::vector<RuntimeDiagnostic>   diagnostics;
    // Events evicted before this poll; reported once by the consumer.
    std::size_t                      droppedDiagnostics = 0;
    std::optional<RuntimeCompletion> completion;
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
    ToolStatus     requestReset();
    // Idle only. Releases Python instances/results before invoking background work.
    ToolStatus     requestRefresh(std::function<ToolStatus()> work);
    ToolStatus     requestSynchronize(const ProjectDefinition &project, const ToolRegistry &registry);
    RuntimeUpdate  pollLatest();
    bool           acknowledgeCompletion(RuntimeCompletion completion);
    void           setCompletionCallback(std::function<void(RuntimeCompletion)> callback);
#ifdef VISIONRT_TEST_PROBES
    void injectDiagnosticForTest(std::string message);
#endif

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};
} // namespace visionrt
