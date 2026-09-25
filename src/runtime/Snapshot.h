#pragma once

#include "core/Project.h"
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <memory>
#include <mutex>

namespace visionrt {
struct Timing {
    double prepareMs = 0;
    double initMs = 0;
    double executeMs = 0;
    double convertMs = 0;
    double totalMs = 0;
    double waitGilMs = 0;
};
struct NodeSnapshot {
    NodeId node = 0;
    std::uint64_t generation = 0;
    std::uint64_t session = 0;
    std::uint64_t round = 0;
    std::uint64_t publication = 0;
    std::uint64_t executionRevision = 0;
    ToolStatus status;
    Values outputs;
    Timing timing;
    Values roiInputs;
};
class ResultSlot {
public:
    using Snapshot = std::shared_ptr<const NodeSnapshot>;
    Snapshot latest() const noexcept { return latest_.load(std::memory_order_acquire); }
    void publish(Snapshot value) {
        Snapshot previous;
        {
            std::lock_guard lock(mutex_);
            previous = latest_.exchange(std::move(value), std::memory_order_acq_rel);
        }
        changed_.notify_all();
    }
    template<class Predicate>
    Snapshot waitUntil(std::chrono::steady_clock::time_point deadline, Predicate accepts) {
        std::unique_lock lock(mutex_);
        Snapshot value;
        changed_.wait_until(lock, deadline, [&] {
            value = latest();
            return value && accepts(*value);
        });
        return value && accepts(*value) ? value : Snapshot{};
    }
private:
    std::atomic<Snapshot> latest_;
    std::mutex mutex_;
    std::condition_variable changed_;
};
}
