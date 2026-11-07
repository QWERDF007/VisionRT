#include "python/PythonBackend.h"

#include "core/Error.h"
#include "python/PythonBridgeApi.h"

#include <QCoreApplication>
#include <QDir>
#include <QFileInfo>
#include <QStringList>

#include <array>
#include <new>
#include <type_traits>
#include <vector>

#if defined(_WIN32)
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#endif

namespace visionrt
{
namespace
{
constexpr const wchar_t *kBridgeFileName = L"visionrt_python_bridge.dll";
constexpr std::size_t    kErrorSize      = 1024;

QString bridgePath()
{
    const auto directory = QCoreApplication::applicationDirPath();
    return directory.isEmpty() ? QString{} : QDir(directory).filePath(QString::fromWCharArray(kBridgeFileName));
}
} // namespace

struct PythonBackend::Api
{
    decltype(&vrt_python_create)           create           = nullptr;
    decltype(&vrt_python_destroy)          destroy          = nullptr;
    decltype(&vrt_python_refresh)          refresh          = nullptr;
    decltype(&vrt_python_factory)          factory          = nullptr;
    decltype(&vrt_python_destroy_factory)  destroyFactory   = nullptr;
    decltype(&vrt_python_prepare_shutdown) prepareShutdown = nullptr;
    decltype(&vrt_python_active_references) activeReferences = nullptr;
    decltype(&vrt_python_close_ready)      closeReady       = nullptr;
};

struct PythonBackend::Impl
{
    Api         api;
    void       *handle  = nullptr;
    HMODULE     module  = nullptr;
    std::wstring dllPath;
    QString     reason  = QStringLiteral("未配置 Python 环境");
};

PythonBackend::PythonBackend() : impl_(std::make_unique<Impl>()) {}

PythonBackend::~PythonBackend()
{
    stop();
    releaseModule();
}

bool PythonBackend::running() const noexcept
{
    return impl_ && impl_->handle != nullptr;
}

QString PythonBackend::unavailableReason() const
{
    return impl_ ? impl_->reason : QString();
}

bool PythonBackend::loadModule(QString *error)
{
    if (impl_->module)
        return true;
    const auto path = bridgePath();
    if (path.isEmpty())
    {
        impl_->reason = QStringLiteral("无法确定程序目录，Python 桥接组件不可用");
        if (error)
            *error = impl_->reason;
        return false;
    }
    if (!QFileInfo::exists(path))
    {
        impl_->reason = QStringLiteral("缺少 Python 桥接组件：%1").arg(QDir::toNativeSeparators(path));
        if (error)
            *error = impl_->reason;
        return false;
    }
    const auto module = LoadLibraryW(reinterpret_cast<LPCWSTR>(path.utf16()));
    if (!module)
    {
        impl_->reason = QStringLiteral("无法加载 Python 桥接组件（错误码 %1）：%2")
                            .arg(GetLastError())
                            .arg(QDir::toNativeSeparators(path));
        if (error)
            *error = impl_->reason;
        return false;
    }
    const auto resolve = [module](const char *name) { return GetProcAddress(module, name); };
    impl_->api.create            = reinterpret_cast<decltype(impl_->api.create)>(resolve("vrt_python_create"));
    impl_->api.destroy           = reinterpret_cast<decltype(impl_->api.destroy)>(resolve("vrt_python_destroy"));
    impl_->api.refresh           = reinterpret_cast<decltype(impl_->api.refresh)>(resolve("vrt_python_refresh"));
    impl_->api.factory           = reinterpret_cast<decltype(impl_->api.factory)>(resolve("vrt_python_factory"));
    impl_->api.destroyFactory
        = reinterpret_cast<decltype(impl_->api.destroyFactory)>(resolve("vrt_python_destroy_factory"));
    impl_->api.prepareShutdown
        = reinterpret_cast<decltype(impl_->api.prepareShutdown)>(resolve("vrt_python_prepare_shutdown"));
    impl_->api.activeReferences
        = reinterpret_cast<decltype(impl_->api.activeReferences)>(resolve("vrt_python_active_references"));
    impl_->api.closeReady = reinterpret_cast<decltype(impl_->api.closeReady)>(resolve("vrt_python_close_ready"));
    if (!impl_->api.create || !impl_->api.destroy || !impl_->api.refresh || !impl_->api.factory
        || !impl_->api.destroyFactory || !impl_->api.prepareShutdown || !impl_->api.activeReferences
        || !impl_->api.closeReady)
    {
        FreeLibrary(module);
        impl_->reason = QStringLiteral("Python 桥接组件不完整，请重新安装");
        if (error)
            *error = impl_->reason;
        return false;
    }
    impl_->module = module;
    return true;
}

void PythonBackend::releaseModule()
{
    if (!impl_ || !impl_->module)
        return;
    FreeLibrary(impl_->module);
    impl_->module = nullptr;
    impl_->api    = {};
}

bool PythonBackend::start(const PythonEnvironment &environment, QString *error)
{
    if (running())
        return true;
    if (environment.executable.empty())
    {
        impl_->reason = QStringLiteral("未配置 Python 环境");
        if (error)
            *error = impl_->reason;
        return false;
    }

#if defined(_WIN32)
    // The selected environment owns its runtime libraries. Its directories are added to this
    // process's search path explicitly; PATH and the registry are never consulted. This runs before
    // the bridge is loaded, because the bridge imports CPython and must resolve it to the selected
    // build, not to a copy that happens to sit next to the executable.
    SetDefaultDllDirectories(LOAD_LIBRARY_SEARCH_DEFAULT_DIRS);
    QStringList runtimeDirectories;
    runtimeDirectories << QString::fromStdWString(environment.dll.parent_path().wstring());
    for (const auto &root : {environment.prefix, environment.basePrefix})
    {
        const auto base = QString::fromStdWString(root.wstring());
        runtimeDirectories << QDir(base).filePath(QStringLiteral("DLLs"))
                           << QDir(base).filePath(QStringLiteral("Library/bin"));
    }
    for (const auto &directory : runtimeDirectories)
        if (QFileInfo::exists(directory))
            AddDllDirectory(reinterpret_cast<LPCWSTR>(QDir::toNativeSeparators(directory).utf16()));

    // Preload the exact CPython build the probe reported, so the bridge's import of the same file
    // name binds to this module.
    const auto dllPath = environment.dll.wstring();
    const auto python  = LoadLibraryExW(dllPath.c_str(), nullptr,
                                        LOAD_LIBRARY_SEARCH_DEFAULT_DIRS | LOAD_LIBRARY_SEARCH_DLL_LOAD_DIR);
    if (!python)
    {
        impl_->reason = QStringLiteral("无法加载 CPython 运行时（错误码 %1）：%2")
                            .arg(GetLastError())
                            .arg(QDir::toNativeSeparators(QString::fromStdWString(dllPath)));
        if (error)
            *error = impl_->reason;
        return false;
    }
#endif

    if (!loadModule(error))
        return false;

#if defined(_WIN32)
    // The bridge imports CPython by name. If the loader resolved that import to another copy, the
    // process would run two runtimes and the selected environment would not be the one in use.
    if (GetModuleHandleW(environment.dll.filename().c_str()) != python)
    {
        impl_->reason = QStringLiteral("进程中已加载其他版本的 CPython，无法使用选定环境：%1")
                            .arg(QDir::toNativeSeparators(QString::fromStdWString(dllPath)));
        releaseModule();
        if (error)
            *error = impl_->reason;
        return false;
    }
#endif

    std::vector<std::wstring> searchPaths;
    searchPaths.reserve(environment.searchPaths.size());
    std::vector<const wchar_t *> searchPointers;
    searchPointers.reserve(environment.searchPaths.size());
    for (const auto &entry : environment.searchPaths)
        searchPaths.push_back(entry.wstring());
    for (const auto &entry : searchPaths)
        searchPointers.push_back(entry.c_str());

    const auto executable = environment.executable.wstring();
    const auto prefix     = environment.prefix.wstring();
    const auto basePrefix = environment.basePrefix.wstring();
    const auto dll        = environment.dll.wstring();
    const VrtPythonPaths paths{executable.c_str(), prefix.c_str(), basePrefix.c_str(), dll.c_str(),
                               searchPointers.data(), searchPointers.size()};

    std::array<char, kErrorSize> message{};
    impl_->handle = impl_->api.create(&paths, message.data(), message.size());
    if (!impl_->handle)
    {
        impl_->reason = QString::fromUtf8(message.data());
        if (impl_->reason.isEmpty())
            impl_->reason = QStringLiteral("所选 Python 环境无法启动");
        if (error)
            *error = impl_->reason;
        return false;
    }
    impl_->reason.clear();
    return true;
}

void PythonBackend::stop()
{
    if (!running())
        return;
    if (impl_->api.prepareShutdown)
        impl_->api.prepareShutdown(impl_->handle);
    impl_->api.destroy(impl_->handle);
    impl_->handle = nullptr;
    impl_->reason = QStringLiteral("未配置 Python 环境");
}

void PythonBackend::prepareShutdown()
{
    if (running())
        impl_->api.prepareShutdown(impl_->handle);
}

std::vector<PythonImplementation> PythonBackend::refresh(std::span<const ToolDefinition> definitions)
{
    if (!running())
    {
        std::vector<PythonImplementation> statuses;
        for (const auto &definition : definitions)
            if (definition.kind == ToolKind::PythonFunction || definition.kind == ToolKind::PythonClass)
                statuses.push_back({definition.nameEn,
                                    makeStatus(ErrorCode::PythonUnavailable, impl_->reason.toStdString())});
        return statuses;
    }
    std::vector<PythonImplementation> statuses(definitions.size());
    std::size_t                       written = 0;
    impl_->api.refresh(impl_->handle, definitions.data(), definitions.size(), statuses.data(),
                       statuses.size(), &written);
    statuses.resize(std::min(written, statuses.size()));
    return statuses;
}

ToolFactory PythonBackend::factory(const ToolDefinition &definition) const
{
    if (!running())
        return {};
    std::aligned_storage_t<sizeof(ToolFactory), alignof(ToolFactory)> storage;
    impl_->api.factory(impl_->handle, &definition, &storage);
    auto *factory = std::launder(reinterpret_cast<ToolFactory *>(&storage));
    ToolFactory result = std::move(*factory);
    impl_->api.destroyFactory(factory);
    return result;
}

bool PythonBackend::isCloseReady() const noexcept
{
    return !running() || impl_->api.closeReady(impl_->handle) != 0;
}

std::size_t PythonBackend::activeReferences() const noexcept
{
    return running() ? impl_->api.activeReferences(impl_->handle) : 0;
}

} // namespace visionrt
