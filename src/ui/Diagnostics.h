#pragma once

#include "runtime/RuntimeEngine.h"
#include "core/ProjectStorage.h"

#include <QJsonObject>
#include <QString>

namespace visionrt {
using ApplicationDiagnostic
    = std::variant<QString, ToolStatus, NodeDiagnostic, RuntimeDiagnostic, ProjectStorageDiagnostic>;

// How a status message is presented: the curated first line as the summary, the rest as detail.
struct StatusDisplay
{
    QString summary;
    QString detail;
};

StatusDisplay statusDisplay(const ToolStatus &status);
QString       statusSummary(const ToolStatus &status);
QString     dispositionName(NodeDisposition disposition);
QString     diagnosticMessage(const NodeDiagnostic &diagnostic);
QString     diagnosticMessage(const ApplicationDiagnostic &diagnostic);
QJsonObject diagnosticObject(const ApplicationDiagnostic &diagnostic);
QString     diagnosticJson(const NodeDiagnostic &diagnostic, const NodeDiagnostic *lifecycle = nullptr);
}
