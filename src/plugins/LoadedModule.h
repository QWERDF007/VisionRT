#pragma once

#include "core/ToolRegistry.h"
#ifdef _WIN32
#    ifndef WIN32_LEAN_AND_MEAN
#        define WIN32_LEAN_AND_MEAN
#    endif
#    ifndef NOMINMAX
#        define NOMINMAX
#    endif
#    include <windows.h>
#else
#    include <QLibrary>
#endif

namespace visionrt {
struct LoadedModule
{
    std::filesystem::path path;
#ifdef _WIN32
    HMODULE                                              handle = nullptr;
    std::unique_ptr<void, decltype(&RemoveDllDirectory)> directory{nullptr, &RemoveDllDirectory};
    LoadedModule() = default;

    ~LoadedModule()
    {
        if (handle)
        {
            FreeLibrary(handle);
            handle = nullptr;
        }
    }

    LoadedModule(const LoadedModule &)            = delete;
    LoadedModule &operator=(const LoadedModule &) = delete;
#else
    std::shared_ptr<QLibrary> library;
#endif
    ToolRegistry registry;
};

}
