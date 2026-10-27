#pragma once

#include "core/ToolDefinition.h"
#include "runtime/Snapshot.h"

#include <QObject>
#include <QString>
#include <QTimer>

#include <atomic>
#include <condition_variable>
#include <filesystem>
#include <memory>
#include <mutex>
#include <optional>
#include <stop_token>
#include <thread>

namespace visionrt {
class ToolRegistry;

// Everything a proposal was learned from. A result may only be applied while every field still
// matches, so a changed parameter, a different image or a new round turns the proposal stale
// instead of letting it land on data the user never pointed at.
struct AssistContext
{
    std::uint64_t        projectEpoch      = 0;
    std::uint64_t        generation        = 0;
    std::string          flow;
    NodeId               node              = 0;
    std::uint64_t        executionRevision = 0;
    int                  imageIndex        = 0;
    ResultSlot::Snapshot source;
    bool operator==(const AssistContext &) const = default;
};

struct AssistRequest
{
    AssistContext context;
    // Helper inputs already resolved to this image; cursor, phase and the search budget are added
    // by the coordinator because they are protocol fields, not declaration mappings.
    Values inputs;
    bool   learn  = false;
    bool   append = false;
};

struct AssistOutcome
{
    AssistContext context;
    bool          learn  = false;
    bool          append = false;
    bool          valid  = false;
    ToolStatus    status;
    QString       quality;
    ROI           preview;  // current image coordinates, preview only
    ROI           proposal; // reference coordinates, ready for a literal commit
    Values        outputs;  // helper outputs, used for the mapped suggestions
};

// Bounded assist channel. One request may run at a time; a newer hover replaces the single pending
// slot, so a mouse storm can never grow a queue, spawn threads or create extra tool instances.
class AssistPreview final : public QObject
{
    Q_OBJECT
public:
    explicit AssistPreview(QObject *parent = nullptr);
    ~AssistPreview() override;

    // GUI thread. Replaces the helper session and drops whatever was queued.
    void        configure(std::shared_ptr<const ToolRegistry> registry, ToolDefinition helper,
                          std::filesystem::path directory);
    bool        configured() const;
    std::string helperId() const;

    // Hover requests are coalesced for 40 ms; learn requests jump the queue.
    // Returns false when a learn request arrived while another learn is running.
    bool request(AssistRequest value);
    // Drops the preview and every queued request. An in-flight algorithm is allowed to finish and
    // its result is discarded.
    void cancel();
    void invalidate();
    // Queues an asynchronous release of the persistent helper instance. The GUI keeps its event
    // loop; busy() stays true until the worker acknowledges with sessionReleased().
    void releaseSession();
    // Starts the asynchronous close: no further work is accepted, the worker performs the final
    // release and then exits. The GUI never waits for it.
    void beginShutdown();
    bool finished() const;
    // Reclaims an already finished worker (the join is immediate) and drops the session.
    void finalizeShutdown();
    // Blocking close for process teardown only.
    void shutdown();
    bool busy() const;

signals:
    void outcomeReady(const visionrt::AssistOutcome &outcome);
    void busyChanged();
    // Emitted on the GUI thread after the worker actually destroyed the instance.
    void sessionReleased();

private:
    struct Session
    {
        std::shared_ptr<const ToolRegistry> registry;
        ToolDefinition                      helper;
        std::filesystem::path               directory;
    };

    struct Job
    {
        std::shared_ptr<const Session> session;
        std::uint64_t                  serial = 0;
        std::uint64_t                  epoch  = 0;
        AssistRequest                  request;
    };

    AssistOutcome execute(const std::shared_ptr<const Session> &session, const AssistRequest &request);
    void          deliver(std::uint64_t serial, std::uint64_t epoch, AssistOutcome outcome);
    void          startNext();
    void          submitHover();
    void          notifyBusy();
    void          workerLoop(std::stop_token stop);
    void          releaseInstance();
    void          acknowledgeRelease(std::uint64_t id);

    mutable std::mutex            mutex_;
    std::condition_variable_any   changed_;
    std::optional<Job>            work_;
    std::unique_ptr<IToolInstance>  instance_;
    std::shared_ptr<const Session>  instanceSession_;

    std::shared_ptr<const Session> session_;
    QTimer                         hover_;
    std::optional<AssistRequest>   hoverRequest_;
    std::optional<AssistRequest>   pending_;
    std::uint64_t                  epoch_          = 0;
    std::uint64_t                  serial_         = 0;
    std::uint64_t                  inFlightSerial_ = 0;
    bool                           inFlight_       = false;
    bool                           inFlightLearn_  = false;
    bool                           shuttingDown_   = false;
    // GUI-owned release barrier: the request id is only cleared when the worker acknowledges the
    // matching id, so busy() stays true for the whole release instead of flickering before it runs.
    std::optional<std::uint64_t>   releasePending_;
    std::uint64_t                  releaseSerial_ = 0;
    std::atomic<std::uint64_t>     releaseRequest_{0};
    std::atomic<bool>              workerFinished_{false};
    std::jthread                   worker_;
};
} // namespace visionrt

Q_DECLARE_METATYPE(visionrt::ROI)
Q_DECLARE_METATYPE(visionrt::AssistOutcome)
