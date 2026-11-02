#pragma once

#include "core/Project.h"

#include <QString>

#include <optional>

namespace visionrt {

// Owns the file-operation state machine: the queued new/open/save/close/reset/delete intent, the
// unsaved decision, the drain/reset progress and the close handshake with the external owners.
// It never holds the current project or a runtime state; the controller executes the side effects
// and asks this owner to advance.
class ProjectOperations
{
public:
    enum class Action
    {
        None,
        New,
        Open,
        Close,
        Reset,
        DeleteNodes
    };

    enum class ClosePhase
    {
        None,
        Resetting,
        AwaitOwners,
        Ready
    };

    struct Completion
    {
        Action                           action = Action::None;
        QString                          path;
        std::optional<ProjectDefinition> replacement;
    };

    // Starts one operation. Refuses when another operation or a close is already in progress.
    bool begin(Action action, QString path = {});
    // Abandons the current operation. A cancelled close also clears its handshake so the window can
    // stay open.
    void cancel();
    // Returns the completed intent and hands the replacement project to the caller.
    Completion complete();

    Action         action() const noexcept { return action_; }
    bool           active() const noexcept { return action_ != Action::None; }
    const QString &path() const noexcept { return path_; }

    bool approved() const noexcept { return approved_; }
    void approve() noexcept { approved_ = true; }
    bool awaitingDecision() const noexcept { return awaitingDecision_; }
    void requestDecision() noexcept { awaitingDecision_ = true; }
    void decisionMade() noexcept { awaitingDecision_ = false; }

    bool resetStarted() const noexcept { return resetStarted_; }
    void markResetStarted() noexcept { resetStarted_ = true; }
    bool drained() const noexcept { return drained_; }
    void markDrained() noexcept { drained_ = true; }

    std::optional<ProjectDefinition> &replacement() noexcept { return replacement_; }
    void                              clearReplacement() noexcept { replacement_.reset(); }

    bool        isClosing() const noexcept { return closing_ || closePhase_ != ClosePhase::None; }
    ClosePhase  closePhase() const noexcept { return closePhase_; }
    void        beginResetting() noexcept;
    void        failReset() noexcept;
    // The runtime is shut down but external owners still hold references.
    void        holdForOwners() noexcept;
    // Marks the close ready and clears the close intent; true when closeReady must still be emitted.
    bool        markReady() noexcept;
    bool        closeReadyEmitted() const noexcept { return closeReadyEmitted_; }

private:
    void clearOperation() noexcept;

    Action                           action_           = Action::None;
    QString                          path_;
    bool                             approved_         = false;
    bool                             awaitingDecision_ = false;
    bool                             resetStarted_     = false;
    bool                             drained_          = false;
    bool                             closing_          = false;
    ClosePhase                       closePhase_       = ClosePhase::None;
    bool                             closeReadyEmitted_ = false;
    std::optional<ProjectDefinition> replacement_;
};

} // namespace visionrt
