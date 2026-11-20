#pragma once

// A node's execution window as a value, for the UI only. It carries no tool data, no image and no
// pointer into the runtime: the authoritative result is still the ResultSlot snapshot.

#include "core/Project.h"

#include <atomic>
#include <cstdint>
#include <deque>
#include <functional>
#include <mutex>
#include <utility>
#include <vector>

namespace visionrt {

enum class ActivityKind
{
    EnteredExecution,
    LeftExecution
};

struct NodeActivityEvent
{
    std::uint64_t generation = 0;
    std::uint64_t session    = 0;
    std::uint64_t round      = 0;
    std::uint64_t sequence   = 0;
    NodeId        node       = 0;
    ActivityKind  kind       = ActivityKind::EnteredExecution;
};

// A bounded mailbox between the workers and the window. Producers never touch a QObject; the one
// wake callback is coalesced so a burst of nodes costs one queued notification, not one per node.
class ActivityMailbox
{
public:
    using Wake = std::function<void()>;

    explicit ActivityMailbox(Wake wake = {})
        : wake_(std::move(wake))
    {
    }

    void post(NodeActivityEvent event)
    {
        {
            std::lock_guard lock(mutex_);
            if (events_.size() >= capacity_)
            {
                ++dropped_;
                overflowed_ = true;
            }
            else
            {
                event.sequence = ++sequence_;
                events_.push_back(event);
            }
        }
        // A dropped event still wakes the window: the authoritative pollLatest() is what rebuilds
        // the running set, so overflow costs freshness, never correctness.
        bool expected = false;
        if (notified_.compare_exchange_strong(expected, true) && wake_)
            wake_();
    }

    std::vector<NodeActivityEvent> drain()
    {
        std::vector<NodeActivityEvent> events;
        {
            std::lock_guard lock(mutex_);
            // Reset before taking the queue: a producer that runs after this point sees false and
            // wakes the window again, while one that ran before is already in `events`.
            notified_.store(false, std::memory_order_release);
            events.assign(events_.begin(), events_.end());
            events_.clear();
        }
        return events;
    }

    void clear()
    {
        std::lock_guard lock(mutex_);
        events_.clear();
        overflowed_ = false;
        dropped_    = 0;
        notified_.store(false, std::memory_order_release);
    }

    bool     overflowed() const
    {
        std::lock_guard lock(mutex_);
        return overflowed_;
    }

    std::uint64_t dropped() const
    {
        std::lock_guard lock(mutex_);
        return dropped_;
    }

    static constexpr std::size_t capacity() { return capacity_; }

private:
    static constexpr std::size_t capacity_ = 4096;

    mutable std::mutex             mutex_;
    std::deque<NodeActivityEvent>  events_;
    std::uint64_t                  sequence_  = 0;
    std::uint64_t                  dropped_   = 0;
    bool                           overflowed_ = false;
    std::atomic<bool>              notified_{false};
    Wake                           wake_;
};

} // namespace visionrt
