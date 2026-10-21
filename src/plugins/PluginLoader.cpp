#include "plugins/PluginLoader.h"

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

void PluginLoader::registerModule(std::string registryName, std::filesystem::path libraryPath)
{
    std::lock_guard lock(mutex_);
    index_[std::move(registryName)] = std::move(libraryPath);
}

void PluginLoader::registerPackage(const ToolsPackage &package)
{
    std::map<std::string, std::filesystem::path, std::less<>> index;
    for (const auto &tool : package.tools)
    {
        if (tool.moduleLibrary.empty())
            continue;
        const auto inserted = index.emplace(tool.registryName, tool.moduleLibrary);
        // Two modules claiming one factory name would make the second tool silently
        // load the wrong library, so the combination is rejected instead (D-04).
        if (!inserted.second && inserted.first->second != tool.moduleLibrary)
            throw ContractError("registry_name '" + tool.registryName
                                + "' is provided by two different modules: " + inserted.first->second.string() + " and "
                                + tool.moduleLibrary.string());
    }
    std::lock_guard lock(mutex_);
    index_ = std::move(index);
}

std::filesystem::path PluginLoader::resolveModulePath(std::string_view registryName) const
{
    std::lock_guard lock(mutex_);
    const auto      it = index_.find(registryName);
    return it == index_.end() ? std::filesystem::path{} : it->second;
}

bool PluginLoader::hasModule(std::string_view registryName) const
{
    return !resolveModulePath(registryName).empty();
}

std::shared_ptr<const LoadedModule> PluginLoader::loadModule(const std::filesystem::path &libraryPath,
                                                             ToolStatus                  &status)
{
    if (libraryPath.empty())
    {
        status = {-3, "Module library path is empty"};
        return nullptr;
    }

    std::lock_guard lock(mutex_);
    const auto      it = loadedModules_.find(libraryPath);
    if (it != loadedModules_.end())
    {
        status = {};
        return it->second;
    }

    if (!std::filesystem::exists(libraryPath))
    {
        status = {-3, "Module library file not found: " + libraryPath.string()};
        return nullptr;
    }

#ifdef _WIN32
    static const DWORD directoryPolicyError = []
    {
        return SetDefaultDllDirectories(LOAD_LIBRARY_SEARCH_DEFAULT_DIRS) ? ERROR_SUCCESS : GetLastError();
    }();
    if (directoryPolicyError != ERROR_SUCCESS)
    {
        status = {-3, "Failed to configure runtime DLL search (Win32 error: "
                          + std::to_string(directoryPolicyError) + ")"};
        return nullptr;
    }
    const auto absPath = std::filesystem::absolute(libraryPath).lexically_normal();
    auto loaded = std::make_shared<LoadedModule>();
    loaded->directory.reset(AddDllDirectory(absPath.parent_path().wstring().c_str()));
    if (!loaded->directory)
    {
        const DWORD error = GetLastError();
        status = {-3, "Failed to register runtime DLL directory " + libraryPath.parent_path().string()
                          + " (Win32 error: " + std::to_string(error) + ")"};
        return nullptr;
    }
    HMODULE handle = LoadLibraryExW(absPath.wstring().c_str(), nullptr,
                                    LOAD_LIBRARY_SEARCH_DLL_LOAD_DIR | LOAD_LIBRARY_SEARCH_DEFAULT_DIRS);
    if (!handle)
    {
        const DWORD err = GetLastError();
        status = {-3, "Failed to load dynamic library " + libraryPath.string() + " (Win32 error: " + std::to_string(err)
                          + ")"};
        return nullptr;
    }
    loaded->handle = handle;

    auto proc = GetProcAddress(handle, "visiontoolsModuleApi");
    if (!proc)
    {
        status = {-3, "Entry symbol 'visiontoolsModuleApi' not found in " + libraryPath.string()};
        return nullptr;
    }

    auto        apiFunc = reinterpret_cast<const ModuleApi *(VT_CALL *)() noexcept>(proc);
    const auto *api     = apiFunc();
    if (!api)
    {
        status = {-3, "visiontoolsModuleApi() returned null in " + libraryPath.string()};
        return nullptr;
    }

    if (api->abi_major != MODULE_ABI_MAJOR)
    {
        status = {-3, "Module ABI major mismatch (expected " + std::to_string(MODULE_ABI_MAJOR) + ", got "
                          + std::to_string(api->abi_major) + ")"};
        return nullptr;
    }

    if (api->struct_bytes < sizeof(ModuleApi))
    {
        status = {-3, "ModuleApi struct size mismatch"};
        return nullptr;
    }

    if (!api->create || !api->destroy)
    {
        status = {-3, "ModuleApi function pointers are incomplete"};
        return nullptr;
    }

    loaded->path   = libraryPath;
    loaded->api    = api;
    loadedModules_.emplace(libraryPath, loaded);
    status = {};
    return loaded;
#else
    auto       lib      = std::make_shared<QLibrary>(QString::fromStdString(libraryPath.string()));
    const bool loadedOk = lib->load();
    if (!loadedOk)
    {
        status
            = {-3, "Failed to load dynamic library " + libraryPath.string() + ": " + lib->errorString().toStdString()};
        return nullptr;
    }

    auto apiFunc = reinterpret_cast<const ModuleApi *(VT_CALL *)() noexcept>(lib->resolve("visiontoolsModuleApi"));
    if (!apiFunc)
    {
        status = {-3, "Entry symbol 'visiontoolsModuleApi' not found in " + libraryPath.string()};
        return nullptr;
    }

    const auto *api = apiFunc();
    if (!api)
    {
        status = {-3, "visiontoolsModuleApi() returned null in " + libraryPath.string()};
        return nullptr;
    }

    if (api->abi_major != MODULE_ABI_MAJOR)
    {
        status = {-3, "Module ABI major mismatch (expected " + std::to_string(MODULE_ABI_MAJOR) + ", got "
                          + std::to_string(api->abi_major) + ")"};
        return nullptr;
    }

    if (api->struct_bytes < sizeof(ModuleApi))
    {
        status = {-3, "ModuleApi struct size mismatch"};
        return nullptr;
    }

    if (!api->create || !api->destroy)
    {
        status = {-3, "ModuleApi function pointers are incomplete"};
        return nullptr;
    }

    auto loaded     = std::make_shared<LoadedModule>();
    loaded->path    = libraryPath;
    loaded->library = lib;
    loaded->api     = api;
    loadedModules_.emplace(libraryPath, loaded);
    status = {};
    return loaded;
#endif
}

