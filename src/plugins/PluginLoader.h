#pragma once

#include "core/Tool.h"
#include "visionrt/sdk/Module.h"

#include <QLibrary>
#include <filesystem>
#include <map>
#include <memory>
#include <mutex>
#include <string>
#include <string_view>

namespace visionrt {

struct LoadedModule {
    std::filesystem::path path;
    std::shared_ptr<QLibrary> library;
    const ModuleApi* api = nullptr;
};

class PluginLoader : public std::enable_shared_from_this<PluginLoader> {
public:
    PluginLoader() = default;
    ~PluginLoader() = default;

    void registerModule(std::string registryName, std::filesystem::path libraryPath);
    void loadIndexFile(const std::filesystem::path& indexFilePath);
    void setBaseDirectory(std::filesystem::path baseDirectory);
    const std::filesystem::path& baseDirectory() const noexcept { return baseDirectory_; }

    std::filesystem::path resolveModulePath(std::string_view registryName) const;
    bool hasModule(std::string_view registryName) const;

    std::shared_ptr<const LoadedModule> loadModule(const std::filesystem::path& libraryPath, ToolStatus& status);
    std::unique_ptr<IToolInstance> createAdapter(std::string_view registryName, const PlatformToolServices& services);

    std::size_t loadedModuleCount() const;

private:
    std::filesystem::path baseDirectory_;
    mutable std::mutex mutex_;
    std::map<std::string, std::filesystem::path, std::less<>> index_;
    std::map<std::filesystem::path, std::shared_ptr<LoadedModule>> loadedModules_;
};

class DomainToolAdapter final : public IToolInstance {
public:
    DomainToolAdapter(std::shared_ptr<PluginLoader> loader,
                      std::filesystem::path libraryPath,
                      std::string registryName,
                      PlatformToolServices services);
    ~DomainToolAdapter() override;

    ToolStatus initialize(const Values& initParams) override;
    ToolResult execute(const Values& inputs) override;
    ToolStatus release() override;

private:
    std::shared_ptr<PluginLoader> loader_;
    std::filesystem::path libraryPath_;
    std::string registryName_;
    PlatformToolServices services_;
    std::shared_ptr<const LoadedModule> module_;
    IToolInstance* raw_ = nullptr;
    ToolStatus failureStatus_;
};

} // namespace visionrt
