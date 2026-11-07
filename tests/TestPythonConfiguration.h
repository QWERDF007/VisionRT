#pragma once

#include "ui/AppController.h"

#include <QString>

namespace testsupport
{

// R50: the host executes Python only on an interpreter someone selected. A test binary calls this
// once, before the first controller exists, and states the interpreter its build was configured
// with. Nothing is inherited from PYTHONHOME or a compile-time home, and the value never reaches the
// preferences file: the controller probes it exactly like a user's saved choice.
inline void useBuildInterpreter()
{
    visionrt::AppController::usePythonForTesting(QString::fromUtf8(VISIONRT_TEST_PYTHON_EXECUTABLE));
}

} // namespace testsupport
