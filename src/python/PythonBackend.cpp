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
#include <tlhelp32.h>
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

std::string notConfigured()
{
    return "未配置 Python 环境";
}

std::string utf8Text(const QString &text)
{
    const auto bytes = text.toUtf8();
    return {bytes.constData(), static_cast<std::size_t>(bytes.size())};
}

std::string pathText(const std::filesystem::path &path)
{
    const auto text = path.generic_u8string();
    return {reinterpret_cast<const char *>(text.data()), text.size()};
}

#if defined(_WIN32)
// Windows keeps a process-wide DLL search order and a directory cookie per added directory. Both are
// process resources: a failed start has to give them back, or the next start runs with a search path
// that names an environment nobody selected.
class DllDirectoryScope
{
public:
    DllDirectoryScope()                                   = default;
    ~DllDirectoryScope()                                  { rollback(); }
    DllDirectoryScope(const DllDirectoryScope &)            = delete;
    DllDirectoryScope &operator=(const DllDirectoryScope &) = delete;

    void add(const QString &directory)
    {
        const auto native = QDir::toNativeSeparators(directory);
        if (const auto cookie = AddDllDirectory(reinterpret_cast<LPCWSTR>(native.utf16())))
            cookies_.push_back(cookie);
    }

    // The interpreter serves tools for the rest of the process life; its directories stay reachable
    // after this scope ends so unloading and reloading the bridge cannot lose its dependencies.
    void commit() noexcept
    {
        cookies_.clear();
    }

    void rollback() noexcept
    {
        for (const auto cookie : cookies_)
            RemoveDllDirectory(cookie);
        cookies_.clear();
    }

private:
    std::vector<DLL_DIRECTORY_COOKIE> cookies_;
};

// A module handle is either handed to the interpreter or released. Nothing keeps an unused CPython
// build resident, which would make the "already loaded another version" check fire on a retry.
class ModuleHandle
{
public:
    explicit ModuleHandle(HMODULE handle) noexcept : handle_(handle) {}
    ~ModuleHandle() { release(); }
    ModuleHandle(const ModuleHandle &)            = delete;
    ModuleHandle &operator=(const ModuleHandle &) = delete;

    HMODULE get() const noexcept
    {
        return handle_;
    }

    void release() noexcept
    {
        if (handle_)
            FreeLibrary(handle_);
        handle_ = nullptr;
    }

private:
    HMODULE handle_;
};
#endif
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
    Api                     api;
    void                   *handle = nullptr;
    HMODULE                 module = nullptr;
    std::wstring            dllPath;
    PythonEnvironmentStatus status;

    Impl()
    {
        status.reason = notConfigured();
    }
};

PythonBackend::PythonBackend() : impl_(std::make_unique<Impl>()) {}

PythonBackend::~PythonBackend()
{
    stop();
    releaseModule();
}

void PythonBackend::setEnvironmentStatus(PythonEnvironmentStatus status)
{
    impl_->status = std::move(status);
}

const PythonEnvironmentStatus &PythonBackend::environmentStatus() const noexcept
{
    return impl_->status;
}

bool PythonBackend::running() const noexcept
{
    return impl_->handle != nullptr;
}

QString PythonBackend::unavailableReason() const
{
    return QString::fromStdString(impl_->status.reason);
}

bool PythonBackend::loadModule(QString *error)
{
    if (impl_->module)
        return true;
    const auto path = bridgePath();
    const auto fail = [this, error](QString reason)
    {
        impl_->status.phase  = PythonEnvironmentPhase::BackendFailed;
        impl_->status.reason = utf8Text(reason);
        if (error)
            *error = reason;
        return false;
    };
    if (path.isEmpty())
        return fail(QStringLiteral("无法确定程序目录，Python 桥接组件不可用"));
    if (!QFileInfo::exists(path))
        return fail(QStringLiteral("缺少 Python 桥接组件：%1").arg(QDir::toNativeSeparators(path)));
    const auto module = LoadLibraryW(reinterpret_cast<LPCWSTR>(path.utf16()));
    if (!module)
        return fail(QStringLiteral("无法加载 Python 桥接组件（错误码 %1）：%2")
                        .arg(GetLastError())
                        .arg(QDir::toNativeSeparators(path)));
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
        return fail(QStringLiteral("Python 桥接组件不完整，请重新安装"));
    }
    impl_->module = module;
    return true;
}

void PythonBackend::releaseModule()
{
    if (!impl_->module)
        return;
    FreeLibrary(impl_->module);
    impl_->module = nullptr;
    impl_->api    = {};
}

