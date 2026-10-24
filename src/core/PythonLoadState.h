#pragma once

#include "visionrt/contracts/Tool.h"

#include <cstdint>
#include <mutex>
#include <optional>

namespace visionrt {

class PythonLoadState
{
public:
    PythonLoadState() = default;
    explicit PythonLoadState(ToolStatus status, std::uint64_t revision = 0)
        : status_(std::move(status))
        , revision_(revision)
    {
    }

    struct Snapshot
    {
        ToolStatus    status;
        std::uint64_t revision{0};
    };

    Snapshot snapshot() const
    {
        std::lock_guard lock(mutex_);
        return {status_, revision_};
    }

    ToolStatus status() const
    {
        std::lock_guard lock(mutex_);
        return status_;
    }

    std::uint64_t revision() const
    {
        std::lock_guard lock(mutex_);
        return revision_;
    }

    bool update(ToolStatus newStatus, std::optional<std::uint64_t> expectedRevision = std::nullopt)
    {
        std::lock_guard lock(mutex_);
        if (expectedRevision.has_value() && *expectedRevision != revision_)
            return false;
        status_ = std::move(newStatus);
        ++revision_;
        return true;
    }

    void set(ToolStatus newStatus, std::uint64_t revision)
    {
        std::lock_guard lock(mutex_);
        status_   = std::move(newStatus);
        revision_ = revision;
    }

private:
    mutable std::mutex mutex_;
    ToolStatus         status_;
    std::uint64_t      revision_{0};
};

} // namespace visionrt
