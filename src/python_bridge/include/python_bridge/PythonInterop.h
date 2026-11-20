#pragma once

#include "python_bridge/Export.h"

#include "python_bridge/PythonStorage.h"

namespace visionrt {

VISIONRT_PYTHON_BRIDGE_API pybind11::object toPython(const PortValue &value);
VISIONRT_PYTHON_BRIDGE_API PortValue        fromPython(const TypeSpec &expected, pybind11::handle value, const PythonStorage &storage,
                            std::string path = "value");

}
