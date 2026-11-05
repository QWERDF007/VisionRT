#pragma once

#include "runtime/RuntimeEngine.h"
#include "ui/Diagnostics.h"
#include "ui/DisplayConverter.h"
#include "ui/ImageViewport.h"
#include "ui/OutputModel.h"
#include "ui/RoiEditSession.h"

#include <QPointer>
#include <QVariantMap>

#include <deque>
#include <functional>
#include <future>
#include <map>
#include <mutex>
#include <optional>
#include <set>
#include <string>

namespace visionrt {

// Facts and effects the presentation needs from the controller. Selection and project structure
// stay owned by AppController: this interface only exposes what one presentation step must read or
// announce, never the controller's mutable state.
class PresentationHost
{
public:
    virtual ~PresentationHost() = default;

    // Current selection. All three come from the same node, never from separate mirrors.
    virtual const NodeDefinition *selectedNode() const                            = 0;
    virtual const FlowDefinition *currentFlow() const                             = 0;
    virtual const ToolDefinition *effectiveTool() const                           = 0;
    virtual const PortDefinition *parameter(PortId id, bool init) const = 0;
    // Same candidate/validate rule the binding menu uses, cached by the controller. The returned map
    // is the cache itself, not a copy.
    virtual const std::map<PortId, bool> *refreshConnectable(const FlowDefinition &flow,
                                                                               const NodeDefinition &node) const = 0;

    // Runtime facts the presentation must not duplicate.
    virtual bool runtimeIdle() const = 0;
    virtual bool closing() const     = 0;

    // Effects that belong to the controller: QML notifications and the user-visible ROI session.
    virtual bool contextCurrent(const QVariantMap &context) const = 0;
    // Rebuilds the live context for a port; a queued read is only served when it still matches.
    virtual QVariantMap portContext(PortId port) const = 0;
    virtual void deliverText(const QString &text)                 = 0;
    virtual void resultsChanged()                                 = 0;
    virtual void runtimeChanged()                                 = 0;
    virtual void endRoiEdit()                                     = 0;
    virtual void reportDiagnostic(const ApplicationDiagnostic &diagnostic) = 0;
    virtual void reportFailure(const std::exception &error, const QString &summary) = 0;
};

// Owns everything the workbench presents for the runtime: node snapshots and role diagnostics, the
// selected node's output rows and image batch, the display request, and the text/readback pipeline
// including the drain that guards them. It composes the existing OutputModel and DisplayConverter
// instead of copying their job, and never edits the project or executes a tool.
class ResultPresentation
{
public:
    ResultPresentation(PresentationHost &host, OutputModel &outputs, RoiEditSession &roi, QObject *parent);
    ~ResultPresentation();
    ResultPresentation(const ResultPresentation &)            = delete;
    ResultPresentation &operator=(const ResultPresentation &) = delete;

    void setRuntime(RuntimeEngine *runtime) noexcept { runtime_ = runtime; }
    void setGeneration(std::uint64_t generation) noexcept { generation_ = generation; }
    // Release completion is part of the drain condition; the controller forwards its helper state.
    void setExternalBusy(std::function<bool()> busy) { externalBusy_ = std::move(busy); }

    // --- snapshots and node roles -------------------------------------------------------------
    const ResultSlot::Snapshot *snapshot(NodeId node) const;
    // Shared owner for consumers that keep the snapshot (assist contexts), never a deep copy.
    ResultSlot::Snapshot        sharedSnapshot(NodeId node) const;
    bool                        active(NodeId node) const;
    std::size_t                 snapshotCount() const noexcept { return snapshots_.size(); }
    const NodeDiagnostic       *lifecycle(NodeId node) const;
    // Shared owner for consumers that pass the diagnostic into the output model.
    std::shared_ptr<const NodeDiagnostic> lifecycleShared(NodeId node) const;
    const NodeDiagnostic       *instance(NodeId node) const;
    ResultSlot::Snapshot        displaySnapshot(const NodeDefinition &node) const;
    // The bound value of an input, read through this module's snapshots. One lookup, no second copy.
    const PortValue *resolvedInput(const NodeDefinition &node, PortId input) const;

