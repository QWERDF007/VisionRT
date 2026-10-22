#include "plugins/PluginLoader.h"

#include "core/JsonFields.h"

#include <algorithm>

#ifndef NOMINMAX
#    define NOMINMAX
#endif

#ifdef _WIN32
#    ifndef WIN32_LEAN_AND_MEAN
#        define WIN32_LEAN_AND_MEAN
#    endif
#    include <windows.h>
#else
#    include <QLibrary>
#endif

namespace visionrt {

void PluginLoader::registerModule(std::string toolId, std::filesystem::path libraryPath)
{
    if (!libraryPath.empty())
        libraryPath = std::filesystem::absolute(libraryPath).lexically_normal();
    std::lock_guard lock(mutex_);
    failures_.clear();
    index_[std::move(toolId)] = std::move(libraryPath);
}

void PluginLoader::registerPackage(const ToolsPackage &package)
{
    std::map<std::string, std::filesystem::path, std::less<>> index;
    for (const auto &tool : package.tools)
    {
        if (tool.moduleLibrary.empty())
            continue;
        const auto path     = std::filesystem::absolute(tool.moduleLibrary).lexically_normal();
        const auto inserted = index.emplace(tool.definition.id, path);
        // Two modules claiming one factory name would make the second tool silently
        // load the wrong library, so the combination is rejected instead (D-04).
        if (!inserted.second && inserted.first->second != path)
            throw ContractError("tool_id '" + tool.definition.id + "' is provided by two different modules: "
                                + inserted.first->second.string() + " and " + tool.moduleLibrary.string());
    }
    std::lock_guard lock(mutex_);
    failures_.clear();
    index_ = std::move(index);
}

std::filesystem::path PluginLoader::resolveModulePath(std::string_view toolId) const
{
    std::lock_guard lock(mutex_);
    const auto      it = index_.find(toolId);
    return it == index_.end() ? std::filesystem::path{} : it->second;
}

Availability PluginLoader::availability(std::string_view toolId) const
{
    std::unique_lock lock(mutex_);
    const auto       entry = index_.find(toolId);
    if (entry == index_.end() || entry->second.empty())
        return {ToolAvailability::ImplementationUnavailable, "工具未关联模块：" + std::string(toolId)};
    const auto path = entry->second;
    if (const auto loaded = loadedModules_.find(path); loaded != loadedModules_.end())
    {
        if (loaded->second->registry.nativeFactories().contains(toolId))
            return {ToolAvailability::Available, {}};
        return {ToolAvailability::ImplementationUnavailable, "指定模块未注册工具工厂：" + std::string(toolId),
                json::pathText(path).toStdString()};
    }
    if (const auto failure = failures_.find(path); failure != failures_.end())
        return {ToolAvailability::ImplementationUnavailable, failure->second.message, failure->second.detail};
    lock.unlock();
    std::error_code error;
    if (!std::filesystem::is_regular_file(path, error))
        return {ToolAvailability::ImplementationUnavailable, "模块库文件不可用：" + std::string(toolId),
                json::pathText(path).toStdString() + (error ? ": " + error.message() : "")};
    return {ToolAvailability::ImplementationNotLoaded, "模块尚未加载：" + std::string(toolId)};
}

std::shared_ptr<const LoadedModule> PluginLoader::loadModule(const std::filesystem::path &libraryPath,
                                                             ToolStatus                  &status)
{
    if (libraryPath.empty())
    {
        status = {-3, "模块库路径为空"};
        return nullptr;
    }
    const auto       key = std::filesystem::absolute(libraryPath).lexically_normal();
    std::unique_lock lock(mutex_);
    for (;;)
    {
        if (const auto found = loadedModules_.find(key); found != loadedModules_.end())
        {
            status = {};
            return found->second;
        }
        if (const auto failed = failures_.find(key); failed != failures_.end())
        {
            status = failed->second;
            return nullptr;
        }
        const auto pending = loading_.find(key);
        if (pending == loading_.end())
        {
            loading_.emplace(key, PendingLoad{std::this_thread::get_id()});
            break;
        }
        if (pending->second.owner == std::this_thread::get_id())
        {
            status = {-3, "检测到同路径模块重入加载：" + key.string()};
            return nullptr;
        }
        loadedChanged_.wait(lock);
    }
    lock.unlock();

    ToolStatus                    local;
    std::shared_ptr<LoadedModule> module;
    try
    {
        module = loadModuleFile(key, local);
    }
    catch (const std::exception &error)
    {
        local = {-3, "模块加载异常", error.what()};
    }
    catch (...)
    {
        local = {-3, "模块加载发生未知异常"};
    }

    lock.lock();
    loading_.erase(key);
    if (module)
    {
        for (const auto &[path, active] : loadedModules_)
        {
            const bool referenced
                = std::any_of(index_.begin(), index_.end(), [&](const auto &entry) { return entry.second == path; });
            if (!referenced)
                continue;
            for (const auto &[id, factory] : module->registry.nativeFactories())
            {
                if (active->registry.nativeFactories().contains(id))
                {
                    local = {-3, "不同模块注册了重复的工具工厂 ID：" + id};
                    break;
                }
            }
            if (!local)
                break;
        }
    }
    auto rejected = !local ? std::move(module) : nullptr;
    if (module)
    {
        loadedModules_[key] = module;
        status              = {};
    }
    else
    {
        failures_[key] = local;
        status         = local;
    }
    loadedChanged_.notify_all();
    lock.unlock();
    return module;
}

std::shared_ptr<LoadedModule> PluginLoader::loadModuleFile(const std::filesystem::path &libraryPath, ToolStatus &status)
{
    if (!std::filesystem::exists(libraryPath))
    {
        status = {-3, "模块库文件不存在：" + libraryPath.string()};
        return nullptr;
    }

    auto loaded = std::make_shared<LoadedModule>();
#ifdef _WIN32
    static const DWORD directoryPolicyError = []
    {
        return SetDefaultDllDirectories(LOAD_LIBRARY_SEARCH_DEFAULT_DIRS) ? ERROR_SUCCESS : GetLastError();
    }();
    if (directoryPolicyError != ERROR_SUCCESS)
    {
        status = {-3, "无法配置运行时 DLL 搜索目录（Win32 错误：" + std::to_string(directoryPolicyError) + "）"};
        return nullptr;
    }
    const auto absPath = std::filesystem::absolute(libraryPath).lexically_normal();
    loaded->directory.reset(AddDllDirectory(absPath.parent_path().wstring().c_str()));
    if (!loaded->directory)
    {
        const DWORD error = GetLastError();
        status = {-3, "无法注册运行时 DLL 目录 " + libraryPath.parent_path().string() + "（Win32 错误："
                          + std::to_string(error) + "）"};
        return nullptr;
    }
    HMODULE handle = LoadLibraryExW(absPath.wstring().c_str(), nullptr,
                                    LOAD_LIBRARY_SEARCH_DLL_LOAD_DIR | LOAD_LIBRARY_SEARCH_DEFAULT_DIRS);
    if (!handle)
    {
        const DWORD err = GetLastError();
        status = {-3, "无法加载动态库 " + libraryPath.string() + "（Win32 错误：" + std::to_string(err) + "）"};
        return nullptr;
    }
    loaded->handle = handle;

    auto proc = GetProcAddress(handle, "visiontoolsModuleApi");
    if (!proc)
    {
        status = {-3, "动态库缺少入口符号 visiontoolsModuleApi：" + libraryPath.string()};
        return nullptr;
    }

    auto apiFunc = reinterpret_cast<const ModuleApi *(VT_CALL *)() noexcept>(proc);
#else
    auto       lib      = std::make_shared<QLibrary>(QString::fromStdString(libraryPath.string()));
    const bool loadedOk = lib->load();
    if (!loadedOk)
    {
        status = {-3, "无法加载动态库 " + libraryPath.string(), lib->errorString().toStdString()};
        return nullptr;
    }

    auto apiFunc = reinterpret_cast<const ModuleApi *(VT_CALL *)() noexcept>(lib->resolve("visiontoolsModuleApi"));
    if (!apiFunc)
    {
        status = {-3, "动态库缺少入口符号 visiontoolsModuleApi：" + libraryPath.string()};
        return nullptr;
    }
    loaded->library = lib;
#endif
    const auto *api = apiFunc();
    if (!api)
    {
        status = {-3, "visiontoolsModuleApi() 返回空：" + libraryPath.string()};
        return nullptr;
    }

    if (api->abi_major != MODULE_ABI_MAJOR)
    {
        status = {-3, "模块 ABI 主版本不匹配（期望 " + std::to_string(MODULE_ABI_MAJOR) + "，实际 "
                          + std::to_string(api->abi_major) + "）"};
        return nullptr;
    }

    if (api->struct_bytes < sizeof(ModuleApi))
    {
        status = {-3, "ModuleApi 结构体大小不匹配"};
        return nullptr;
    }

    if (!api->register_tools)
    {
        status = {-3, "ModuleApi 缺少工具注册函数"};
        return nullptr;
    }
    status = api->register_tools(loaded->registry);
    if (!loaded->registry.registrationStatus())
        status = loaded->registry.registrationStatus();
    if (!status)
        return nullptr;
    loaded->path = libraryPath;
    status       = {};
    return loaded;
}

std::unique_ptr<IToolInstance> PluginLoader::createAdapter(std::string_view            toolId,
                                                           const PlatformToolServices &services)
{
    const auto path = resolveModulePath(toolId);
    ToolStatus status;
    auto       module = loadModule(path, status);
    if (!module)
        throw RegistryError(ToolAvailability::ImplementationUnavailable, std::move(status));
    return std::make_unique<DomainToolAdapter>(std::move(module), std::string(toolId), services);
}

std::size_t PluginLoader::loadedModuleCount() const
{
    std::lock_guard lock(mutex_);
    return loadedModules_.size();
}

DomainToolAdapter::DomainToolAdapter(std::shared_ptr<const LoadedModule> module, std::string toolId,
                                     PlatformToolServices services)
    : toolId_(std::move(toolId))
    , services_(std::move(services))
    , module_(std::move(module))
{
    const auto found = module_->registry.nativeFactories().find(toolId_);
    if (found == module_->registry.nativeFactories().end())
        throw RegistryError(
            ToolAvailability::ImplementationUnavailable,
            ToolStatus{-3, "指定模块未注册工具工厂：" + toolId_, json::pathText(module_->path).toStdString()});
    factory_ = &found->second;
    ToolStatus createStatus;
    raw_ = factory_->create(&services_, &createStatus);
    if (!raw_ || !createStatus)
    {
        if (createStatus)
            createStatus = {-3, "模块创建返回空实例：" + toolId_};
        release();
        throw RegistryError(ToolAvailability::ImplementationUnavailable, std::move(createStatus));
    }
}

DomainToolAdapter::~DomainToolAdapter()
{
    release();
}

ToolStatus DomainToolAdapter::initialize(const Values &initParams)
{
    if (!failureStatus_)
        return failureStatus_;
    if (!raw_ || initialized_)
        return {-2, "工具实例已释放或已初始化：" + toolId_};

    ToolStatus status;
    try
    {
        status = raw_->initialize(initParams);
    }
    catch (const std::exception &error)
    {
        status = {-1, "工具初始化异常", error.what()};
    }
    catch (...)
    {
        status = {-1, "工具初始化发生未知异常"};
    }

    if (!status)
    {
        release();
        failureStatus_ = status;
        return failureStatus_;
    }

    initialized_ = true;
    return status;
}

ToolResult DomainToolAdapter::execute(const Values &inputs)
{
    if (!raw_ || !initialized_)
    {
        const auto status
            = failureStatus_.code != 0 ? failureStatus_ : ToolStatus{-2, "工具实例尚未初始化，不能执行：" + toolId_};
        return ToolResult{status, {}};
    }
    try
    {
        return raw_->execute(inputs);
    }
    catch (const std::exception &error)
    {
        return ToolResult{
            {-1, "工具执行异常", error.what()},
            {}
        };
    }
    catch (...)
    {
        return ToolResult{
            {-1, "工具执行发生未知异常"},
            {}
        };
    }
}

ToolStatus DomainToolAdapter::release()
{
    if (!raw_)
        return {};
    ToolStatus status;
    try
    {
        status = raw_->release();
    }
    catch (const std::exception &error)
    {
        status = {-4, "工具释放异常", error.what()};
    }
    catch (...)
    {
        status = {-4, "工具释放发生未知异常"};
    }
    factory_->destroy(raw_);
    raw_         = nullptr;
    initialized_ = false;
    factory_     = nullptr;
    module_.reset();
    return status;
}

} // namespace visionrt
