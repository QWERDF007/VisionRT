#include "TestPythonEnvironment.h"

#include "python/PythonProbe.h"

#include <QString>

#include <stdexcept>

namespace testsupport
{

const visionrt::PythonEnvironment &pythonEnvironment()
{
    static const auto environment = []
    {
        QString    error;
        const auto probed = visionrt::probePythonEnvironment(QString::fromUtf8(VISIONRT_TEST_PYTHON_EXECUTABLE), &error);
        if (!probed)
            throw std::runtime_error(error.toStdString());
        return *probed;
    }();
    return environment;
}

} // namespace testsupport
