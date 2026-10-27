#pragma once

#include "python/PythonStorage.h"

namespace visionrt {

pybind11::object toPython(const PortValue &value);
PortValue        fromPython(const TypeSpec &expected, pybind11::handle value, const PythonStorage &storage,
                            std::string path = "value");

}