std::unique_ptr<IToolInstance> PluginLoader::createAdapter(std::string_view            registryName,
                                                           const PlatformToolServices &services)
{
    const auto path = resolveModulePath(registryName);
    return std::make_unique<DomainToolAdapter>(shared_from_this(), path, std::string(registryName), services);
}

std::size_t PluginLoader::loadedModuleCount() const
{
    std::lock_guard lock(mutex_);
    return loadedModules_.size();
}

DomainToolAdapter::DomainToolAdapter(std::shared_ptr<PluginLoader> loader, std::filesystem::path libraryPath,
                                     std::string registryName, PlatformToolServices services)
    : loader_(std::move(loader))
    , libraryPath_(std::move(libraryPath))
    , registryName_(std::move(registryName))
    , services_(std::move(services))
{
}

DomainToolAdapter::~DomainToolAdapter()
{
    if (raw_)
    {
        try
        {
            raw_->release();
        }
        catch (...)
        {
        }
        try
        {
            if (module_ && module_->api && module_->api->destroy)
            {
                module_->api->destroy(raw_);
            }
        }
        catch (...)
        {
        }
        raw_ = nullptr;
    }
}

ToolStatus DomainToolAdapter::initialize(const Values &initParams)
{
    if (failureStatus_.code != 0)
        return failureStatus_;

    if (raw_)
    {
        try
        {
            raw_->release();
        }
        catch (...)
        {
        }
        if (module_ && module_->api && module_->api->destroy)
        {
            module_->api->destroy(raw_);
        }
        raw_ = nullptr;
    }

    ToolStatus status;
    module_ = loader_->loadModule(libraryPath_, status);
    if (!module_ || !module_->api || status.code != 0)
    {
        failureStatus_ = (status.code != 0 ? status : ToolStatus{-3, "Module load failed: " + libraryPath_.string()});
        return failureStatus_;
    }

    ToolStatus createStatus;
    raw_ = module_->api->create(registryName_.c_str(), &services_, &createStatus);
    if (!raw_)
    {
        failureStatus_
            = (createStatus.code != 0 ? createStatus
                                      : ToolStatus{-3, "Module create returned null instance: " + registryName_});
        return failureStatus_;
    }

    if (createStatus.code != 0)
    {
        try
        {
            module_->api->destroy(raw_);
        }
        catch (...)
        {
        }
        raw_           = nullptr;
        failureStatus_ = createStatus;
        return failureStatus_;
    }

    try
    {
        status = raw_->initialize(initParams);
    }
    catch (const std::exception &error)
    {
        status = {-1, "Tool initialization exception: " + std::string(error.what())};
    }
    catch (...)
    {
        status = {-1, "Tool initialization unknown exception"};
    }

    if (!status)
    {
        try
        {
            raw_->release();
        }
        catch (...)
        {
        }
        try
        {
            module_->api->destroy(raw_);
        }
        catch (...)
        {
        }
        raw_           = nullptr;
        failureStatus_ = status;
        return failureStatus_;
    }

    return status;
}

ToolResult DomainToolAdapter::execute(const Values &inputs)
{
    if (!raw_)
    {
        if (failureStatus_.code == 0)
        {
            auto s = initialize({});
            if (s.code != 0)
            {
                return ToolResult{s, {}};
            }
        }
    }
    if (!raw_)
    {
        const auto s = (failureStatus_.code != 0 ? failureStatus_
                                                 : ToolStatus{-3, "Tool instance unavailable: " + registryName_});
        return ToolResult{s, {}};
    }
    try
    {
        return raw_->execute(inputs);
    }
    catch (const std::exception &error)
    {
        return ToolResult{
            {-1, "Tool execute exception: " + std::string(error.what())},
            {}
        };
    }
    catch (...)
    {
        return ToolResult{
            {-1, "Tool execute unknown exception"},
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
        status = {-4, "Tool release exception: " + std::string(error.what())};
    }
    catch (...)
    {
        status = {-4, "Tool release unknown exception"};
    }
    try
    {
        if (module_ && module_->api && module_->api->destroy)
        {
            module_->api->destroy(raw_);
        }
    }
    catch (...)
    {
    }
    raw_ = nullptr;
    return status;
}

} // namespace visionrt
