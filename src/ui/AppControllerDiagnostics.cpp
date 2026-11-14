#include "ui/AppController_p.h"

#include "log/LogService.h"

#include <QClipboard>
#include <QGuiApplication>
#include <QJsonArray>
#include <QJsonDocument>
#include <QQuickTextDocument>

namespace visionrt {
void AppController::Impl::report(const QString &text, LogSource source)
{
    report(ApplicationDiagnostic{text}, source);
}

void AppController::Impl::report(ApplicationDiagnostic entry, LogSource source)
{
    const auto message = diagnosticMessage(entry);
    if (message.isEmpty())
        return;
    // Every application event is one log line as well. The diagnostic buffer stays as the compact
    // in-memory history the rest of the UI and the tests already consume.
    LogService::write(LogLevel::Error, source, message);
    diagnostics.push_back(std::move(entry));
    while (diagnostics.size() > 100) diagnostics.pop_front();
    emit q->diagnosticChanged();
}

void AppController::Impl::report(const ErrorInfo &error)
{
    // A host failure goes through the same diagnostic entry a tool status uses: one code, one
    // readable sentence, the technical cause kept apart.
    report(error.toStatus());
}

void AppController::Impl::report(const std::exception &error, const QString &summary)
{
    if (const auto *registryError = dynamic_cast<const RegistryError *>(&error))
        report(registryError->status);
    else if (dynamic_cast<const ContractError *>(&error))
        report(makeStatus(ErrorCode::InvalidArgument, error.what()));
    else
        report(makeStatus(ErrorCode::Internal, summary.toStdString(), error.what()));
}

void AppController::setLogViewActive(bool active)
{
    LogService::instance().setViewActive(active);
}

void AppController::logParameterDraftRejected(const QString &detail)
{
    LogService::write(LogLevel::Warn, LogSource::Ui, QStringLiteral("参数草稿被拒绝：") + detail);
}

QAbstractItemModel *AppController::logModel()
{
    return &impl_->logModel;
}

void AppController::copyAllLogs()
{
    QGuiApplication::clipboard()->setText(impl_->logModel.allText());
}

void AppController::attachLogHighlighter(QQuickTextDocument *document, const QColor &warning, const QColor &error)
{
    // A reopened window brings a new document; the previous highlighter is still alive with it.
    delete impl_->logHighlighter.data();
    impl_->logHighlighter = new LogHighlighter(document->textDocument(), warning, error);
}

QString AppController::toolUpdateSummary() const
{
    return impl_->toolUpdateSummary;
}

void AppController::dismissToolUpdate()
{
    if (impl_->toolUpdateSummary.isEmpty())
        return;
    impl_->toolUpdateSummary.clear();
    emit toolUpdateSummaryChanged();
}

void AppController::Impl::publishToolUpdates(const std::vector<ToolUpdate> &updates)
{
    // A refresh that finds nothing is not an event: the window keeps whatever it is showing until
    // the user dismisses it.
    if (updates.empty())
        return;
    QStringList lines;
    for (const auto &update : updates)
    {
        QString header = QString::fromStdString(update.nameZh);
        if (!update.savedVersion.empty() && update.savedVersion != update.currentVersion)
            header += QStringLiteral("（%1 → %2）")
                          .arg(QString::fromStdString(update.savedVersion),
                               QString::fromStdString(update.currentVersion));
        lines.append(header);
        QStringList nodes;
        for (const auto &node : update.nodes) nodes.append(QString::fromStdString(node));
        lines.append(QStringLiteral("  节点：") + nodes.join(QStringLiteral("、")));
        lines.append(QStringLiteral("  连线：保留 %1 条，断开 %2 条")
                         .arg(update.kept)
                         .arg(update.removed.size()));
        for (const auto &wire : update.removed)
            lines.append(QStringLiteral("  - %1 → %2：%3")
                             .arg(QString::fromStdString(wire.source),
                                  QString::fromStdString(wire.target),
                                  QString::fromStdString(wire.reason)));
    }
    // One notice per open or refresh: the newest finding replaces the previous sentence.
    const auto summary = lines.join(QLatin1Char('\n'));
    if (summary == toolUpdateSummary)
        return;
    toolUpdateSummary = summary;
    emit q->toolUpdateSummaryChanged();
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

}
