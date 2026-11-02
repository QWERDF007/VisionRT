#pragma once

// Waits shared by tests that drive a real RuntimeEngine. The deadline is a failure bound, not a
// performance assertion: callers want "this must finish", not a fixed sleep.

#include "core/ToolRegistry.h"
#include "python/PythonRuntime.h"
#include "runtime/RuntimeEngine.h"

#include <cassert>
#include <chrono>
#include <stdexcept>
#include <string>
#include <thread>
#include <vector>

namespace testsupport {

using namespace visionrt;
using namespace std::chrono_literals;

inline RuntimeUpdate finish(RuntimeEngine &engine)
{
    const auto deadline = std::chrono::steady_clock::now() + 15s;
    while (std::chrono::steady_clock::now() < deadline)
    {
        auto update = engine.pollLatest();
        if (update.completion)
        {
            assert((engine.acknowledgeCompletion(*update.completion)));
            return update;
        }
        std::this_thread::sleep_for(2ms);
    }
    throw std::runtime_error("runtime completion deadline exceeded");
}

inline ResultSlot::Snapshot snapshot(const RuntimeUpdate &update, NodeId id)
{
    for (const auto &item : update.nodes)
    {
        if (item.node == id)
            return item.snapshot;
    }
    throw std::runtime_error("node update absent: " + std::to_string(id));
}

inline void setupPython(ToolRegistry &registry, PythonRuntime &runtime)
{
    std::vector<ToolDefinition> pyDefs;
    for (const auto &definition : registry.definitions())
    {
        if (definition.kind == ToolKind::PythonClass || definition.kind == ToolKind::PythonFunction)
            pyDefs.push_back(definition);
    }
    const auto loadedList = runtime.refresh(pyDefs);
    for (const auto &loaded : loadedList)
        registry.setPythonImplementation(loaded.toolId, runtime.factory(*registry.find(loaded.toolId)),
                                         loaded.status);
}

} // namespace testsupport
