#include "ui/AppController_p.h"

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
    if (diagnosticMessage(entry).isEmpty())
        return;
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
