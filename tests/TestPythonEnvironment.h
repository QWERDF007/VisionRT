#pragma once

#include "python/PythonEnvironment.h"

namespace testsupport
{

// The interpreter this test build was configured with. A test never inherits PYTHONHOME, a
// compile-time home or any other candidate: it probes the build interpreter once and starts from the
// facts that interpreter reports about itself, exactly like the application does with the user's
// choice. A broken build environment therefore fails loudly instead of silently running another
// Python.
const visionrt::PythonEnvironment &pythonEnvironment();

} // namespace testsupport