bool PythonBackend::start(const PythonEnvironment &environment, QString *error)
{
    if (running())
        return true;

    const auto fail = [this, error](QString reason)
    {
        impl_->status.phase  = PythonEnvironmentPhase::BackendFailed;
        impl_->status.reason = utf8Text(reason);
        if (error)
            *error = reason;
        return false;
    };

    if (environment.executable.empty())
    {
        impl_->status        = PythonEnvironmentStatus{};
        impl_->status.reason = notConfigured();
        if (error)
            *error = QString::fromStdString(impl_->status.reason);
        return false;
    }

#if defined(_WIN32)
    // The selected environment owns its runtime libraries. Its directories are added to this
    // process's search path explicitly; PATH and the registry are never consulted. This runs before
    // the bridge is loaded, because the bridge imports CPython and must resolve it to the selected
    // build, not to a copy that happens to sit next to the executable.
    DllDirectoryScope dllDirectories;
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
            dllDirectories.add(directory);

    // Preload the exact CPython build the probe reported, so the bridge's import of the same file
    // name binds to this module. The handle is released unless the interpreter takes over the
    // process, so a rejected environment leaves nothing loaded for the next attempt.
    const auto   dllPath = environment.dll.wstring();
    ModuleHandle python(LoadLibraryExW(dllPath.c_str(), nullptr,
                                       LOAD_LIBRARY_SEARCH_DEFAULT_DIRS | LOAD_LIBRARY_SEARCH_DLL_LOAD_DIR));
    if (!python.get())
        return fail(QStringLiteral("无法加载 CPython 运行时（错误码 %1）：%2")
                        .arg(GetLastError())
                        .arg(QDir::toNativeSeparators(QString::fromStdWString(dllPath))));
#endif

    if (!loadModule(error))
        return false;

#if defined(_WIN32)
    // The bridge imports CPython by name. If the loader resolved that import to another copy, the
    // process would run two runtimes and the selected environment would not be the one in use.
    if (GetModuleHandleW(environment.dll.filename().c_str()) != python.get())
    {
        releaseModule();
        return fail(QStringLiteral("进程中已加载其他版本的 CPython，无法使用选定环境：%1")
                        .arg(QDir::toNativeSeparators(QString::fromStdWString(dllPath))));
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
        releaseModule();
        const auto reason = QString::fromUtf8(message.data());
        return fail(reason.isEmpty() ? QStringLiteral("所选 Python 环境无法启动") : reason);
    }

    impl_->status          = PythonEnvironmentStatus{};
    impl_->status.phase    = PythonEnvironmentPhase::Ready;
    impl_->status.selected   = pathText(environment.executable);
    impl_->status.executable = impl_->status.selected;
    impl_->status.version    = environment.version;
    impl_->status.numpyVersion = environment.numpyVersion;
    impl_->status.numpyFile    = environment.numpyFile;
#if defined(_WIN32)
    dllDirectories.commit();
    python.release();
#endif
    return true;
}

void PythonBackend::stop()
{
    if (!running())
        return;
    if (impl_->api.prepareShutdown)
        impl_->api.prepareShutdown(impl_->handle);
    impl_->api.destroy(impl_->handle);
    impl_->handle         = nullptr;
    impl_->status        = PythonEnvironmentStatus{};
    impl_->status.reason = notConfigured();
}

void PythonBackend::prepareShutdown()
{
    if (running())
        impl_->api.prepareShutdown(impl_->handle);
}

std::vector<PythonImplementation> PythonBackend::refresh(std::span<const ToolDefinition> definitions,
                                                         const PythonEnvironmentStatus &status)
{
    if (!running())
    {
        std::vector<PythonImplementation> statuses;
        for (const auto &definition : definitions)
            if (definition.kind == ToolKind::PythonFunction || definition.kind == ToolKind::PythonClass)
                statuses.push_back({definition.nameEn,
                                    makeStatus(ErrorCode::PythonUnavailable, status.reason)});
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

PythonRuntimeFacts PythonBackend::runtimeFacts() const
{
    PythonRuntimeFacts facts;
#if defined(_WIN32)
    // The snapshot is taken from the loader's own table. Deriving it from the environment we asked
    // for would report the intention, not the process, and those two are exactly what has to agree.
    const auto snapshot = CreateToolhelp32Snapshot(TH32CS_SNAPMODULE, GetCurrentProcessId());
    if (snapshot == INVALID_HANDLE_VALUE)
        return facts;
    const auto lowerAscii = [](std::wstring text)
    {
        for (auto &character : text)
            if (character >= L'A' && character <= L'Z')
                character = static_cast<wchar_t>(character - L'A' + L'a');
        return text;
    };
    MODULEENTRY32W entry{};
    entry.dwSize = sizeof(entry);
    for (BOOL more = Module32FirstW(snapshot, &entry); more; more = Module32NextW(snapshot, &entry))
    {
        const std::wstring name(entry.szModule);
        const auto         key    = lowerAscii(name);
        // Only loadable modules count: an executable whose file name starts with "python" is a
        // caller, not an interpreter this process linked.
        const bool binary = key.size() > 4 && (key.substr(key.size() - 4) == L".dll" || key.substr(key.size() - 4) == L".pyd");
        const bool python = binary && key.rfind(L"python", 0) == 0;
        const bool numpy  = binary && key.rfind(L"numpy", 0) == 0;
        const bool bridge = key == L"visionrt_python_bridge.dll";
        if (!python && !numpy && !bridge)
            continue;
        facts.modules.emplace_back(utf8Text(QString::fromStdWString(name)),
                                   pathText(std::filesystem::path(std::wstring(entry.szExePath))));
        if (bridge)
            facts.bridge = true;
        // A CPython DLL is the interpreter itself. `python3.dll` is only the stable-ABI forwarder, so
        // the first real `python3<x>.dll` is the one worth naming.
        if (facts.runtime.empty() && key.rfind(L"python3", 0) == 0 && key != L"python3.dll"
            && key.size() > 4 && key.substr(key.size() - 4) == L".dll")
            facts.runtime = pathText(std::filesystem::path(std::wstring(entry.szExePath)));
    }
    CloseHandle(snapshot);
#endif
    return facts;
}

} // namespace visionrt
