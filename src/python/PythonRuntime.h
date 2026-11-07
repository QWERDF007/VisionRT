#pragma once

#include "python/PythonEnvironment.h"

#include <span>

namespace visionrt
{

// Construct and destroy on the application main thread. The interpreter remains alive across
// reset; its main-thread GIL is released until destruction.
//
// Every fact about the environment comes from the probe, never from this process's environment:
// the interpreter is started with the exact executable, prefixes and search paths the selected
// interpreter reported about itself.
class PythonRuntime
{
public:
    explicit PythonRuntime(const PythonEnvironment &environment);
    ~PythonRuntime();
    PythonRuntime(const PythonRuntime &)            = delete;
    PythonRuntime &operator=(const PythonRuntime &) = delete;

    std::vector<PythonImplementation> refresh(std::span<const ToolDefinition> definitions);
    ToolFactory                       factory(const ToolDefinition &definition) const;

    bool        isCloseReady() const noexcept;
    void        prepareShutdown();
    std::size_t activeReferences() const noexcept;

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

} // namespace visionrt
