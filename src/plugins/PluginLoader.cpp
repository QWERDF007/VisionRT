#include "plugins/PluginLoader.h"
#include "visionrt/sdk/SchemaYaml.h"

#include <set>
#include <yaml-cpp/yaml.h>

#ifdef _WIN32
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#else
#include <QLibrary>
#endif

namespace visionrt {

void PluginLoader::registerModule(std::string registryName, std::filesystem::path libraryPath) {
    std::lock_guard lock(mutex_);
    index_[std::move(registryName)] = std::move(libraryPath);
}

void PluginLoader::setBaseDirectory(std::filesystem::path baseDirectory) {
    std::lock_guard lock(mutex_);
    baseDirectory_ = std::move(baseDirectory);
}

void PluginLoader::loadIndexFile(const std::filesystem::path& indexFilePath) {
    std::lock_guard lock(mutex_);
    const auto fname = indexFilePath.filename().string();
    if (fname != "module_index.yml") {
        diagnostics_.push_back({indexFilePath, "Unsupported module index file name: only module_index.yml is supported: " + indexFilePath.string()});
        return;
    }
    if (!std::filesystem::exists(indexFilePath)) {
        diagnostics_.push_back({indexFilePath, "Module index file does not exist: " + indexFilePath.string()});
        return;
    }
    if (baseDirectory_.empty()) {
        baseDirectory_ = indexFilePath.parent_path().lexically_normal();
    }

    visionrt::ModuleIndex parsedIndex;
    try {
        parsedIndex = visionrt::parseModuleIndexFile(indexFilePath);
    } catch (const std::exception& error) {
        diagnostics_.push_back({indexFilePath, "Failed to parse module index YAML: " + std::string(error.what())});
        return;
    }

    for (const auto& item : parsedIndex) {
        if (index_.contains(item.registryName)) {
            diagnostics_.push_back({indexFilePath, "Duplicate tool registry_name across loaded module indices: " + item.registryName});
            return;
        }
    }

    for (const auto& item : parsedIndex) {
        index_.emplace(item.registryName, std::filesystem::path(std::u8string(reinterpret_cast<const char8_t*>(item.path.data()), item.path.size())));
    }
}

std::filesystem::path PluginLoader::resolveModulePath(std::string_view registryName) const {
    std::lock_guard lock(mutex_);
    const auto it = index_.find(registryName);
    std::filesystem::path path;
    if (it != index_.end()) {
        path = it->second;
    } else {
        return {};
    }
    if (path.is_relative() && !baseDirectory_.empty()) {
        path = (baseDirectory_ / path).lexically_normal();
    }
    return path;
}

bool PluginLoader::hasModule(std::string_view registryName) const {
    return !resolveModulePath(registryName).empty();
}

std::shared_ptr<const LoadedModule> PluginLoader::loadModule(const std::filesystem::path& libraryPath, ToolStatus& status) {
    if (libraryPath.empty()) {
        status = {-3, "Module library path is empty"};
        return nullptr;
    }

    std::lock_guard lock(mutex_);
    const auto it = loadedModules_.find(libraryPath);
    if (it != loadedModules_.end()) {
        status = {};
        return it->second;
    }

    if (!std::filesystem::exists(libraryPath)) {
        status = {-3, "Module library file not found: " + libraryPath.string()};
        return nullptr;
    }

#ifdef _WIN32
    const auto absPath = std::filesystem::absolute(libraryPath).lexically_normal();
    HMODULE handle = LoadLibraryExW(absPath.wstring().c_str(), nullptr, LOAD_WITH_ALTERED_SEARCH_PATH);
    if (!handle) {
        const DWORD err = GetLastError();
        status = {-3, "Failed to load dynamic library " + libraryPath.string() + " (Win32 error: " + std::to_string(err) + ")"};
        return nullptr;
    }

    auto proc = GetProcAddress(handle, "visiontoolsModuleApi");
    if (!proc) {
        FreeLibrary(handle);
        status = {-3, "Entry symbol 'visiontoolsModuleApi' not found in " + libraryPath.string()};
        return nullptr;
    }

    auto apiFunc = reinterpret_cast<const ModuleApi* (VT_CALL *)() noexcept>(proc);
    const auto* api = apiFunc();
    if (!api) {
        FreeLibrary(handle);
        status = {-3, "visiontoolsModuleApi() returned null in " + libraryPath.string()};
        return nullptr;
    }

    if (api->abi_major != MODULE_ABI_MAJOR) {
        FreeLibrary(handle);
        status = {-3, "Module ABI major mismatch (expected " + std::to_string(MODULE_ABI_MAJOR) +
                      ", got " + std::to_string(api->abi_major) + ")"};
        return nullptr;
    }

    if (api->struct_bytes < sizeof(ModuleApi)) {
        FreeLibrary(handle);
        status = {-3, "ModuleApi struct size mismatch"};
        return nullptr;
    }

    if (!api->create || !api->destroy) {
        FreeLibrary(handle);
        status = {-3, "ModuleApi function pointers are incomplete"};
        return nullptr;
    }

    auto loaded = std::make_shared<LoadedModule>();
    loaded->path = libraryPath;
    loaded->handle = handle;
    loaded->api = api;
    loadedModules_.emplace(libraryPath, loaded);
    status = {};
    return loaded;
#else
    auto lib = std::make_shared<QLibrary>(QString::fromStdString(libraryPath.string()));
    const bool loadedOk = lib->load();
    if (!loadedOk) {
        status = {-3, "Failed to load dynamic library " + libraryPath.string() + ": " + lib->errorString().toStdString()};
        return nullptr;
    }

    auto apiFunc = reinterpret_cast<const ModuleApi* (VT_CALL *)() noexcept>(lib->resolve("visiontoolsModuleApi"));
    if (!apiFunc) {
        status = {-3, "Entry symbol 'visiontoolsModuleApi' not found in " + libraryPath.string()};
        return nullptr;
    }

    const auto* api = apiFunc();
    if (!api) {
        status = {-3, "visiontoolsModuleApi() returned null in " + libraryPath.string()};
        return nullptr;
    }

    if (api->abi_major != MODULE_ABI_MAJOR) {
        status = {-3, "Module ABI major mismatch (expected " + std::to_string(MODULE_ABI_MAJOR) +
                      ", got " + std::to_string(api->abi_major) + ")"};
        return nullptr;
    }

    if (api->struct_bytes < sizeof(ModuleApi)) {
        status = {-3, "ModuleApi struct size mismatch"};
        return nullptr;
    }

    if (!api->create || !api->destroy) {
        status = {-3, "ModuleApi function pointers are incomplete"};
        return nullptr;
    }

    auto loaded = std::make_shared<LoadedModule>();
    loaded->path = libraryPath;
    loaded->library = lib;
    loaded->api = api;
    loadedModules_.emplace(libraryPath, loaded);
    status = {};
    return loaded;
#endif
}

std::unique_ptr<IToolInstance> PluginLoader::createAdapter(std::string_view registryName, const PlatformToolServices& services) {
    const auto path = resolveModulePath(registryName);
    return std::make_unique<DomainToolAdapter>(shared_from_this(), path, std::string(registryName), services);
}

std::size_t PluginLoader::loadedModuleCount() const {
    std::lock_guard lock(mutex_);
    return loadedModules_.size();
}

DomainToolAdapter::DomainToolAdapter(std::shared_ptr<PluginLoader> loader,
                                     std::filesystem::path libraryPath,
                                     std::string registryName,
                                     PlatformToolServices services)
    : loader_(std::move(loader)),
      libraryPath_(std::move(libraryPath)),
      registryName_(std::move(registryName)),
      services_(std::move(services)) {}

DomainToolAdapter::~DomainToolAdapter() {
    if (raw_) {
        try { raw_->release(); } catch (...) {}
        try {
            if (module_ && module_->api && module_->api->destroy) {
                module_->api->destroy(raw_);
            }
        } catch (...) {}
        raw_ = nullptr;
    }
}

ToolStatus DomainToolAdapter::initialize(const Values& initParams) {
    if (failureStatus_.code != 0) return failureStatus_;

    if (raw_) {
        try { raw_->release(); } catch (...) {}
        if (module_ && module_->api && module_->api->destroy) {
            module_->api->destroy(raw_);
        }
        raw_ = nullptr;
    }

    ToolStatus status;
    module_ = loader_->loadModule(libraryPath_, status);
    if (!module_ || !module_->api || status.code != 0) {
        failureStatus_ = (status.code != 0 ? status : ToolStatus{-3, "Module load failed: " + libraryPath_.string()});
        return failureStatus_;
    }

    ToolStatus createStatus;
    raw_ = module_->api->create(registryName_.c_str(), &services_, &createStatus);
    if (!raw_) {
        failureStatus_ = (createStatus.code != 0 ? createStatus : ToolStatus{-3, "Module create returned null instance: " + registryName_});
        return failureStatus_;
    }

    if (createStatus.code != 0) {
        try { module_->api->destroy(raw_); } catch (...) {}
        raw_ = nullptr;
        failureStatus_ = createStatus;
        return failureStatus_;
    }

    try {
        status = raw_->initialize(initParams);
    } catch (const std::exception& error) {
        status = {-1, "Tool initialization exception: " + std::string(error.what())};
    } catch (...) {
        status = {-1, "Tool initialization unknown exception"};
    }

    if (!status) {
        try { raw_->release(); } catch (...) {}
        try { module_->api->destroy(raw_); } catch (...) {}
        raw_ = nullptr;
        failureStatus_ = status;
        return failureStatus_;
    }

    return status;
}

ToolResult DomainToolAdapter::execute(const Values& inputs) {
    if (!raw_) {
        if (failureStatus_.code == 0) {
            auto s = initialize({});
            if (s.code != 0) {
                return ToolResult{s, {}};
            }
        }
    }
    if (!raw_) {
        const auto s = (failureStatus_.code != 0 ? failureStatus_ : ToolStatus{-3, "Tool instance unavailable: " + registryName_});
        return ToolResult{s, {}};
    }
    try {
        return raw_->execute(inputs);
    } catch (const std::exception& error) {
        return ToolResult{{-1, "Tool execute exception: " + std::string(error.what())}, {}};
    } catch (...) {
        return ToolResult{{-1, "Tool execute unknown exception"}, {}};
    }
}

ToolStatus DomainToolAdapter::release() {
    if (!raw_) return {};
    ToolStatus status;
    try {
        status = raw_->release();
    } catch (const std::exception& error) {
        status = {-4, "Tool release exception: " + std::string(error.what())};
    } catch (...) {
        status = {-4, "Tool release unknown exception"};
    }
    try {
        if (module_ && module_->api && module_->api->destroy) {
            module_->api->destroy(raw_);
        }
    } catch (...) {}
    raw_ = nullptr;
    return status;
}

} // namespace visionrt
