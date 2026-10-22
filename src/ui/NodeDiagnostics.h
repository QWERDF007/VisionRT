#pragma once

#include "runtime/Snapshot.h"

#include <QString>

namespace visionrt {
QString diagnosticMessage(const NodeDiagnostic &diagnostic);
QString diagnosticJson(const NodeDiagnostic &diagnostic, const NodeDiagnostic *lifecycle = nullptr);
}
