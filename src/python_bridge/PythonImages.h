#pragma once

#include "python_bridge/PythonStorage.h"

namespace visionrt::python {

pybind11::list imageViews(const DataRef<Image> &images);
DataRef<Image> imageHeaders(pybind11::handle value, const PythonStorage &storage, const std::string &path);

}
