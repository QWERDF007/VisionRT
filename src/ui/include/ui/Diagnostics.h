#pragma once

#include "ui/Export.h"

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

VISIONRT_UI_API StatusDisplay statusDisplay(const ToolStatus &status);
VISIONRT_UI_API QString       statusSummary(const ToolStatus &status);
VISIONRT_UI_API QString     dispositionName(NodeDisposition disposition);
VISIONRT_UI_API QString     diagnosticMessage(const NodeDiagnostic &diagnostic);
VISIONRT_UI_API QString     diagnosticMessage(const ApplicationDiagnostic &diagnostic);
VISIONRT_UI_API QJsonObject diagnosticObject(const ApplicationDiagnostic &diagnostic);
VISIONRT_UI_API QString     diagnosticJson(const NodeDiagnostic &diagnostic, const NodeDiagnostic *lifecycle = nullptr);
}
