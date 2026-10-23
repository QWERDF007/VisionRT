#include "core/ToolRegistry.h"

#include "core/DefinitionJson.h"
#include "plugins/PluginLoader.h"

#include <algorithm>
#include <set>

namespace visionrt {
namespace {
std::string pathText(const std::filesystem::path &path)
{
    const auto text = path.generic_u8string();
    return {reinterpret_cast<const char *>(text.data()), text.size()};
}

void align(const ToolDefinition &definition, const ToolDefinition &contract)
{
    if (definition.kind != contract.kind || definition.id != contract.id)
        throw ContractError("C++ 工具种类或 ID 与注册合同不一致：" + definition.id);
    const auto portsMatch = [](const auto &actual, const auto &expected, const char *direction)
    {
        const auto business = [](const auto &port)
        {
            return !port.id.starts_with("__");
        };
        if (std::count_if(actual.begin(), actual.end(), business)
            != std::count_if(expected.begin(), expected.end(), business))
            throw ContractError(std::string("C++ 合同端口数量不一致：") + direction);
        for (const auto &port : actual)
        {
            if (!business(port))
                continue;
            const auto other
                = std::find_if(expected.begin(), expected.end(), [&](const auto &item) { return item.id == port.id; });
            if (other == expected.end() || !compatible(port.type, other->type))
                throw ContractError(std::string("C++ 合同端口不一致：") + direction + "." + port.id);
        }
    };
    portsMatch(definition.initParams, contract.initParams, "init_params");
    portsMatch(definition.inputs, contract.inputs, "inputs");
    portsMatch(definition.outputs, contract.outputs, "outputs");
}

} // namespace

ToolDefinition parseDefinition(const std::filesystem::path &path, bool builtin)
{
    return parseToolDefinitionFile(path, {builtin ? ToolOrigin::Builtin : ToolOrigin::Custom, {}});
}

ToolStatus ToolRegistry::add(const ToolFactoryDescriptor &descriptor) noexcept
{
    if (!registrationStatus_)
        return registrationStatus_;
    try
    {
        if (!descriptor.tool_id || isBlankMessage(descriptor.tool_id))
            registrationStatus_ = {-3, "工具工厂 ID 不能为空"};
        else if (!descriptor.create || !descriptor.destroy)
            registrationStatus_ = {-3, "工具工厂函数不完整：" + std::string(descriptor.tool_id)};
        else if (!nativeFactories_.emplace(descriptor.tool_id, NativeFactory{descriptor.create, descriptor.destroy})
                      .second)
            registrationStatus_ = {-3, "工具工厂 ID 重复：" + std::string(descriptor.tool_id)};
    }
    catch (const std::exception &error)
    {
        registrationStatus_ = {-3, "工具工厂注册失败", error.what()};
    }
    catch (...)
    {
        registrationStatus_ = {-3, "工具工厂注册发生未知异常"};
    }
    return registrationStatus_;
}

void ToolRegistry::registerPackage(const ToolsPackage &package)
{
    std::vector<ToolDefinition>                     definitions;
    std::map<std::string, std::size_t, std::less<>> index;
    for (const auto &tool : package.tools)
    {
        validateToolDefinition(tool.definition);
        validateContract(tool.definition);
        if (!index.emplace(tool.definition.id, definitions.size()).second)
            throw ContractError("工具包内 tool_id 重复：" + tool.definition.id);
        definitions.push_back(tool.definition);
    }
    if (pluginLoader_)
        pluginLoader_->registerPackage(package);
    definitions_ = std::move(definitions);
    index_       = std::move(index);
    python_.clear();
}

void ToolRegistry::loadNativeImplementations() const
{
    if (!pluginLoader_)
        return;
    std::set<std::filesystem::path> paths;
    for (const auto &definition : definitions_)
        if (definition.kind == ToolKind::Cpp && !cpp_.contains(definition.id))
        {
            const auto path = pluginLoader_->resolveModulePath(definition.id);
            if (!path.empty())
                paths.insert(path);
        }
    for (const auto &path : paths)
    {
        ToolStatus status;
        pluginLoader_->loadModule(path, status);
    }
}

void ToolRegistry::addDefinition(ToolDefinition definition)
{
    validateToolDefinition(definition);
    if (const auto *previous = find(definition.id))
        throw ContractError("tool_id 重复：" + definition.id + "，声明位于 " + pathText(previous->definitionPath)
                            + " 和 " + pathText(definition.definitionPath));
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

void ToolRegistry::registerCpp(ToolDefinition contract, ToolFactory factory)
{
    if (!factory || contract.kind != ToolKind::Cpp)
        throw ContractError("C++ 工厂注册无效：需要 cpp 工具种类和非空工厂");
    validateToolDefinition(contract);
    const auto id = contract.id;
    if (cpp_.contains(id))
        throw ContractError("C++ 工厂 tool_id 重复：" + id);
    if (const auto *definition = find(id))
        align(*definition, contract);
    cpp_.emplace(id, CppImplementation{std::move(contract), std::move(factory)});
}

void ToolRegistry::validateContract(const ToolDefinition &definition) const
{
    if (definition.kind == ToolKind::Cpp)
    {
        const auto implementation = cpp_.find(definition.id);
        if (implementation != cpp_.end())
            align(definition, implementation->second.contract);
    }
}

void ToolRegistry::setPythonImplementation(std::string toolId, ToolFactory factory, ToolStatus status)
{
    const auto *definition = find(toolId);
    if (!definition || (definition->kind != ToolKind::PythonFunction && definition->kind != ToolKind::PythonClass))
        throw ContractError("注册 Python 实现需要已加载的 Python 工具声明：" + toolId);
    std::shared_ptr<ToolStatus> statusPtr;
    if (const auto *pyFunctor = factory.target<PythonFactoryFunctor>())
        statusPtr = pyFunctor->status;
    if (!statusPtr)
        statusPtr = std::make_shared<ToolStatus>(std::move(status));
    python_.insert_or_assign(std::move(toolId), PythonImplementation{std::move(factory), std::move(statusPtr)});
}

void ToolRegistry::registerHostBinding(std::string bindingName, HostBindingFactory factory)
{
    if (bindingName.empty() || !factory)
        throw ContractError("宿主绑定注册无效：名称和工厂不能为空");
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
        return {
            ToolAvailability::DefinitionMissing,
            {-3, "工具定义缺失：" + std::string(id)}
        };
    if (definition->kind == ToolKind::Host)
    {
        const auto &bindingName = definition->hostBinding;
        if (hostBindingExecutors_.contains(bindingName))
        {
            return {ToolAvailability::Available, {}};
        }
        return {
            ToolAvailability::ImplementationUnavailable,
            {-3, "宿主绑定未注册：" + bindingName}
        };
    }
    if (definition->kind == ToolKind::Cpp)
    {
        if (cpp_.contains(definition->id))
            return {ToolAvailability::Available, {}};
        if (pluginLoader_)
            return pluginLoader_->availability(definition->id);
        return {
            ToolAvailability::ImplementationUnavailable,
            {-3, "C++ 实现不可用：" + definition->id, pathText(definition->definitionPath)}
        };
    }
    const auto implementation = python_.find(id);
    if (implementation != python_.end())
    {
        if (implementation->second.status && !*implementation->second.status)
            return {ToolAvailability::ImplementationUnavailable, *implementation->second.status};
        if (implementation->second.factory && implementation->second.status && *implementation->second.status)
            return {ToolAvailability::Available, {}};
    }
    return {
        ToolAvailability::ImplementationUnavailable,
        {-3, "Python 实现尚未加载：" + std::string(id)}
    };
}

std::unique_ptr<IToolInstance> ToolRegistry::create(std::string_view id, const PlatformToolServices &services) const
{
    const auto *definition = find(id);
    if (!definition)
        throw RegistryError(ToolAvailability::DefinitionMissing, availability(id).status);
    return create(*definition, services);
}

std::unique_ptr<IToolInstance> ToolRegistry::create(const ToolDefinition       &definition,
                                                    const PlatformToolServices &services) const
{
    const std::string_view id    = definition.id;
    const auto             state = availability(id);
    if (state.state != ToolAvailability::Available)
        throw RegistryError(state.state, state.status);
    for (const auto &port : definition.outputs)
        if (port.typeFrom)
            throw ContractError("工具输出类型尚未解析：" + definition.id + "." + port.id);
    if (definition.kind == ToolKind::Host)
    {
        const auto execIt = hostBindingExecutors_.find(definition.hostBinding);
        if (execIt != hostBindingExecutors_.end() && execIt->second)
        {
            auto instance = execIt->second(definition, services);
            if (!instance)
                throw RegistryError(ToolAvailability::ImplementationUnavailable,
                                    "宿主绑定工厂返回空实例：" + std::string(id));
            return instance;
        }
        throw RegistryError(ToolAvailability::ImplementationUnavailable, "宿主绑定未注册：" + definition.hostBinding);
    }
    if (definition.kind == ToolKind::Cpp)
    {
        const auto it = cpp_.find(definition.id);
        if (it != cpp_.end() && it->second.factory)
        {
            auto instance = it->second.factory(services);
            if (!instance)
                throw RegistryError(ToolAvailability::ImplementationUnavailable,
                                    "工具工厂返回空实例：" + std::string(id));
            return instance;
        }
        if (pluginLoader_)
            return pluginLoader_->createAdapter(definition.id, services);
    }
    const auto it = python_.find(id);
    if (it == python_.end() || !it->second.factory)
        throw RegistryError(ToolAvailability::ImplementationUnavailable, "Python 实现尚未加载：" + std::string(id));
    try
    {
        auto instance = it->second.factory(services);
        if (!instance)
            throw RegistryError(ToolAvailability::ImplementationUnavailable, "工具工厂返回空实例：" + std::string(id));
        return instance;
    }
    catch (const RegistryError &error)
    {
        if (it->second.status && error.reason == ToolAvailability::ImplementationUnavailable)
            *it->second.status = error.status;
        throw;
    }
}

} // namespace visionrt
