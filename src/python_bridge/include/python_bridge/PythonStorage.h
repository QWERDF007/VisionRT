#pragma once

#include "python_bridge/Export.h"

#include "visionrt/contracts/Value.h"

#include <pybind11/pybind11.h>

#include <cstddef>

namespace visionrt {

class VISIONRT_PYTHON_BRIDGE_API PythonStorage
{
public:
    PythonStorage();
    Owner       retain(pybind11::handle object) const;
    void        waitForRelease() const;
    std::size_t activeCount() const noexcept;
    bool        isReadyForClose() const noexcept;

private:
    struct State;
    std::shared_ptr<State> state_;
};

} // namespace visionrt
