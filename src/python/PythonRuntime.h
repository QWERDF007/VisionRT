#pragma once

#include "core/Tool.h"

#include <span>

namespace visionrt {

struct PythonImplementation
{
    std::string toolId;
    ToolStatus  status;
};

// Construct and destroy on the application main thread. The interpreter remains
// alive across reset; its main-thread GIL is released until destruction.
class PythonRuntime
{
public:
    explicit PythonRuntime(const std::filesystem::path &pythonHome = {});
    ~PythonRuntime();
    PythonRuntime(const PythonRuntime &)            = delete;
    PythonRuntime &operator=(const PythonRuntime &) = delete;

    std::vector<PythonImplementation> refresh(std::span<const ToolDefinition> definitions);
    ToolFactory                       factory(const ToolDefinition &definition) const;

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

} // namespace visionrt
