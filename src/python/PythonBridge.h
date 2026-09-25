#pragma once

#include "common/Value.h"

#include <pybind11/pybind11.h>

namespace visionrt {

// The caller holds the GIL for the call and for every returned Python reference.
// Array inputs retain only C++ owners; outputs never retain Python references.
pybind11::object toPythonReadonly(const PortValue& value);
PortValue fromPython(const TypeSpec& expected, pybind11::handle value);

} // namespace visionrt
