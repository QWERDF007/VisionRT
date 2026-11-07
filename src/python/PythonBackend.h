#pragma once

#include "python/PythonEnvironment.h"

#include <QString>

#include <span>

namespace visionrt
{

// Host-side owner of the Python bridge module. The application links none of the bridge's
// dependencies: the module is resolved next to the executable and loaded only when an interpreter
// has been selected and probed successfully.
//
// An unconfigured, missing or incompatible environment leaves this object unavailable with a reason.
// It never falls back to another interpreter, a system Python or a compile-time default.
class PythonBackend
{
public:
    PythonBackend();
    ~PythonBackend();
    PythonBackend(const PythonBackend &)            = delete;
    PythonBackend &operator=(const PythonBackend &) = delete;

    // Preloads the exact CPython DLL the probe reported, then loads the bridge from the application
    // directory and starts the interpreter on the selected environment.
    bool start(const PythonEnvironment &environment, QString *error = nullptr);
    void stop();
    bool running() const noexcept;

    // Why the backend cannot serve Python tools, in the user's language. Empty while running.
    QString unavailableReason() const;

    std::vector<PythonImplementation> refresh(std::span<const ToolDefinition> definitions);
    ToolFactory                       factory(const ToolDefinition &definition) const;

    bool        isCloseReady() const noexcept;
    std::size_t activeReferences() const noexcept;
    // Releases tool objects and removes the package finder while the interpreter is still alive.
    // The interpreter itself is destroyed with the backend, at process exit.
    void        prepareShutdown();

private:
    struct Api;

    bool loadModule(QString *error);
    void releaseModule();

    struct Impl;
    std::unique_ptr<Impl> impl_;
};

} // namespace visionrt
