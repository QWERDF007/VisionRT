#pragma once

#include "plugins/Export.h"

#include "core/Tool.h"
#include "core/ToolPackage.h"
#include "core/ToolRegistry.h"
#include "visionrt/contracts/Module.h"

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

struct LoadedModule;

class VISIONRT_PLUGINS_API PluginLoader final : public NativeImplementationProvider
{
public:
    PluginLoader()  = default;
    ~PluginLoader() = default;

    // Low-level seam for registering a single known module (tests, embedded modules).
    void registerModule(std::string toolId, std::filesystem::path libraryPath);

    void registerPackage(const ToolsPackage &package) override;
    void loadImplementations(std::span<const std::string_view> toolIds) override;

    std::filesystem::path resolveModulePath(std::string_view toolId) const;
    Availability          availability(std::string_view toolId) const override;

    std::shared_ptr<const LoadedModule> loadModule(const std::filesystem::path &libraryPath, ToolStatus &status);
    std::unique_ptr<IToolInstance>      createAdapter(std::string_view toolId) override;

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

class VISIONRT_PLUGINS_API DomainToolAdapter final : public IToolInstance, public ILifecycleDiagnostics
{
public:
    DomainToolAdapter(std::shared_ptr<const LoadedModule> module, std::string toolId);
    ~DomainToolAdapter() override;

    ToolStatus initialize(const ToolConfiguration &initParams) override;
    ToolResult execute(const Values &inputs) override;
    ToolResult train(const TrainingSet &, const Values &) override;
    ToolStatus release() override;
    DiagnosticOrigin lifecycleOrigin() const noexcept override { return origin_; }

private:
    std::string                         toolId_;
    std::shared_ptr<const LoadedModule> module_;
    const ToolRegistry::NativeFactory  *factory_     = nullptr;
    IToolInstance                      *raw_         = nullptr;
    bool                                initialized_ = false;
    ToolStatus                          failureStatus_;
    DiagnosticOrigin                    origin_ = DiagnosticOrigin::System;
};

} // namespace visionrt
