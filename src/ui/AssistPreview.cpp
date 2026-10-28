#include "ui/AssistPreview.h"

#include "core/ToolRegistry.h"
#include "core/InputNormalizer.h"
#include "core/ToolValueValidation.h"

#include <QMetaObject>
#include <QThread>
#include <cstdio>

#include <stdexcept>
#include <utility>

namespace visionrt {
namespace {
constexpr int kHoverDelayMs = 40;
// Fixed helper protocol fields (S-ASSIST-01/05). They are not declaration mappings: the host
// always supplies the cursor, the phase and the bounded search budget.
constexpr std::string_view kPreview  = "preview_rois";
constexpr std::string_view kProposal = "proposal_rois";
constexpr std::string_view kValid    = "candidate_valid";
constexpr std::string_view kQuality  = "quality";

QString helperMessage(const std::string &message)
{
    return QString::fromUtf8(message.c_str(), static_cast<qsizetype>(message.size()));
}
} // namespace

AssistPreview::AssistPreview(QObject *parent)
    : QObject(parent)
{
    hover_.setSingleShot(true);
    hover_.setInterval(kHoverDelayMs);
    connect(&hover_, &QTimer::timeout, this, &AssistPreview::submitHover);
    worker_ = std::jthread([this](std::stop_token stop) { workerLoop(stop); });
}

AssistPreview::~AssistPreview()
{
    shutdown();
}

void AssistPreview::configure(std::shared_ptr<const ToolRegistry> registry, ToolDefinition helper,
                              std::filesystem::path directory)
{
    Q_ASSERT(QThread::currentThread() == thread());
    normalizeToolDefinition(helper);
    hover_.stop();
    hoverRequest_.reset();
    pending_.reset();
    ++epoch_;
    session_ = std::make_shared<Session>(Session{std::move(registry), std::move(helper), std::move(directory)});
    notifyBusy();
}

bool AssistPreview::configured() const
{
    return session_ != nullptr;
}

std::string AssistPreview::helperId() const
{
    return session_ ? session_->helper.id : std::string{};
}

bool AssistPreview::request(AssistRequest value)
{
    Q_ASSERT(QThread::currentThread() == thread());
    if (!session_ || shuttingDown_)
        return false;
    if (value.learn)
    {
        hover_.stop();
        hoverRequest_.reset();
        // A click that arrived while another learn is running is rejected: its cursor and context
        // can no longer be proven current by the time the second algorithm finishes.
        if (inFlightLearn_)
            return false;
        pending_ = std::move(value);
        startNext();
        notifyBusy();
        return true;
    }
    if (inFlightLearn_ || (pending_ && pending_->learn))
        return true;
    hoverRequest_ = std::move(value);
    if (!hover_.isActive())
        hover_.start();
    notifyBusy();
    return true;
}

void AssistPreview::submitHover()
{
    if (!hoverRequest_)
        return;
    pending_ = std::move(hoverRequest_);
    hoverRequest_.reset();
    startNext();
    notifyBusy();
}

void AssistPreview::cancel()
{
    Q_ASSERT(QThread::currentThread() == thread());
    hover_.stop();
    hoverRequest_.reset();
    pending_.reset();
    // An in-flight result belongs to the cancelled interaction and must not reach the UI.
    ++epoch_;
    notifyBusy();
}

void AssistPreview::invalidate()
{
    cancel();
}

void AssistPreview::releaseSession()
{
    Q_ASSERT(QThread::currentThread() == thread());
    hover_.stop();
    hoverRequest_.reset();
    pending_.reset();
    ++epoch_;
    if (!shuttingDown_ && !releasePending_)
    {
        // One outstanding release per barrier: a repeated request while the worker is still
        // destroying the instance must not queue a second release behind the first.
        const auto id = ++releaseSerial_;
        releasePending_ = id;
        {
            std::lock_guard lock(mutex_);
            releaseRequest_.store(id, std::memory_order_release);
        }
        changed_.notify_all();
    }
    notifyBusy();
}

void AssistPreview::beginShutdown()
{
    if (shuttingDown_)
        return;
    shuttingDown_ = true;
    hover_.stop();
    hoverRequest_.reset();
    pending_.reset();
    ++epoch_;
    {
        std::lock_guard lock(mutex_);
        work_.reset();
        if (!releasePending_)
        {
            const auto id = ++releaseSerial_;
            releasePending_ = id;
            releaseRequest_.store(id, std::memory_order_release);
        }
    }
    worker_.request_stop();
    changed_.notify_all();
    notifyBusy();
}

bool AssistPreview::finished() const
{
    return workerFinished_.load(std::memory_order_acquire) || !worker_.joinable();
}

void AssistPreview::finalizeShutdown()
{
    if (!shuttingDown_ || !finished())
        return;
    if (worker_.joinable())
        worker_.join();
    session_.reset();
    notifyBusy();
}

void AssistPreview::shutdown()
{
    beginShutdown();
    if (worker_.joinable())
        worker_.join();
    workerFinished_.store(true, std::memory_order_release);
    session_.reset();
    notifyBusy();
}

void AssistPreview::acknowledgeRelease(std::uint64_t id)
{
    // Only the acknowledgement for the request the GUI is waiting on clears the barrier; a stale
    // acknowledgement from an earlier session leaves a newer pending release intact.
    if (!releasePending_ || *releasePending_ != id)
        return;
    releasePending_.reset();
    notifyBusy();
}

bool AssistPreview::busy() const
{
    return inFlight_ || pending_.has_value() || hover_.isActive() || releasePending_.has_value();
}

void AssistPreview::startNext()
{
    if (inFlight_ || !pending_ || !session_)
        return;
    auto request = std::move(*pending_);
    pending_.reset();
    inFlight_      = true;
    inFlightLearn_ = request.learn;
    inFlightSerial_ = ++serial_;
    Job job{session_, inFlightSerial_, epoch_, std::move(request)};
    {
        std::lock_guard lock(mutex_);
        work_ = std::move(job);
    }
    changed_.notify_one();
}

void AssistPreview::notifyBusy()
{
    emit busyChanged();
}

void AssistPreview::deliver(std::uint64_t serial, std::uint64_t epoch, AssistOutcome outcome)
{
    if (serial != inFlightSerial_)
        return;
    inFlight_       = false;
    inFlightLearn_  = false;
    inFlightSerial_ = 0;
    // A hover learned before the click must not flash back over the pending proposal.
    const bool suppressed = !outcome.learn && pending_ && pending_->learn;
    if (!shuttingDown_ && epoch == epoch_ && !suppressed)
        emit outcomeReady(outcome);
    startNext();
    notifyBusy();
}

void AssistPreview::releaseInstance()
{
    if (!instance_)
        return;
    try
    {
        instance_->release();
    }
    catch (...)
    {
    }
    instance_.reset();
    instanceSession_.reset();
}

AssistOutcome AssistPreview::execute(const std::shared_ptr<const Session> &sessionPtr,
                                    const AssistRequest &request)
{
    AssistOutcome outcome;
    outcome.context = request.context;
    outcome.learn   = request.learn;
    outcome.append  = request.append;
    const auto &session = *sessionPtr;
    try
    {
        // A new session (tool change or catalog refresh) owns a new registry snapshot, so the old
        // instance must be released instead of serving requests from an unloaded module.
        if (!instance_ || instanceSession_ != sessionPtr)
        {
            releaseInstance();
            PlatformToolServices services{PathResolver(session.directory)};
            instance_ = session.registry->create(session.helper, services);
            if (!instance_)
                throw ContractError("辅助工具实例不可用：" + session.helper.id);
            const auto status = instance_->initialize({});
            if (!status)
            {
                outcome.status  = status;
                outcome.quality = helperMessage(status.message);
                releaseInstance();
                return outcome;
            }
            instanceSession_ = sessionPtr;
        }
        const auto inputs = normalizeInputs(session.helper.inputs, request.inputs);
        validateToolValues(session.helper.inputs, inputs);
        auto result = instance_->execute(inputs);
        // The helper is a tool like any other: its status is published into the same two ports the
        // node runtime uses, so the preview shows what a real run would.
        outcome.status = validateToolExecution(session.helper.outputs, result.outputs, result.detail);


        if (!outcome.status)
        {
            outcome.quality = helperMessage(outcome.status.message);
            return outcome;
        }
        outcome.outputs = result.outputs;
        const auto valid = result.outputs.find(kValid);
        const auto *flag = valid == result.outputs.end() ? nullptr : std::get_if<bool>(&valid->second);
        outcome.valid    = flag && *flag;
        if (const auto quality = result.outputs.find(kQuality); quality != result.outputs.end())
            if (const auto *text = std::get_if<std::string>(&quality->second))
                outcome.quality = helperMessage(*text);
        if (outcome.valid)
        {
            const auto proposal = result.outputs.find(kProposal);
            const auto preview  = result.outputs.find(kPreview);
            const auto *regions = proposal == result.outputs.end()
                                    ? nullptr
                                    : std::get_if<DataRef<ROI>>(&proposal->second);
            const auto *shown = preview == result.outputs.end() ? nullptr : std::get_if<DataRef<ROI>>(&preview->second);
            if (!regions || !shown || regions->get().empty())
                outcome.valid = false;
            else
            {
                outcome.proposal = regions->get();
                outcome.preview  = shown->get();
            }
        }
    }
    catch (const RegistryError &error)
    {
        outcome.status  = error.status;
        outcome.quality = helperMessage(error.status.message);
        releaseInstance();
    }
    catch (const std::exception &error)
    {
        outcome.status  = makeStatus(ErrorCode::InvalidArgument, error.what());
        outcome.quality = helperMessage(error.what());
        releaseInstance();
    }
    catch (...)
    {
        outcome.status  = makeStatus(ErrorCode::Internal, "辅助工具执行发生未知异常");
        outcome.quality = QStringLiteral("辅助工具执行发生未知异常");
        releaseInstance();
    }
    if (!outcome.valid && outcome.quality.isEmpty())
        outcome.quality = QStringLiteral("未找到有效目标");
    return outcome;
}

void AssistPreview::workerLoop(std::stop_token stop)
{
    while (!stop.stop_requested())
    {
        std::optional<Job> job;
        std::uint64_t      releaseId = 0;
        {
            std::unique_lock lock(mutex_);
            if (!changed_.wait(lock, stop,
                               [this]
                               {
                                   return work_.has_value()
                                       || releaseRequest_.load(std::memory_order_acquire) != 0;
                               }))
                break;
            releaseId = releaseRequest_.exchange(0, std::memory_order_acq_rel);
            if (releaseId == 0)
            {
                job = std::move(work_);
                work_.reset();
            }
        }
        if (releaseId != 0)
        {
            releaseInstance();
            // The release is only complete once the GUI consumes this exact id; until then busy()
            // keeps any drain or close waiting instead of racing the worker.
            QMetaObject::invokeMethod(
                this, [this, id = releaseId] { acknowledgeRelease(id); }, Qt::QueuedConnection);
            QMetaObject::invokeMethod(this, [this] { Q_EMIT sessionReleased(); }, Qt::QueuedConnection);
            QMetaObject::invokeMethod(this, [this] { notifyBusy(); }, Qt::QueuedConnection);
            if (stop.stop_requested())
                break;
            continue;
        }
        if (!job)
            continue;
        auto outcome = execute(job->session, job->request);
        QMetaObject::invokeMethod(
            this,
            [this, serial = job->serial, epoch = job->epoch, outcome = std::move(outcome)]() mutable
            { deliver(serial, epoch, std::move(outcome)); },
            Qt::QueuedConnection);
    }
    releaseInstance();
    const auto outstanding = releaseRequest_.exchange(0, std::memory_order_acq_rel);
    workerFinished_.store(true, std::memory_order_release);
    if (outstanding != 0)
        QMetaObject::invokeMethod(
            this, [this, id = outstanding] { acknowledgeRelease(id); }, Qt::QueuedConnection);
    QMetaObject::invokeMethod(this, [this] { notifyBusy(); }, Qt::QueuedConnection);
}
} // namespace visionrt
