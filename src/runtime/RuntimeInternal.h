#pragma once

#include "runtime/Receiver.h"
#include "runtime/RuntimeEngine.h"

#include <atomic>
#include <map>
#include <stop_token>

namespace visionrt::runtime_detail {
class WaitBudget
{
public:
    explicit WaitBudget(std::size_t workers)
        : capacity_(workers - 1)
    {
    }

    bool acquire() noexcept
    {
        auto used = used_.load(std::memory_order_relaxed);
        while (used < capacity_)
        {
            if (used_.compare_exchange_weak(used, used + 1, std::memory_order_acquire))
                return true;
        }
        return false;
    }

    void release() noexcept
    {
        used_.fetch_sub(1, std::memory_order_release);
    }

private:
    const std::size_t        capacity_;
    std::atomic<std::size_t> used_{0};
};
struct NodeRuntime;

struct FrozenRun
{
    ProjectDefinition                              project;
    ToolRegistry                                   registry;
    RunMode                                        mode;
    std::optional<NodeId>                          target;
    std::vector<std::string>                       selected;
    std::map<NodeId, const NodeDefinition *>       nodes;
    std::map<NodeId, ToolDefinition>               contracts;
    std::map<NodeId, std::shared_ptr<NodeRuntime>> runtimes;
};

struct RoundContext
{
    std::shared_ptr<const FrozenRun>   frozen;
    std::uint64_t                      generation;
    std::uint64_t                      session;
    std::uint64_t                      round;
    std::shared_ptr<std::stop_source>  stop;
};

class NodeReader final : public IResultReader
{
public:
    NodeReader(NodeId receiver, std::shared_ptr<WaitBudget> budget)
        : receiver_(receiver)
        , budget_(std::move(budget))
    {
    }

    void bind(const std::shared_ptr<const RoundContext> &context)
    {
        context_ = context;
    }

    ToolResult receive(const ReceiveRequest &request, ReceiveCursor &cursor) override;

private:
    NodeId                            receiver_;
    std::shared_ptr<WaitBudget>       budget_;
    std::weak_ptr<const RoundContext> context_;
};

struct InstanceConfiguration
{
    InstanceConfiguration(const NodeDefinition &node, const ToolRegistry &registry,
                          const std::filesystem::path &directory);
    bool operator==(const InstanceConfiguration &other) const;

    std::string           toolId;
    std::string           version;
    ToolKind              kind = ToolKind::Cpp;
    ToolAvailability      availability;
    Values                parameters;
    TypeMap               outputs;
    std::filesystem::path directory;
};

struct NodeRuntime
{
    NodeRuntime(NodeId id, std::shared_ptr<WaitBudget> budget)
        : id(id)
        , reader(std::make_shared<NodeReader>(id, std::move(budget)))
    {
    }

    NodeId                                         id;
    ResultSlot                                     slot;
    std::atomic<bool>                              active{false};
    std::atomic<std::shared_ptr<const ToolStatus>> lifecycle;
    std::atomic<std::shared_ptr<const ToolStatus>> preparation;
    std::shared_ptr<NodeReader>                    reader;
    std::unique_ptr<IToolInstance>                 instance;
    std::optional<InstanceConfiguration>           configuration;
    bool                                           initialized          = false;
    std::uint64_t                                  publication          = 0;
    void                                           run(const std::shared_ptr<const RoundContext> &context);
    void configure(const NodeDefinition &node, const ToolRegistry &registry, InstanceConfiguration next, bool retry);
    // Returns the release failure when one was observed; success is nullopt.
    std::optional<ToolStatus>                      release() noexcept;
    void                                           reset() noexcept;
};
} // namespace visionrt::runtime_detail
