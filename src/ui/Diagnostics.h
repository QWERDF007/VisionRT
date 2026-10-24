#pragma once

#include "runtime/RuntimeEngine.h"
#include "core/ProjectStorage.h"

#include <QJsonObject>
#include <QString>

namespace visionrt {
using ApplicationDiagnostic
    = std::variant<QString, ToolStatus, NodeDiagnostic, RuntimeDiagnostic, ProjectStorageDiagnostic>;

QString     statusSummary(const ToolStatus &status);
QString     diagnosticMessage(const NodeDiagnostic &diagnostic);
QString     diagnosticMessage(const ApplicationDiagnostic &diagnostic);
QJsonObject diagnosticObject(const ApplicationDiagnostic &diagnostic);
QString     diagnosticJson(const NodeDiagnostic &diagnostic, const NodeDiagnostic *lifecycle = nullptr);
}
