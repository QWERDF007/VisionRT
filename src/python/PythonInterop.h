#pragma once

#include "visionrt/contracts/Value.h"

#include <pybind11/pybind11.h>

namespace visionrt {

// Converts a PortValue to a read-only Python object (NumPy array, list of arrays, or scalar).
// The caller holds the GIL for the call and for every returned Python reference.
// Array inputs retain only C++ owners; outputs never retain Python references.
pybind11::object toPythonReadonly(const PortValue& value);

// Converts a Python object back to PortValue matching the expected TypeSpec.
// C++ deep-copies or takes ownership into pure C++ storage; no Python reference is retained.
PortValue fromPython(const TypeSpec& expected, pybind11::handle value);

#if defined(VISIONRT_TEST_PROBES)
std::size_t pythonLeafImageCopyCount() noexcept;
void resetPythonLeafImageCopyCount() noexcept;
std::size_t pythonLeafNumericCopyCount() noexcept;
void resetPythonLeafNumericCopyCount() noexcept;
#endif

} // namespace visionrt
