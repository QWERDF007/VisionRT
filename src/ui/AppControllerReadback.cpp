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
    return state == RuntimeState::Idle || readback.has_value();
}

void AppController::Impl::drainReaders()
{
    converter.waitForReads();
    if (textTask.valid())
        textTask.wait();
}

void AppController::Impl::updateReadback()
{
    using namespace std::chrono_literals;
    if (textTask.valid() && textTask.wait_for(0ms) == std::future_status::ready)
    {
        try
        {
            auto result = textTask.get();
            if (result.generation == generation && q->portContextCurrent(result.context))
            {
                if (result.copy)
                    QGuiApplication::clipboard()->setText(result.text);
                emit q->outputTextReady(result.text);
            }
        }
        catch (const std::exception &error)
        {
            report(error, QStringLiteral("完整输出生成失败"));
        }
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
    if (!context.value("output").toBool() || !portContextCurrent(context))
        return;
    if (!impl_->readsAllowed())
    {
        impl_->readRequests.push_back(Impl::TextRequest{textTarget(context), copy, diagnostic});
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
    impl_->textTask       = std::async(std::launch::async,
                                       [captured = std::move(*captured), copy, generation, context, diagnostic]
                                       {
                                     auto text = diagnostic ? OutputModel::diagnosticText(captured)
                                                                  : OutputModel::fullText(captured);
                                     return Impl::TextResult{std::move(text), copy, generation, context};
                                 });
}
} // namespace visionrt
