#include "ui/AssistPreview.h"

#include "core/ToolRegistry.h"
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

bool AssistPreview::busy() const
{
    return inFlight_ || pending_.has_value() || hover_.isActive();
}

void AssistPreview::shutdown()
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
    }
    worker_.request_stop();
    changed_.notify_all();
    if (worker_.joinable())
        worker_.join();
    session_.reset();
    notifyBusy();
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
        validateToolValues(session.helper.inputs, request.inputs);
        auto result = instance_->execute(request.inputs);
        outcome.status = result.status;
        if (!result.status)
        {
            outcome.quality = helperMessage(result.status.message);
            return outcome;
        }
        for (const auto &port : session.helper.outputs)
        {
            if (port.id == "__status_code" || port.id == "__status_message")
                continue;
            const auto found = result.outputs.find(port.id);
            if (found == result.outputs.end())
                throw ContractError("辅助工具缺少输出端口：" + port.id);
            validateToolOutput(port, found->second, result.status);
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
        outcome.status  = {-2, error.what()};
        outcome.quality = helperMessage(error.what());
        releaseInstance();
    }
    catch (...)
    {
        outcome.status  = {-1, "辅助工具执行发生未知异常"};
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
        {
            std::unique_lock lock(mutex_);
            if (!changed_.wait(lock, stop, [this] { return work_.has_value(); }))
                break;
            job = std::move(work_);
            work_.reset();
        }
        auto outcome = execute(job->session, job->request);
        QMetaObject::invokeMethod(
            this,
            [this, serial = job->serial, epoch = job->epoch, outcome = std::move(outcome)]() mutable
            { deliver(serial, epoch, std::move(outcome)); },
            Qt::QueuedConnection);
    }
    releaseInstance();
}
} // namespace visionrt
