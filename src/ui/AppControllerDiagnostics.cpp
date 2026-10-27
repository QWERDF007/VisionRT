#include "ui/AppController_p.h"

#include "log/LogService.h"

#include <QClipboard>
#include <QGuiApplication>
#include <QJsonArray>
#include <QJsonDocument>

namespace visionrt {
void AppController::Impl::report(const QString &text)
{
    report(ApplicationDiagnostic{text});
}

void AppController::Impl::report(ApplicationDiagnostic entry)
{
    const auto message = diagnosticMessage(entry);
    if (message.isEmpty())
        return;
    // Every application event is one log line as well. The diagnostic buffer stays as the compact
    // in-memory history the rest of the UI and the tests already consume.
    LogService::write(LogLevel::Error, QStringLiteral("应用"), message);
    diagnostics.push_back(std::move(entry));
    while (diagnostics.size() > 100) diagnostics.pop_front();
    emit q->diagnosticChanged();
}

void AppController::Impl::report(const std::exception &error, const QString &summary)
{
    if (const auto *registryError = dynamic_cast<const RegistryError *>(&error))
        report(registryError->status);
    else if (dynamic_cast<const ContractError *>(&error))
        report(ToolStatus{-2, error.what()});
    else
        report(ToolStatus{-1, summary.toStdString(), error.what()});
}

void AppController::setLogViewActive(bool active)
{
    LogService::instance().setViewActive(active);
}

void AppController::logParameterDraftRejected(const QString &detail)
{
    LogService::write(LogLevel::Warn, QStringLiteral("参数"), QStringLiteral("参数草稿被拒绝：") + detail);
}

QAbstractItemModel *AppController::logModel()
{
    if (!impl_->logModel)
        impl_->logModel = std::make_unique<LogModel>();
    return impl_->logModel.get();
}

QString AppController::diagnostic() const
{
    return impl_->diagnostics.empty() ? QString() : diagnosticMessage(impl_->diagnostics.back());
}

QStringList AppController::diagnostics() const
{
    QStringList result;
    for (auto item = impl_->diagnostics.rbegin(); item != impl_->diagnostics.rend(); ++item)
        result.push_back(diagnosticMessage(*item));
    return result;
}

QString AppController::diagnosticsJson() const
{
    QJsonArray result;
    for (auto item = impl_->diagnostics.rbegin(); item != impl_->diagnostics.rend(); ++item)
        result.push_back(diagnosticObject(*item));
    return QString::fromUtf8(QJsonDocument(result).toJson(QJsonDocument::Indented));
}

void AppController::copyDiagnostics() const
{
    QGuiApplication::clipboard()->setText(diagnosticsJson());
}
}
