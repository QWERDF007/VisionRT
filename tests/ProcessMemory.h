#pragma once

#include <cstddef>
#include <string>

#ifdef _WIN32
#    ifndef NOMINMAX
#        define NOMINMAX
#    endif
#    include <windows.h>
#    include <psapi.h>
#endif

namespace visionrt::testing {
struct MemoryProbe
{
    bool        supported    = false;
    std::size_t workingSetKB = 0;
    std::string report;
};

inline MemoryProbe getWorkingSetMemory()
{
    MemoryProbe probe;
#ifdef _WIN32
    PROCESS_MEMORY_COUNTERS pmc{};
    if (GetProcessMemoryInfo(GetCurrentProcess(), &pmc, sizeof(pmc)))
    {
        probe.supported    = true;
        probe.workingSetKB = pmc.WorkingSetSize / 1024;
        probe.report       = std::to_string(probe.workingSetKB) + " KB";
        return probe;
    }
    probe.report = "unsupported (GetProcessMemoryInfo failed)";
#else
    probe.report = "unsupported (non-Windows platform)";
#endif
    return probe;
}
}
