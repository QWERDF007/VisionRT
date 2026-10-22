#include "core/ToolRegistry.h"

#include "plugins/PluginLoader.h"
#include "core/ToolDeclarationYaml.h"

#include <yaml-cpp/yaml.h>

#include <algorithm>

namespace visionrt {
namespace {
std::string pathText(const std::filesystem::path &path)
{
    const auto text = path.generic_u8string();
    return {reinterpret_cast<const char *>(text.data()), text.size()};
}

bool cppKind(ToolKind kind)
{
    return kind == ToolKind::CppClass || kind == ToolKind::CppFunction;
}

const PortDefinition *portById(const std::vector<PortDefinition> &ports, std::string_view id)
{
    const auto found = std::find_if(ports.begin(), ports.end(), [&](const auto &port) { return port.id == id; });
    return found == ports.end() ? nullptr : &*found;
}

void align(const ToolDefinition &definition, const ToolDefinition &contract)
{
    if (definition.kind != contract.kind || definition.registryName != contract.registryName)
        throw ContractError("C++ tool kind/entry does not match registered contract: " + definition.id);
    const auto portsMatch = [](const auto &actual, const auto &expected, const char *direction)
    {
        if (actual.size() != expected.size())
            throw ContractError(std::string("C++ contract port count differs: ") + direction);
        for (const auto &port : actual)
        {
            const auto *other = portById(expected, port.id);
            if (!other || !compatible(port.type, other->type))
                throw ContractError(std::string("C++ contract differs at ") + direction + "." + port.id);
        }
    };
    portsMatch(definition.initParams, contract.initParams, "init_params");
    portsMatch(definition.inputs, contract.inputs, "inputs");
    portsMatch(definition.outputs, contract.outputs, "outputs");
}

} // namespace

ToolDefinition parseDefinitionNode(const YAML::Node &node, const std::filesystem::path &path, bool builtin)
{
    YAML::Emitter emitter;
    emitter << node;
    return parseToolDefinitionYaml(emitter.c_str(), path, builtin);
}

ToolDefinition parseDefinition(const std::filesystem::path &path, bool builtin)
{
    return parseToolDefinitionFile(path, builtin);
}

void ToolRegistry::registerPackage(const ToolsPackage &package)
{
    std::vector<ToolDefinition>                   definitions;
    std::map<std::string, std::size_t, std::less<>> index;
    std::map<std::string, std::string, std::less<>> bindings;
    for (const auto &tool : package.tools)
    {
        validateToolDefinition(tool.definition);
        validateContract(tool.definition);
        if (!index.emplace(tool.toolId, definitions.size()).second)
            throw ContractError("Duplicate packaged tool_id: " + tool.toolId);
        definitions.push_back(tool.definition);
        if (!tool.hostBinding.empty())
            bindings.emplace(tool.toolId, tool.hostBinding);
    }
    if (pluginLoader_)
        pluginLoader_->registerPackage(package);
    definitions_ = std::move(definitions);
    index_ = std::move(index);
    hostBindingsMap_ = std::move(bindings);
    python_.clear();
}

void ToolRegistry::addDefinition(ToolDefinition definition)
{
    validateToolDefinition(definition);
    if (const auto *previous = find(definition.id))
        throw ContractError("Duplicate tool_id " + definition.id + ": " + pathText(previous->definitionPath) + " and "
                            + pathText(definition.definitionPath));
    validateContract(definition);
    const auto index = definitions_.size();
    const auto id    = definition.id;
    definitions_.push_back(std::move(definition));
    index_.emplace(id, index);
}

void ToolRegistry::replaceDefinition(ToolDefinition definition)
{
    const auto existing = index_.find(definition.id);
    if (existing == index_.end())
    {
        addDefinition(std::move(definition));
        return;
    }
    validateToolDefinition(definition);
    validateContract(definition);
    definitions_[existing->second] = std::move(definition);
    python_.erase(existing->first);
}

void ToolRegistry::registerCpp(std::string registryName, ToolDefinition contract, ToolFactory factory)
{
    if (registryName.empty() || !factory || !cppKind(contract.kind))
        throw ContractError("Invalid C++ factory registration");
    if (contract.registryName.empty())
        contract.registryName = registryName;
    if (contract.registryName != registryName)
        throw ContractError("C++ registry_name disagrees with contract");
    if (cpp_.contains(registryName))
        throw ContractError("Duplicate C++ registry_name: " + registryName);
    validateToolDefinition(contract);
    for (const auto &definition : definitions_)
        if (cppKind(definition.kind) && definition.registryName == registryName)
            align(definition, contract);
    cpp_.emplace(std::move(registryName), CppImplementation{std::move(contract), std::move(factory)});
}

void ToolRegistry::validateContract(const ToolDefinition &definition) const
{
    if (cppKind(definition.kind))
    {
        const auto implementation = cpp_.find(definition.registryName);
        if (implementation != cpp_.end())
            align(definition, implementation->second.contract);
    }
}

void ToolRegistry::setPythonImplementation(std::string toolId, ToolFactory factory, ToolStatus status)
{
    const auto *definition = find(toolId);
    if (!definition || cppKind(definition->kind))
        throw ContractError("Python implementation requires a loaded Python definition: " + toolId);
    python_.insert_or_assign(std::move(toolId), PythonImplementation{std::move(factory), std::move(status)});
}

void ToolRegistry::registerHostBinding(std::string bindingName, HostBindingFactory factory)
{
    if (bindingName.empty() || !factory)
        throw ContractError("Invalid host binding registration");
    hostBindingExecutors_[std::move(bindingName)] = std::move(factory);
}

const ToolDefinition *ToolRegistry::find(std::string_view id) const
{
    const auto found = index_.find(id);
    return found == index_.end() ? nullptr : &definitions_[found->second];
}

Availability ToolRegistry::availability(std::string_view id) const
{
    const auto *definition = find(id);
    if (!definition)
        return {ToolAvailability::DefinitionMissing, "Tool definition missing: " + std::string(id)};
    const auto hostBindingIt = hostBindingsMap_.find(id);
    if (hostBindingIt != hostBindingsMap_.end())
    {
        const auto &bindingName = hostBindingIt->second;
        if (hostBindingExecutors_.contains(bindingName))
        {
            return {ToolAvailability::Available, {}};
        }
        return {ToolAvailability::ImplementationUnavailable, "Unknown host_binding: " + bindingName};
    }
    if (cppKind(definition->kind))
    {
        if (cpp_.contains(definition->registryName))
            return {ToolAvailability::Available, {}};
        if (pluginLoader_ && pluginLoader_->hasModule(definition->registryName))
            return {ToolAvailability::Available, {}};
        return {ToolAvailability::ImplementationUnavailable,
                "C++ implementation unavailable: " + definition->registryName + " ("
                    + pathText(definition->definitionPath) + ")"};
    }
    const auto implementation = python_.find(id);
    if (implementation != python_.end() && implementation->second.factory && implementation->second.status)
        return {ToolAvailability::Available, {}};
    return {ToolAvailability::ImplementationUnavailable,
            "Python implementation unavailable: " + std::string(id)
                + (implementation != python_.end() && !implementation->second.status.message.empty()
                       ? ": " + implementation->second.status.message
                       : " (not loaded)")};
}

std::unique_ptr<IToolInstance> ToolRegistry::create(std::string_view id, const PlatformToolServices &services) const
{
    const auto *definition = find(id);
    if (!definition)
        throw RegistryError(ToolAvailability::DefinitionMissing, "Tool definition is missing: " + std::string(id));
    return create(*definition, services);
}

std::unique_ptr<IToolInstance> ToolRegistry::create(const ToolDefinition &definition, const PlatformToolServices &services) const
{
    const std::string_view id = definition.id;
    const auto state = availability(id);
    if (state.state != ToolAvailability::Available)
        throw RegistryError(state.state, state.message);
    for (const auto &port : definition.outputs)
        if (port.typeFrom)
            throw ContractError("Tool output type has not been resolved: " + definition.id + "." + port.id);
    const auto  hostBindingIt = hostBindingsMap_.find(id);
    if (hostBindingIt != hostBindingsMap_.end())
    {
        const auto execIt = hostBindingExecutors_.find(hostBindingIt->second);
        if (execIt != hostBindingExecutors_.end() && execIt->second)
        {
            auto instance = execIt->second(definition, services);
            if (!instance)
                throw RegistryError(ToolAvailability::ImplementationUnavailable,
                                    "Host binding factory returned no instance: " + std::string(id));
            return instance;
        }
        throw RegistryError(ToolAvailability::ImplementationUnavailable,
                            "Unknown host_binding: " + hostBindingIt->second);
    }
    if (cppKind(definition.kind))
    {
        const auto it = cpp_.find(definition.registryName);
        if (it != cpp_.end() && it->second.factory)
        {
            auto instance = it->second.factory(services);
            if (!instance)
                throw RegistryError(ToolAvailability::ImplementationUnavailable,
                                    "Tool factory returned no instance: " + std::string(id));
            return instance;
        }
        if (pluginLoader_ && pluginLoader_->hasModule(definition.registryName))
        {
            auto instance = pluginLoader_->createAdapter(definition.registryName, services);
            if (!instance)
                throw RegistryError(ToolAvailability::ImplementationUnavailable,
                                    "Plugin loader returned no adapter: " + std::string(id));
            return instance;
        }
    }
    const auto &factory  = python_.find(id)->second.factory;
    auto        instance = factory(services);
    if (!instance)
        throw RegistryError(ToolAvailability::ImplementationUnavailable,
                            "Tool factory returned no instance: " + std::string(id));
    return instance;
}

} // namespace visionrt
