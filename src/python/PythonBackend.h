#pragma once

#include "python/PythonEnvironment.h"

#include <QString>

#include <span>
#include <string>
#include <utility>
#include <vector>

namespace visionrt
{

// What this process actually has loaded, as opposed to what the user selected. A package can look
// correct and still run a second interpreter, so release evidence records the observed modules
// instead of trusting the configuration that was meant to be used.
struct PythonRuntimeFacts
{
    // Absolute path of the CPython DLL currently loaded into this process; empty when none is.
    std::string runtime;
    bool        bridge = false;
    // Every loaded module whose name could carry an interpreter or its NumPy, by name and path.
    std::vector<std::pair<std::string, std::string>> modules;
};

// Host-side owner of the Python bridge module. The application links none of the bridge's
// dependencies: the module is resolved next to the executable and loaded only when an interpreter
// has been selected and probed successfully.
//
// The backend also owns the single environment status the whole host reads: the probe records the
// verdict of the environment, this object records the verdict of the bridge, and the tool registry,
// the settings window and the diagnostics all read the one value here. It never falls back to
// another interpreter, a system Python or a compile-time default.
class PythonBackend
{
public:
    PythonBackend();
    ~PythonBackend();
    PythonBackend(const PythonBackend &)            = delete;
    PythonBackend &operator=(const PythonBackend &) = delete;

    // Records a verdict produced before the bridge exists: not configured, still checking, or
    // rejected with the reason the probe reported.
    void                           setEnvironmentStatus(PythonEnvironmentStatus status);
    const PythonEnvironmentStatus &environmentStatus() const noexcept;

    // Preloads the exact CPython DLL the probe reported, then loads the bridge from the application
    // directory and starts the interpreter on the selected environment. The status becomes Ready or
    // BackendFailed; every failure path releases what it loaded.
    bool start(const PythonEnvironment &environment, QString *error = nullptr);
    void stop();
    bool running() const noexcept;

    // Why the backend cannot serve Python tools, in the user's language. Empty while running.
    QString unavailableReason() const;

    // `status` is the caller's snapshot of environmentStatus(), taken on the thread that owns this
    // object: the refresh runs on a runtime worker and must not read state another thread writes.
    std::vector<PythonImplementation> refresh(std::span<const ToolDefinition> definitions,
                                              const PythonEnvironmentStatus &status);
    ToolFactory                       factory(const ToolDefinition &definition) const;

    bool        isCloseReady() const noexcept;
    std::size_t activeReferences() const noexcept;
    // Releases tool objects and removes the package finder while the interpreter is still alive.
    // The interpreter itself is destroyed with the backend, at process exit.
    void        prepareShutdown();

    // Reads the process module list. It never loads anything: an environment that failed to start
    // must report exactly what is resident, which is how "no interpreter was loaded" is proven.
    PythonRuntimeFacts runtimeFacts() const;

private:
    struct Api;

    bool loadModule(QString *error);
    void releaseModule();

    struct Impl;
    std::unique_ptr<Impl> impl_;
};

} // namespace visionrt
