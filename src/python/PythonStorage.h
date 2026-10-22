#pragma once

#include "visionrt/contracts/Value.h"

#include <pybind11/pybind11.h>

namespace visionrt {

class PythonStorage
{
public:
    PythonStorage();
    Owner retain(pybind11::handle object) const;
    void  waitForRelease() const;

private:
    struct State;
    std::shared_ptr<State> state_;
};

}
