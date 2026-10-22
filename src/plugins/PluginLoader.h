#pragma once

#include "core/Tool.h"
#include "core/ToolPackage.h"
#include "core/ToolRegistry.h"
#include "visionrt/contracts/Module.h"

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
#include <condition_variable>
#include <filesystem>
#include <map>
#include <memory>
#include <mutex>
#include <string>
#include <string_view>
#include <thread>
#include <vector>

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

class PluginLoader : public std::enable_shared_from_this<PluginLoader>
{
public:
    PluginLoader()  = default;
    ~PluginLoader() = default;

    // Low-level seam for registering a single known module (tests, embedded modules).
    void registerModule(std::string toolId, std::filesystem::path libraryPath);

    void registerPackage(const ToolsPackage &package);

    std::filesystem::path resolveModulePath(std::string_view toolId) const;
    Availability          availability(std::string_view toolId) const;

    std::shared_ptr<const LoadedModule> loadModule(const std::filesystem::path &libraryPath, ToolStatus &status);
    std::unique_ptr<IToolInstance>      createAdapter(std::string_view toolId, const PlatformToolServices &services);

    std::size_t loadedModuleCount() const;

private:
    std::shared_ptr<LoadedModule> loadModuleFile(const std::filesystem::path &libraryPath, ToolStatus &status);

    struct PendingLoad
    {
        std::thread::id owner;
    };

    mutable std::mutex                                             mutex_;
    std::condition_variable                                        loadedChanged_;
    std::map<std::string, std::filesystem::path, std::less<>>      index_;
    std::map<std::filesystem::path, std::shared_ptr<LoadedModule>> loadedModules_;
    std::map<std::filesystem::path, PendingLoad>                   loading_;
    std::map<std::filesystem::path, ToolStatus>                    failures_;
};

class DomainToolAdapter final : public IToolInstance
{
public:
    DomainToolAdapter(std::shared_ptr<PluginLoader> loader, std::filesystem::path libraryPath, std::string toolId,
                      PlatformToolServices services);
    ~DomainToolAdapter() override;

    ToolStatus initialize(const Values &initParams) override;
    ToolResult execute(const Values &inputs) override;
    ToolStatus release() override;

private:
    std::shared_ptr<PluginLoader>       loader_;
    std::filesystem::path               libraryPath_;
    std::string                         toolId_;
    PlatformToolServices                services_;
    std::shared_ptr<const LoadedModule> module_;
    const ToolRegistry::NativeFactory  *factory_ = nullptr;
    IToolInstance                      *raw_     = nullptr;
    ToolStatus                          failureStatus_;
};

} // namespace visionrt
