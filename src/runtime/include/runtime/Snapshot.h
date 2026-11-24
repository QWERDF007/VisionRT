#pragma once

#include "core/Project.h"
#include "core/ExecutionMetrics.h"

#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstdint>
#include <memory>
#include <mutex>
#include <stop_token>

namespace visionrt {
#define VRT_ENUM_FILE "runtime/Snapshot.def"
#include <visionrt/contracts/EnumDeclarations.h>

struct NodeDiagnostic
{
    NodeId      node;
    std::string toolId;
    NodePhase   phase;
    ToolStatus  status;
    DiagnosticOrigin origin = DiagnosticOrigin::System;
};

using Timing = ExecutionMetrics;

struct InputUse
{
    PortId        inputPort;
    NodeId        sourceNode        = 0;
    PortId        sourcePort        = 0;
    std::uint64_t sourcePublication = 0;
};

struct NodeSnapshot
{
    NodeId        node              = 0;
    std::string   toolId;
    std::uint64_t generation        = 0;
    std::uint64_t session           = 0;
    std::uint64_t round             = 0;
    std::uint64_t publication       = 0;
    std::uint64_t executionRevision = 0;
    ToolStatus    status;
    // Identifies the origin of the terminal result for diagnostics. Every terminal snapshot
    // publishes the node status ports, including failures before execute.
    bool          toolResultPublished = false;
    NodeDisposition       disposition = NodeDisposition::Succeeded;
    NodePhase             phase = NodePhase::Execute;
    TypeMap               outputTypes;
    ProducedBy    invocation = ProducedBy::Execute;
    Values        outputs;
    Timing        timing;
    // The measured execution window on the steady clock, in nanoseconds. The runtime derives a
    // flow's span from these instead of running a second timer, so a single-node flow reports
    // exactly the node's own total.
    std::int64_t  startedNs = 0;
    std::int64_t  endedNs   = 0;
    Values        roiInputs;
    std::vector<InputUse> inputUses;
};

class ResultSlot
{
public:
    using Snapshot = std::shared_ptr<const NodeSnapshot>;

    Snapshot latest() const noexcept
    {
        return latest_.load(std::memory_order_acquire);
    }

    void publish(Snapshot value)
    {
        Snapshot previous;
        {
            std::lock_guard lock(mutex_);
            previous = latest_.exchange(std::move(value), std::memory_order_acq_rel);
        }
        changed_.notify_all();
    }

    template<class Predicate>
    Snapshot waitUntil(std::chrono::steady_clock::time_point deadline, Predicate accepts)
    {
        std::unique_lock lock(mutex_);
        Snapshot         value;
        changed_.wait_until(lock, deadline,
                            [&]
                            {
                                value = latest();
                                return value && accepts(*value);
                            });
        return value && accepts(*value) ? value : Snapshot{};
    }

    template<class Predicate>
    Snapshot waitUntil(std::chrono::steady_clock::time_point deadline, Predicate accepts, std::stop_token stop)
    {
        std::unique_lock lock(mutex_);
        Snapshot         value;
        changed_.wait_until(lock, stop, deadline,
                            [&]
                            {
                                value = latest();
                                return value && accepts(*value);
                            });
        if (stop.stop_requested())
            return {};
        return value && accepts(*value) ? value : Snapshot{};
    }

private:
    std::atomic<Snapshot>         latest_;
    std::mutex                    mutex_;
    std::condition_variable_any   changed_;
};
} // namespace visionrt