    // One polled node. Returns true when its visible role or result changed.
    bool applyNode(NodeId node, bool active, const std::shared_ptr<const NodeDiagnostic> &lifecycle,
                   const std::shared_ptr<const NodeDiagnostic> &instance, ResultSlot::Snapshot snapshot,
                   std::uint64_t generation);
    void forgetNode(NodeId node);
    // Keeps only nodes that still exist in the committed project.
    void retain(const std::set<NodeId> &existing);
    void clear();
    void clearActive() noexcept { active_.clear(); }
    // Drops every presented image/row; the next update repopulates them.
    void clearDisplay();

    // --- selection presentation ---------------------------------------------------------------
    void updateResults();
    void updateDisplay();
    void applyOverlays();

    void           adoptViewport(ImageViewport *target);
    void           adoptThumbnail(ImageViewport *target);
    ImageViewport *viewport() const;

    QVariantList imageOutputs() const;
    int          imageIndex() const;
    int          imageCount() const;
    QString      selectedImageOutput() const;
    bool         setImageIndex(int index);
    bool         setImageOutput(const QString &output);
    // Restores the pre-edit index without the user-command guards; the ROI session owns that rule.
    void restoreImageIndex(int index);

    // --- text and readback --------------------------------------------------------------------
    bool readsAllowed() const;
    // Returns true when the barrier identity changed; a repeated poll is a no-op.
    bool setReadback(const RuntimeReadback *readback);
    bool hasReadback() const { return readback_.has_value(); }
    void updateReadback();
    void requestText(const QVariantMap &context);
    bool textRunning() const { return textTask_.valid(); }

    // --- drain --------------------------------------------------------------------------------
    void beginDrain(std::function<void()> continuation);
    void finishDrain();
    void drainReaders();
    bool drainReady() const;
    bool draining() const noexcept { return draining_; }

private:
    struct TextRequest
    {
        QVariantMap context;
    };

    struct TextResult
    {
        QString       text;
        std::uint64_t generation;
        QVariantMap   context;
    };

    struct TextMailbox
    {
        std::mutex                mutex;
        std::uint64_t             generation = 0;
        bool                      done       = false;
        std::optional<TextResult> value;
        std::exception_ptr        error;
    };

    void             consumeTextMailbox(std::shared_ptr<TextMailbox> mailbox);

    // One entry per real Image port the workbench may present. Visible ports come first; an ROI
    // session also names its own background input so the session stays selectable when that port is
    // not offered to the general selector.
    struct ImageChoice
    {
        PortRef ref;
        QString name;
    };
    std::vector<ImageChoice> imageChoices() const;

    PresentationHost &host_;
    OutputModel      &outputs_;
    RoiEditSession   &roi_;
    DisplayConverter converter_;
    RuntimeEngine   *runtime_ = nullptr;

    std::map<NodeId, ResultSlot::Snapshot>                  snapshots_;
    std::set<NodeId>                                        active_;
    std::map<NodeId, std::shared_ptr<const NodeDiagnostic>> lifecycle_;
    std::map<NodeId, std::shared_ptr<const NodeDiagnostic>> instances_;

    std::optional<PortRef> imageSelection_;
    // The node the current selection was made for. A node that has never been shown picks its first
    // offered image; a node whose chosen port disappeared stays empty instead of picking another.
    // Both are optional: node 0 is a real node, so 0 cannot mean "none".
    std::optional<NodeId> selectionNode_;
    int      index_    = 0;
    int      count_    = 0;
    std::optional<NodeId> displayed_;
    quint64  displayRequest_ = 0;
    RegionGroup                  displayInputs_;
    RegionGroup                  displayOutputs_;
    std::optional<RoiEditTarget> displayRoiTarget_;
    QPointer<ImageViewport>              viewport_;
    std::vector<QPointer<ImageViewport>> thumbnails_;
    std::vector<QPointer<ImageViewport>> displayTargets_;

    std::deque<TextRequest>            readRequests_;
    std::optional<RuntimeReadback>     readback_;
    std::future<void>                  textTask_;
    std::shared_ptr<TextMailbox>       textMailbox_;
    std::atomic<int>                   textPending_{0};
    bool                               draining_ = false;
    std::function<void()>              drainContinuation_;
    std::function<bool()>              externalBusy_;
    std::uint64_t                      generation_ = 1;
};

} // namespace visionrt
