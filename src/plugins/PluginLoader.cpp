#include "plugins/PluginLoader.h"

#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <yaml-cpp/yaml.h>

#ifdef _WIN32
#include <windows.h>
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
    if (!std::filesystem::exists(indexFilePath)) {
        diagnostics_.push_back({indexFilePath, "Module index file does not exist: " + indexFilePath.string()});
        return;
    }
    if (baseDirectory_.empty()) {
        baseDirectory_ = indexFilePath.parent_path().lexically_normal();
    }
    const auto ext = indexFilePath.extension().string();
    if (ext == ".json") {
        QFile file(QString::fromStdString(indexFilePath.string()));
        if (!file.open(QIODevice::ReadOnly)) {
            diagnostics_.push_back({indexFilePath, "Cannot open module index file: " + indexFilePath.string()});
            return;
        }
        QJsonParseError parseError;
        const auto doc = QJsonDocument::fromJson(file.readAll(), &parseError);
        if (parseError.error != QJsonParseError::NoError || !doc.isObject()) {
            diagnostics_.push_back({indexFilePath, "Failed to parse module index JSON: " + parseError.errorString().toStdString()});
            return;
        }
        const auto obj = doc.object();
        if (obj.contains("schema_version") && obj["schema_version"].toInt() != 1) {
            diagnostics_.push_back({indexFilePath, "Unsupported module index schema_version: " + std::to_string(obj["schema_version"].toInt())});
            return;
        }

        if (obj.contains("modules") && obj["modules"].isArray()) {
            const auto arr = obj["modules"].toArray();
            for (const auto& item : arr) {
                if (!item.isObject()) {
                    diagnostics_.push_back({indexFilePath, "Module item in array must be an object"});
                    continue;
                }
                const auto m = item.toObject();
                const auto name = m.value("registry_name").toString().toStdString();
                const auto pathStr = m.value("path").toString().toStdString();
                if (name.empty()) {
                    diagnostics_.push_back({indexFilePath, "Module registry_name must not be empty"});
                    continue;
                }
                if (pathStr.empty()) {
                    diagnostics_.push_back({indexFilePath, "Module path must not be empty for registry_name: " + name});
                    continue;
                }
                if (index_.contains(name)) {
                    diagnostics_.push_back({indexFilePath, "Duplicate registry_name in module index: " + name});
                    continue;
                }
                const auto relPath = std::filesystem::u8path(pathStr);
                if (relPath.is_absolute()) {
                    diagnostics_.push_back({indexFilePath, "Module path must be relative: " + pathStr});
                    continue;
                }
                auto norm = relPath.lexically_normal();
                if (!norm.empty() && norm.begin()->string() == "..") {
                    diagnostics_.push_back({indexFilePath, "Module path escapes package root: " + pathStr});
                    continue;
                }
                index_[name] = relPath;
            }
        } else if (obj.contains("modules") && obj["modules"].isObject()) {
            const auto mods = obj["modules"].toObject();
            for (auto it = mods.begin(); it != mods.end(); ++it) {
                const auto name = it.key().toStdString();
                const auto pathStr = it.value().toString().toStdString();
                if (name.empty() || pathStr.empty()) continue;
                if (index_.contains(name)) {
                    diagnostics_.push_back({indexFilePath, "Duplicate registry_name in module index: " + name});
                    continue;
                }
                const auto relPath = std::filesystem::u8path(pathStr);
                if (relPath.is_absolute()) {
                    diagnostics_.push_back({indexFilePath, "Module path must be relative: " + pathStr});
                    continue;
                }
                auto norm = relPath.lexically_normal();
                if (!norm.empty() && norm.begin()->string() == "..") {
                    diagnostics_.push_back({indexFilePath, "Module path escapes package root: " + pathStr});
                    continue;
                }
                index_[name] = relPath;
            }
        } else if (obj.contains("tools") && obj["tools"].isObject()) {
            const auto tools = obj["tools"].toObject();
            for (auto it = tools.begin(); it != tools.end(); ++it) {
                if (!it.value().isObject()) continue;
                const auto t = it.value().toObject();
                std::string name = it.key().toStdString();
                if (t.contains("entry") && t["entry"].isObject()) {
                    const auto entry = t["entry"].toObject();
                    if (entry.contains("registry_name") && !entry["registry_name"].toString().isEmpty()) {
                        name = entry["registry_name"].toString().toStdString();
                    }
                }
                const auto pathStr = t.value("module_dll").toString().toStdString();
                if (name.empty() || pathStr.empty()) continue;
                if (index_.contains(name)) {
                    continue;
                }
                const auto relPath = std::filesystem::u8path(pathStr);
                if (relPath.is_absolute()) {
                    diagnostics_.push_back({indexFilePath, "Module path must be relative: " + pathStr});
                    continue;
                }
                auto norm = relPath.lexically_normal();
                if (!norm.empty() && norm.begin()->string() == "..") {
                    diagnostics_.push_back({indexFilePath, "Module path escapes package root: " + pathStr});
                    continue;
                }
                index_[name] = relPath;
            }
        } else {
            diagnostics_.push_back({indexFilePath, "Module index must contain 'modules' or 'tools'"});
        }
    } else if (ext == ".yaml" || ext == ".yml") {
        try {
            const auto node = YAML::LoadFile(indexFilePath.string());
            if (!node.IsMap()) {
                diagnostics_.push_back({indexFilePath, "Module index YAML root must be a map"});
                return;
            }
            if (node["schema_version"] && node["schema_version"].as<int>() != 1) {
                diagnostics_.push_back({indexFilePath, "Unsupported module index schema_version: " + node["schema_version"].as<std::string>()});
                return;
            }
            if (node["modules"] && node["modules"].IsSequence()) {
                for (const auto& item : node["modules"]) {
                    if (!item.IsMap()) {
                        diagnostics_.push_back({indexFilePath, "Module item in YAML sequence must be a map"});
                        continue;
                    }
                    const auto name = item["registry_name"] ? item["registry_name"].as<std::string>() : "";
                    const auto pathStr = item["path"] ? item["path"].as<std::string>() : "";
                    if (name.empty()) {
                        diagnostics_.push_back({indexFilePath, "Module registry_name must not be empty"});
                        continue;
                    }
                    if (pathStr.empty()) {
                        diagnostics_.push_back({indexFilePath, "Module path must not be empty for registry_name: " + name});
                        continue;
                    }
                    if (index_.contains(name)) {
                        diagnostics_.push_back({indexFilePath, "Duplicate registry_name in module index: " + name});
                        continue;
                    }
                    const auto relPath = std::filesystem::u8path(pathStr);
                    if (relPath.is_absolute()) {
                        diagnostics_.push_back({indexFilePath, "Module path must be relative: " + pathStr});
                        continue;
                    }
                    auto norm = relPath.lexically_normal();
                    if (!norm.empty() && norm.begin()->string() == "..") {
                        diagnostics_.push_back({indexFilePath, "Module path escapes package root: " + pathStr});
                        continue;
                    }
                }
            } else if (node["tools"] && node["tools"].IsMap()) {
                for (const auto& it : node["tools"]) {
                    std::string name = it.first.as<std::string>();
                    const auto& val = it.second;
                    if (val["entry"] && val["entry"]["registry_name"]) {
                        name = val["entry"]["registry_name"].as<std::string>();
                    }
                    std::string pathStr;
                    if (val["module_dll"]) {
                        pathStr = val["module_dll"].as<std::string>();
                    } else if (val["path"]) {
                        pathStr = val["path"].as<std::string>();
                    }
                    if (name.empty() || pathStr.empty()) continue;
                    if (index_.contains(name)) continue;
                    const auto relPath = std::filesystem::u8path(pathStr);
                    if (relPath.is_absolute()) {
                        diagnostics_.push_back({indexFilePath, "Module path must be relative: " + pathStr});
                        continue;
                    }
                    auto norm = relPath.lexically_normal();
                    if (!norm.empty() && norm.begin()->string() == "..") {
                        diagnostics_.push_back({indexFilePath, "Module path escapes package root: " + pathStr});
                        continue;
                    }
                    index_[name] = relPath;
                }
            }
        } catch (const std::exception& error) {
            diagnostics_.push_back({indexFilePath, "Failed to parse module index YAML: " + std::string(error.what())});
        }
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
    SetDllDirectoryW(libraryPath.parent_path().c_str());
#endif
    auto lib = std::make_shared<QLibrary>(QString::fromStdString(libraryPath.string()));
    const bool loadedOk = lib->load();
#ifdef _WIN32
    SetDllDirectoryW(nullptr);
#endif
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
