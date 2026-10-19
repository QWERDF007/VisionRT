#pragma once

#include "core/Tool.h"
#include "visionrt/sdk/Module.h"
#include "visionrt/sdk/Package.h"

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
#include <filesystem>
#include <map>
#include <memory>
#include <mutex>
#include <string>
#include <string_view>
#include <vector>

namespace visionrt {

struct LoadedModule
{
    std::filesystem::path path;
#ifdef _WIN32
    HMODULE handle = nullptr;
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

    LoadedModule(LoadedModule &&other) noexcept
        : path(std::move(other.path))
        , handle(other.handle)
        , api(other.api)
    {
        other.handle = nullptr;
        other.api    = nullptr;
    }

    LoadedModule &operator=(LoadedModule &&other) noexcept
    {
        if (this != &other)
        {
            path = std::move(other.path);
            if (handle)
                FreeLibrary(handle);
            handle       = other.handle;
            other.handle = nullptr;
            api          = other.api;
            other.api    = nullptr;
        }
        return *this;
    }
#else
    std::shared_ptr<QLibrary> library;
#endif
    const ModuleApi *api = nullptr;
};

class PluginLoader : public std::enable_shared_from_this<PluginLoader>
{
public:
    PluginLoader()  = default;
    ~PluginLoader() = default;

    // Low-level seam for registering a single known module (tests, embedded modules).
    void registerModule(std::string registryName, std::filesystem::path libraryPath);

    // Replaces the factory mapping with the modules of a package read by the SDK.
    void registerPackage(const ToolsPackage &package);

    std::filesystem::path resolveModulePath(std::string_view registryName) const;
    bool                  hasModule(std::string_view registryName) const;

    std::shared_ptr<const LoadedModule> loadModule(const std::filesystem::path &libraryPath, ToolStatus &status);
    std::unique_ptr<IToolInstance> createAdapter(std::string_view registryName, const PlatformToolServices &services);

    std::size_t loadedModuleCount() const;

private:
    mutable std::mutex                                             mutex_;
    std::map<std::string, std::filesystem::path, std::less<>>      index_;
    std::map<std::filesystem::path, std::shared_ptr<LoadedModule>> loadedModules_;
};

class DomainToolAdapter final : public IToolInstance
{
public:
    DomainToolAdapter(std::shared_ptr<PluginLoader> loader, std::filesystem::path libraryPath, std::string registryName,
                      PlatformToolServices services);
    ~DomainToolAdapter() override;

    ToolStatus initialize(const Values &initParams) override;
    ToolResult execute(const Values &inputs) override;
    ToolStatus release() override;

private:
    std::shared_ptr<PluginLoader>       loader_;
    std::filesystem::path               libraryPath_;
    std::string                         registryName_;
    PlatformToolServices                services_;
    std::shared_ptr<const LoadedModule> module_;
    IToolInstance                      *raw_ = nullptr;
    ToolStatus                          failureStatus_;
};

} // namespace visionrt
