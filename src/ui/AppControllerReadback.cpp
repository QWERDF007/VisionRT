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

void AppController::Impl::consumeTextMailbox(std::shared_ptr<TextMailbox> mailbox)
{
    if (!mailbox || mailbox != textMailbox)
        return;
    textMailbox.reset();
    if (textTask.valid())
    {
        textTask.wait();
        textTask = {};
    }
    if (mailbox->generation != generation)
        return;

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
    while (!textTask.valid() && !readRequests.empty())
    {
        auto request = std::move(readRequests.front());
        readRequests.pop_front();
        std::visit(
            [&](const auto &intent)
            {
                using Request = std::decay_t<decltype(intent)>;
                if constexpr (std::is_same_v<Request, TextRequest>)
                {
                    const auto current = q->portContext(intent.context.value("port").toString(), true);
                    if (intent.context == textTarget(current))
                        q->requestOutputText(current, intent.copy, intent.diagnostic);
                }
                else if (intent.project == projectEpoch)
                    q->save(intent.path);
            },
            request);
    }
    if (readback && !textTask.valid() && readRequests.empty() && !converter.isBusy() && !converter.hasPending())
    {
        if (runtime->acknowledgeReadback(*readback))
            readback.reset();
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
    auto estimated
        = diagnostic && captured->diagnostic
            ? (captured->diagnostic->status.message.size() + captured->diagnostic->status.detail.size()) * 6 + 1024
            : OutputModel::estimatedTextBytes(*captured);
    if (diagnostic && captured->lifecycle)
        estimated
            += (captured->lifecycle->status.message.size() + captured->lifecycle->status.detail.size()) * 6 + 1024;
    if (estimated > 16 * 1024 * 1024
        && (impl_->approvedTextContext != textTarget(context) || impl_->approvedCopy != copy
            || impl_->approvedDiagnostic != diagnostic))
    {
        impl_->approvedTextContext = textTarget(context);
        impl_->approvedCopy        = copy;
        impl_->approvedDiagnostic  = diagnostic;
        emit outputTextReady(QStringLiteral("完整数据预计约 %1 MiB。再次点击同一查看/复制操作以确认；不会截断全文。")
                                 .arg(static_cast<double>(estimated) / (1024 * 1024), 0, 'f', 1));
        return;
    }
    impl_->approvedTextContext.clear();
    const auto generation = impl_->generation;
    auto       mailbox    = std::make_shared<Impl::TextMailbox>();
    mailbox->generation   = generation;
    impl_->textMailbox    = mailbox;
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
            QMetaObject::invokeMethod(this, [this, mailbox] { impl_->consumeTextMailbox(mailbox); },
                                      Qt::QueuedConnection);
        });
}
} // namespace visionrt
