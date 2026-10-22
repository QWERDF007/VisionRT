#pragma once

#include "core/Project.h"

#include <atomic>
#include <chrono>
#include <condition_variable>
#include <memory>
#include <mutex>
#include <stop_token>

namespace visionrt {
enum class NodePhase
{
    Create,
    Prepare,
    Initialize,
    Execute,
    Convert,
    Release
};

struct NodeDiagnostic
{
    NodeId      node;
    std::string toolId;
    NodePhase   phase;
    ToolStatus  status;
};

class IToolFailureContext
{
public:
    virtual ~IToolFailureContext()                  = default;
    virtual NodePhase failurePhase() const noexcept = 0;
};

struct ExecutionMetrics
{
    double totalMs          = 0; // 总墙钟
    double prepareMs        = 0; // 参数准备墙钟
    double initMs           = 0; // 初始化墙钟
    double adapterExecuteMs = 0; // 工具Adapter整次execute墙钟（含Python桥接）
    bool   toolCallMeasured = false; // 本次执行是否真正进入工具调用边界
    double validateMs       = 0; // 结果统一检查与格式验证墙钟
    // Python 包含项（已包含在 adapterExecuteMs 中，不能再累加到 totalMs）
    double waitGilMs      = 0; // Python 获取 GIL 等待时间
    double pythonInputMs  = 0; // Python 输入映射耗时
    double pythonCallMs   = 0; // Python 函数/类执行耗时
    double pythonOutputMs = 0; // Python 输出复制与转换耗时
    // 兼容别名
    double executeMs = 0;
    double convertMs = 0;
};

using Timing = ExecutionMetrics;

class IPythonExecutionMetrics
{
public:
    virtual ~IPythonExecutionMetrics()             = default;
    virtual ExecutionMetrics pythonMetrics() const = 0;
};

struct InputUse
{
    std::string   inputPort;
    NodeId        sourceNode      = 0;
    std::string   sourcePort;
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
    NodePhase             phase = NodePhase::Execute;
    TypeMap               outputTypes;
    Values        outputs;
    Timing        timing;
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
