#include "ui/ProjectOperations.h"

#include <utility>

namespace visionrt {

bool ProjectOperations::begin(Action action, QString path)
{
    if (action == Action::None || action_ != Action::None || isClosing())
        return false;
    action_           = action;
    path_             = std::move(path);
    approved_         = action == Action::Reset;
    awaitingDecision_ = false;
    resetStarted_     = false;
    drained_          = false;
    replacement_.reset();
    return true;
}

void ProjectOperations::clearOperation() noexcept
{
    action_           = Action::None;
    path_.clear();
    approved_         = false;
    awaitingDecision_ = false;
    resetStarted_     = false;
    drained_          = false;
}

void ProjectOperations::cancel()
{
    if (action_ == Action::Close)
    {
        closing_           = false;
        closePhase_        = ClosePhase::None;
        closeReadyEmitted_ = false;
    }
    clearOperation();
    replacement_.reset();
}

ProjectOperations::Completion ProjectOperations::complete()
{
    Completion result;
    result.action      = action_;
    result.path        = path_;
    result.replacement = std::move(replacement_);
    clearOperation();
    replacement_.reset();
    return result;
}

void ProjectOperations::beginResetting() noexcept
{
    closing_    = true;
    closePhase_ = ClosePhase::Resetting;
}

void ProjectOperations::failReset() noexcept
{
    cancel();
}

void ProjectOperations::holdForOwners() noexcept
{
    closePhase_   = ClosePhase::AwaitOwners;
    action_       = Action::Close;
    approved_     = true;
    resetStarted_ = true;
}

bool ProjectOperations::markReady() noexcept
{
    closing_    = false;
    closePhase_ = ClosePhase::Ready;
    clearOperation();
    if (closeReadyEmitted_)
        return false;
    closeReadyEmitted_ = true;
    return true;
}

} // namespace visionrt
