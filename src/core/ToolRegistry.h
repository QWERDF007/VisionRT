#pragma once

#include "core/PythonLoadState.h"
#include "core/Tool.h"
#include "visionrt/contracts/Registry.h"

#include <stdexcept>
#include <string_view>

namespace visionrt {

ToolDefinition parseDefinition(const std::filesystem::path &path, bool builtin);

enum class ToolAvailability
{
    Available,
    DefinitionMissing,
    ImplementationNotLoaded,
    ImplementationUnavailable
};

struct Availability
{
    ToolAvailability state;
    ToolStatus       status;
};

class RegistryError : public std::runtime_error
{
public:
    RegistryError(ToolAvailability reason, std::string message)
        : RegistryError(reason, ToolStatus{-3, std::move(message)})
    {
    }

    RegistryError(ToolAvailability reason, ToolStatus status)
        : std::runtime_error(status.message)
        , reason(reason)
        , status(std::move(status))
    {
    }

    ToolAvailability reason;
    ToolStatus       status;
};

class PluginLoader;
struct ToolsPackage;

// Mutate only between runs; definitions and their factories form a runtime snapshot.
class ToolRegistry : public IToolRegistry
{
public:
    struct NativeFactory
    {
        decltype(ToolFactoryDescriptor::create)  create;
        decltype(ToolFactoryDescriptor::destroy) destroy;
    };

    ToolStatus add(const ToolFactoryDescriptor &descriptor) noexcept override;

    const std::map<std::string, NativeFactory, std::less<>> &nativeFactories() const noexcept
    {
        return nativeFactories_;
    }

    const ToolStatus &registrationStatus() const noexcept
    {
        return registrationStatus_;
    }

    void registerPackage(const ToolsPackage &package);
    void loadNativeImplementations() const;
    void addDefinition(ToolDefinition definition);
    void replaceDefinition(ToolDefinition definition);
    void validateContract(const ToolDefinition &definition) const;
    void registerCpp(ToolDefinition contract, ToolFactory factory);
    void setPythonImplementation(std::string toolId, ToolFactory factory, ToolStatus status = {});

    using HostBindingFactory
        = std::function<std::unique_ptr<IToolInstance>(const ToolDefinition &, const PlatformToolServices &)>;
    void registerHostBinding(std::string bindingName, HostBindingFactory factory);

    const ToolDefinition            *find(std::string_view id) const;
    Availability                     availability(std::string_view id) const;
    std::shared_ptr<PythonLoadState> pythonLoadState(std::string_view id) const;
    std::unique_ptr<IToolInstance>   create(std::string_view id, const PlatformToolServices &services) const;
    std::unique_ptr<IToolInstance>   create(const ToolDefinition &definition, const PlatformToolServices &services) const;

    const std::vector<ToolDefinition> &definitions() const noexcept
    {
        return definitions_;
    }

    void setPluginLoader(std::shared_ptr<PluginLoader> loader)
    {
        pluginLoader_ = std::move(loader);
    }

    const std::shared_ptr<PluginLoader> &pluginLoader() const noexcept
    {
        return pluginLoader_;
    }

private:
    std::map<std::string, NativeFactory, std::less<>> nativeFactories_;
    ToolStatus                                        registrationStatus_;

    struct CppImplementation
    {
        ToolDefinition contract;
        ToolFactory    factory;
    };

    struct PythonImplementation
    {
        ToolFactory                      factory;
        std::shared_ptr<PythonLoadState> loadState;
    };

    std::shared_ptr<PluginLoader>                            pluginLoader_;
    std::vector<ToolDefinition>                              definitions_;
    std::map<std::string, std::size_t, std::less<>>          index_;
    std::map<std::string, CppImplementation, std::less<>>    cpp_;
    std::map<std::string, PythonImplementation, std::less<>> python_;
    std::map<std::string, HostBindingFactory, std::less<>>   hostBindingExecutors_;
};

} // namespace visionrt
