#pragma once

#include "core/Tool.h"

#include <stdexcept>
#include <string_view>

namespace YAML {
class Node;
}

namespace visionrt {

ToolDefinition parseDefinition(const std::filesystem::path &path, bool builtin);
ToolDefinition parseDefinitionNode(const YAML::Node &node, const std::filesystem::path &path, bool builtin);

enum class ToolAvailability
{
    Available,
    DefinitionMissing,
    ImplementationUnavailable
};

struct Availability
{
    ToolAvailability state;
    std::string      message;
};

struct RegistryDiagnostic
{
    std::filesystem::path path;
    std::string           message;
    std::filesystem::path relatedPath;
};

class RegistryError : public std::runtime_error
{
public:
    RegistryError(ToolAvailability reason, std::string message)
        : std::runtime_error(std::move(message))
        , reason(reason)
    {
    }

    ToolAvailability reason;
};

class PluginLoader;
struct ToolsPackage;

// Mutate only between runs; definitions and their factories form a runtime snapshot.
class ToolRegistry
{
public:
    // Replaces the tool set with a validated package: inside the package root the index is the single discovery source.
    void registerPackage(const ToolsPackage &package);
    // Legacy discovery for tool directories that are not delivered as a package.
    void scan(const std::filesystem::path &toolsRoot);
    void scanRoots(const std::vector<std::filesystem::path> &roots);
    void scanPython(const std::filesystem::path &toolsRoot);
    void scanPythonRoots();

    const std::vector<std::filesystem::path> &managedRoots() const noexcept
    {
        return managedRoots_;
    }

    void                                addDefinition(ToolDefinition definition);
    void                                replaceDefinition(ToolDefinition definition);
    void                                validateContract(const ToolDefinition &definition) const;
    std::vector<const ToolDefinition *> cppContracts() const;
    void registerCpp(std::string registryName, ToolDefinition contract, ToolFactory factory);
    void setPythonImplementation(std::string toolId, ToolFactory factory, ToolStatus status = {});

    using HostBindingFactory
        = std::function<std::unique_ptr<IToolInstance>(const ToolDefinition &, const PlatformToolServices &)>;
    void registerHostBinding(std::string bindingName, HostBindingFactory factory);
    void bindHostTool(std::string toolId, std::string bindingName);

    const ToolDefinition          *find(std::string_view id) const;
    Availability                   availability(std::string_view id) const;
    std::unique_ptr<IToolInstance> create(std::string_view id, const PlatformToolServices &services) const;

    const std::vector<ToolDefinition> &definitions() const noexcept
    {
        return definitions_;
    }

    const std::vector<RegistryDiagnostic> &diagnostics() const noexcept
    {
        return diagnostics_;
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
    void                               rebuildDefinitions(bool pythonOnly);
    std::vector<std::filesystem::path> managedRoots_;
    std::vector<ToolDefinition>        packageDefinitions_;
    std::filesystem::path              packageRoot_;

    struct CppImplementation
    {
        ToolDefinition contract;
        ToolFactory    factory;
    };

    struct PythonImplementation
    {
        ToolFactory factory;
        ToolStatus  status;
    };

    std::shared_ptr<PluginLoader>                            pluginLoader_;
    std::vector<ToolDefinition>                              definitions_;
    std::map<std::string, std::size_t, std::less<>>          index_;
    std::vector<RegistryDiagnostic>                          diagnostics_;
    std::map<std::string, CppImplementation, std::less<>>    cpp_;
    std::map<std::string, PythonImplementation, std::less<>> python_;
    std::map<std::string, std::string, std::less<>>          hostBindingsMap_;
    std::map<std::string, HostBindingFactory, std::less<>>   hostBindingExecutors_;
};

} // namespace visionrt
