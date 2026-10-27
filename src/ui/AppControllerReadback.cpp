#include "ui/AppController_p.h"

#include <QClipboard>
#include <QGuiApplication>
#include <type_traits>

namespace visionrt {
namespace {
QVariantMap textTarget(QVariantMap context)
{
    context.remove("publication");
    return context;
}
}

bool AppController::Impl::readsAllowed() const noexcept
{
    if (isClosing())
        return false;
    return state == RuntimeState::Idle || readback.has_value();
}

void AppController::Impl::drainReaders()
{
    struct DrainGuard
    {
        bool &flag;
        bool  previous;
        DrainGuard(bool &f) : flag(f), previous(f) { flag = true; }
        ~DrainGuard() { flag = previous; }
    } guard(draining);

    converter.waitForReads();
    if (textTask.valid())
    {
        textTask.wait();
        textTask = {};
    }
    if (textMailbox)
    {
        bool done = false;
        {
            std::lock_guard lock(textMailbox->mutex);
            done = textMailbox->done;
        }
        if (done)
            consumeTextMailbox(textMailbox);
        else
            textMailbox.reset();
    }
    for (auto it = readRequests.begin(); it != readRequests.end();)
    {
        if (std::holds_alternative<TextRequest>(*it) || closing || closePhase != ClosePhase::None)
            it = readRequests.erase(it);
        else
            ++it;
    }
}

bool AppController::Impl::drainReady() const
{
    return !converter.isBusy() && textPending.load() == 0 && (!assist || !assist->busy());
}

void AppController::Impl::beginDrain(std::function<void()> continuation)
{
    if (draining)
    {
        // Coalesce: the newest continuation wins, exactly one body runs after the drain.
        drainContinuation = std::move(continuation);
        return;
    }
    // A preview must stop touching the data the caller is about to mutate. Only an invalidation
    // belongs here: the persistent helper is released by the management operations that really end
    // its session (reset, catalog reload, close), otherwise every drain would tear it down again.
    if (assist)
        assist->invalidate();
    // Invalidate before waiting so a completed-but-not-yet-delivered conversion can not touch the
    // data the caller is about to mutate.
    converter.invalidate(generation);
    for (auto it = readRequests.begin(); it != readRequests.end();)
    {
        if (std::holds_alternative<TextRequest>(*it) || closing || closePhase != ClosePhase::None)
            it = readRequests.erase(it);
        else
            ++it;
    }
    if (drainReady())
    {
        if (continuation)
            continuation();
        return;
    }
    draining          = true;
    drainContinuation = std::move(continuation);
}

void AppController::Impl::finishDrain()
{
    if (!draining || !drainReady())
        return;
    draining = false;
    auto continuation = std::move(drainContinuation);
    drainContinuation = {};
    if (continuation)
        continuation();
}

void AppController::Impl::consumeTextMailbox(std::shared_ptr<TextMailbox> mailbox)
{
    if (!mailbox)
        return;
    const bool current = mailbox == textMailbox;
    if (current)
        textMailbox.reset();
    // Never block the GUI thread. The async body has already decremented the pending counter before
    // it queues this call, so releasing the future here cannot wait on formatting work. Releasing it
    // for a discarded mailbox is what keeps a later request from being rejected as still running.
    if (textTask.valid() && textPending.load() == 0)
        textTask = {};
    if (!current)
    {
        if (draining)
            finishDrain();
        return;
    }
    if (mailbox->generation != generation)
    {
        if (draining)
            finishDrain();
        return;
    }

    std::exception_ptr        error;
    std::optional<TextResult> result;
    {
        std::lock_guard lock(mailbox->mutex);
        if (!mailbox->done)
            return;
        error  = mailbox->error;
        result = std::move(mailbox->value);
    }

    if (error)
    {
        try
        {
            std::rethrow_exception(error);
        }
        catch (const std::exception &e)
        {
            report(e, QStringLiteral("输出文本格式化失败"));
        }
        if (!draining)
            updateReadback();
        else
            finishDrain();
        return;
    }

    if (result && q->portContextCurrent(result->context))
    {
        if (result->copy)
            QGuiApplication::clipboard()->setText(result->text);
        emit q->outputTextReady(result->text);
    }
    if (!draining)
        updateReadback();
    else
        finishDrain();
}

void AppController::Impl::updateReadback()
{
    if (draining || closing || closePhase != ClosePhase::None)
        return;
    if (textMailbox)
    {
        bool done = false;
        {
            std::lock_guard lock(textMailbox->mutex);
            done = textMailbox->done;
        }
        if (done)
            consumeTextMailbox(textMailbox);
    }
    if (!readsAllowed())
        return;
    // Text reads need the publication to stay valid, so they are served first; deferred project
    // saves belong to the end of the run and are flushed there.
    while (!textTask.valid())
    {
        const auto textRequest
            = std::find_if(readRequests.begin(), readRequests.end(),
                           [](const auto &request) { return std::holds_alternative<TextRequest>(request); });
        if (textRequest == readRequests.end())
            break;
        auto request = std::move(*textRequest);
        readRequests.erase(textRequest);
        const auto &intent  = std::get<TextRequest>(request);
        const auto  current = q->portContext(intent.context.value("port").toString(), true);
        if (intent.context == textTarget(current))
            q->requestOutputText(current, intent.copy, intent.diagnostic);
    }
    const bool textQueued
        = std::any_of(readRequests.begin(), readRequests.end(),
                      [](const auto &request) { return std::holds_alternative<TextRequest>(request); });
    if (readback && !textTask.valid() && !textQueued && !converter.isBusy() && !converter.hasPending())
    {
        if (runtime->acknowledgeReadback(*readback))
            readback.reset();
    }
}

void AppController::Impl::flushDeferredSaves()
{
    for (auto it = readRequests.begin(); it != readRequests.end();)
    {
        const auto *save = std::get_if<SaveRequest>(&*it);
        if (!save)
        {
            ++it;
            continue;
        }
        const auto path = save->path;
        const bool current = save->project == projectEpoch;
        it = readRequests.erase(it);
        if (current)
            q->save(path);
    }
}

void AppController::requestOutputText(const QVariantMap &context, bool copy, bool diagnostic)
{
    if (impl_->isClosing() || impl_->draining)
        return;
    if (!context.value("output").toBool() || !portContextCurrent(context))
        return;
    if (!impl_->readsAllowed())
    {
        const auto target = textTarget(context);
        const bool alreadyQueued = std::any_of(impl_->readRequests.begin(), impl_->readRequests.end(),
            [&](const auto &item) {
                if (const auto *textReq = std::get_if<Impl::TextRequest>(&item)) {
                    return textReq->context == target && textReq->copy == copy && textReq->diagnostic == diagnostic;
                }
                return false;
            });
        if (!alreadyQueued)
            impl_->readRequests.push_back(Impl::TextRequest{target, copy, diagnostic});
        return;
    }
    if (impl_->textTask.valid())
    {
        impl_->report(QStringLiteral("正在生成完整输出，请稍候"));
        return;
    }
    auto captured = impl_->outputs.capture(context.value("port").toString());
    if (!captured)
        return;
    if (diagnostic && !captured->diagnostic && !captured->lifecycle)
        return;
    // Copy always runs; a large value is formatted on the bounded worker rather than gated behind a
    // second confirmation dialog, and it is never truncated into something that looks complete.
    const auto generation = impl_->generation;
    auto       mailbox    = std::make_shared<Impl::TextMailbox>();
    mailbox->generation   = generation;
    impl_->textMailbox    = mailbox;
    ++impl_->textPending;
    impl_->textTask       = std::async(
        std::launch::async,
        [captured = std::move(*captured), copy, generation, context, diagnostic, mailbox, this]
        {
            try
            {
                auto text = diagnostic ? OutputModel::diagnosticText(captured) : OutputModel::fullText(captured);
                {
                    std::lock_guard lock(mailbox->mutex);
                    mailbox->value = Impl::TextResult{std::move(text), copy, generation, context};
                    mailbox->done  = true;
                }
            }
            catch (...)
            {
                std::lock_guard lock(mailbox->mutex);
                mailbox->error = std::current_exception();
                mailbox->done  = true;
            }
            --impl_->textPending;
            QMetaObject::invokeMethod(this, [this, mailbox] { impl_->consumeTextMailbox(mailbox); },
                                      Qt::QueuedConnection);
        });
}
} // namespace visionrt
