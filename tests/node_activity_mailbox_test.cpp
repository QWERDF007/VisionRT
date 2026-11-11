// The mailbox is the only path from a worker to the window, so its bounds, ordering and wake
// coalescing are contracts rather than implementation details.

#include "runtime/NodeActivity.h"

#include <cassert>
#include <atomic>
#include <cstdint>
#include <iostream>
#include <thread>
#include <vector>

using namespace visionrt;

namespace {

NodeActivityEvent event(NodeId node, ActivityKind kind)
{
    return NodeActivityEvent{1, 2, 3, 0, node, kind};
}

} // namespace

int main()
{
    std::atomic<int> wakes{0};
    ActivityMailbox  mailbox([&] { wakes.fetch_add(1); });

    // A burst of nodes costs one wake, and the queue keeps the producer order.
    mailbox.post(event(7, ActivityKind::EnteredExecution));
    mailbox.post(event(8, ActivityKind::EnteredExecution));
    assert(wakes.load() == 1);
    const auto first = mailbox.drain();
    assert(first.size() == 2);
    assert(first[0].node == 7 && first[1].node == 8);
    assert(first[0].sequence == 1 && first[1].sequence == 2);

    // Draining resets the wake latch: the next post is allowed to notify again.
    assert(mailbox.drain().empty());
    mailbox.post(event(9, ActivityKind::LeftExecution));
    assert(wakes.load() == 2);

    // The queue is bounded; overflow is reported instead of growing without limit.
    mailbox.clear();
    for (std::size_t index = 0; index < ActivityMailbox::capacity() + 10; ++index)
        mailbox.post(event(static_cast<NodeId>(index), ActivityKind::EnteredExecution));
    assert(mailbox.overflowed());
    assert(mailbox.dropped() == 10);
    const auto bounded = mailbox.drain();
    assert(bounded.size() == ActivityMailbox::capacity());
    // The sequence is global and survives a clear; only its order is promised.
    for (std::size_t index = 1; index < bounded.size(); ++index)
        assert(bounded[index].sequence == bounded[index - 1].sequence + 1);

    // A new session clears the record and the overflow flag.
    mailbox.clear();
    assert(!mailbox.overflowed() && mailbox.dropped() == 0 && mailbox.drain().empty());

    // Concurrent producers never lose a stored event and never duplicate a sequence.
    std::vector<std::thread> producers;
    for (int worker = 0; worker < 4; ++worker)
        producers.emplace_back([&]
        {
            for (int index = 0; index < 100; ++index)
                mailbox.post(event(static_cast<NodeId>(index), ActivityKind::EnteredExecution));
        });
    for (auto &producer : producers)
        producer.join();
    const auto concurrent = mailbox.drain();
    assert(concurrent.size() == 400);
    for (std::size_t index = 1; index < concurrent.size(); ++index)
        assert(concurrent[index].sequence == concurrent[index - 1].sequence + 1);

    std::cout << "Node activity mailbox is bounded, ordered and coalesces its wake\n";
    return 0;
}
