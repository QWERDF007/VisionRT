#pragma once

#include "core/PythonLoadState.h"
#include "core/ToolDefinition.h"
#include "visionrt/contracts/Tool.h"

#include <functional>
#include <memory>

namespace visionrt {

class IResultReader;

// What a host-implemented tool may need from the running session. A C++ or Python tool never
// receives this: it gets only the values its declaration names, and interprets any path itself.
struct HostToolContext
{
    std::shared_ptr<IResultReader> results;
};

// A creation entry owns no inputs: everything a tool works with arrives through initialize/execute.
using ToolFactory = std::function<std::unique_ptr<IToolInstance>()>;

// A Python tool's ports can differ per node: an expression or condition node derives its inputs and
// outputs from the project. The registry therefore asks the runtime for a factory bound to the
// contract it is about to instantiate instead of reusing the packaged declaration.
using PythonFactoryResolver = std::function<ToolFactory(const ToolDefinition &)>;

struct PythonFactoryFunctor
{
    std::shared_ptr<PythonLoadState> loadState;
    ToolFactory                      create;
    PythonFactoryResolver            resolve;

    std::unique_ptr<IToolInstance> operator()() const
    {
        return create();
    }
};
} // namespace visionrt
