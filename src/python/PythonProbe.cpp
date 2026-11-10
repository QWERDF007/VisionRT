#include "python/PythonProbe.h"

#include <QDir>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QMetaObject>
#include <QPointer>
#include <QProcess>

#include <atomic>
#include <chrono>
#include <thread>
#include <utility>

namespace visionrt
{
namespace
{
// Asked of the interpreter itself, so every fact below is reported by the environment that will
// actually run the tools. Isolated mode (-I) keeps the user site directory, PYTHON* environment
// variables and the working directory out of sys.path; bytecode writing is off because the selected
// environment is read-only for this product.
constexpr const char *kProbeScript = R"PY(
import json, sys, sysconfig

def dll_path():
    import ctypes
    handle = getattr(sys, "dllhandle", 0)
    if not handle:
        return ""
    buffer = ctypes.create_unicode_buffer(32768)
    length = ctypes.windll.kernel32.GetModuleFileNameW(ctypes.c_void_p(handle), buffer, 32768)
    return buffer.value if length else ""

info = {
    "implementation": sys.implementation.name,
    "major": sys.version_info[0],
    "minor": sys.version_info[1],
    "micro": sys.version_info[2],
    "architecture": 64 if sys.maxsize > 2 ** 32 else 32,
    "executable": sys.executable,
    "prefix": sys.prefix,
    "base_prefix": sys.base_prefix,
    "path": list(sys.path),
    "debug": hasattr(sys, "gettotalrefcount"),
    "free_threaded": bool(sysconfig.get_config_var("Py_GIL_DISABLED")),
    "dll": dll_path() if sys.platform == "win32" else "",
    "numpy": "",
    "numpy_file": "",
    "numpy_error": "",
}
try:
    import numpy
    info["numpy"] = getattr(numpy, "__version__", "")
    info["numpy_file"] = getattr(numpy, "__file__", "") or ""
except Exception as error:
    info["numpy_error"] = "%s: %s" % (type(error).__name__, error)
sys.stdout.write(json.dumps(info, ensure_ascii=True))
)PY";

// One check has one bound: an interpreter that does not answer is killed and reported, never
// retried and never followed by a second candidate.
constexpr int kStartBudgetMs  = 10000;
constexpr int kFinishBudgetMs = 30000;
constexpr int kCancelBudgetMs = 3000;
constexpr int kPollSliceMs    = 100;

QString failure(const QString &executable, const QString &reason)
{
    return QStringLiteral("所选 Python 环境不可用（%1）：%2").arg(QDir::toNativeSeparators(executable), reason);
}

std::string pathText(const std::filesystem::path &path)
{
    const auto text = path.generic_u8string();
    return {reinterpret_cast<const char *>(text.data()), text.size()};
}

PythonEnvironmentStatus makeStatus(PythonEnvironmentPhase phase, const QString &selection, QString reason)
{
    PythonEnvironmentStatus status;
    status.phase    = phase;
    status.selected = selection.toStdString();
    status.reason   = reason.toStdString();
    return status;
}

// Blocks the calling thread only. The GUI thread reaches this function exclusively through
// PythonProbe, which owns the worker; a caller that wants the answer inline is a test.
std::optional<PythonEnvironment> runProbe(const QString &executable, const std::atomic<bool> *cancel,
                                          QString *error)
{
    if (executable.isEmpty())
    {
        if (error)
            *error = QStringLiteral("未选择 Python 解释器");
        return std::nullopt;
    }

    QProcess process;
    process.setProgram(executable);
    process.setArguments({QStringLiteral("-I"), QStringLiteral("-B"), QStringLiteral("-c"),
                          QString::fromUtf8(kProbeScript)});
    process.setProcessChannelMode(QProcess::SeparateChannels);
    process.start();

    QByteArray standardOutput;
    QByteArray standardError;
    const auto drain = [&]
    {
        standardOutput += process.readAllStandardOutput();
        standardError += process.readAllStandardError();
    };
    const auto cancelled = [cancel] { return cancel && cancel->load(std::memory_order_relaxed); };
    // The check is interrupted in slices so closing the window does not wait out a hung interpreter.
    const auto giveUp = [&](const QString &reason)
    {
        process.kill();
        process.waitForFinished(kCancelBudgetMs);
        drain();
        if (error)
            *error = failure(executable, reason);
        return std::nullopt;
    };

    bool       started       = false;
    const auto startDeadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(kStartBudgetMs);
    while (!(started = process.waitForStarted(kPollSliceMs)))
    {
        drain();
        if (cancelled())
            return giveUp(QStringLiteral("检查已取消"));
        if (process.state() == QProcess::NotRunning)
            break;
        if (std::chrono::steady_clock::now() >= startDeadline)
            return giveUp(QStringLiteral("启动超时"));
    }
    if (!started)
    {
        drain();
        if (error)
            *error = failure(executable, process.errorString());
        return std::nullopt;
    }

    const auto finishDeadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(kFinishBudgetMs);
    while (!process.waitForFinished(kPollSliceMs))
    {
        drain();
        if (cancelled())
            return giveUp(QStringLiteral("检查已取消"));
        if (std::chrono::steady_clock::now() >= finishDeadline)
            return giveUp(QStringLiteral("探测超时"));
    }
    drain();

    if (process.exitStatus() != QProcess::NormalExit || process.exitCode() != 0)
    {
        const auto detail = QString::fromUtf8(standardError).trimmed();
        if (error)
            *error = failure(executable, detail.isEmpty()
                                              ? QStringLiteral("进程退出码 %1").arg(process.exitCode())
                                              : detail);
        return std::nullopt;
    }

    QJsonParseError parse{};
    const auto      document = QJsonDocument::fromJson(standardOutput, &parse);
    if (!document.isObject())
    {
        if (error)
            *error = failure(executable, QStringLiteral("没有返回有效的环境信息"));
        return std::nullopt;
    }
    const auto info = document.object();

    if (info.value(QStringLiteral("implementation")).toString() != QStringLiteral("cpython"))
    {
        if (error)
            *error = failure(executable, QStringLiteral("只支持 CPython"));
        return std::nullopt;
    }
    const auto major = info.value(QStringLiteral("major")).toInt();
    const auto minor = info.value(QStringLiteral("minor")).toInt();
    if (major != VISIONRT_PYTHON_BUILD_MAJOR || minor != VISIONRT_PYTHON_BUILD_MINOR)
    {
        if (error)
            *error = failure(executable,
                             QStringLiteral("Python %1.%2 与本构建不兼容，需要 %3.%4")
                                 .arg(major)
                                 .arg(minor)
                                 .arg(VISIONRT_PYTHON_BUILD_MAJOR)
                                 .arg(VISIONRT_PYTHON_BUILD_MINOR));
        return std::nullopt;
    }
    if (info.value(QStringLiteral("architecture")).toInt() != 64)
    {
        if (error)
            *error = failure(executable, QStringLiteral("只支持 64 位 CPython"));
        return std::nullopt;
    }
    if (info.value(QStringLiteral("debug")).toBool())
    {
        if (error)
            *error = failure(executable, QStringLiteral("不支持调试版 CPython"));
        return std::nullopt;
    }
    if (info.value(QStringLiteral("free_threaded")).toBool())
    {
        if (error)
            *error = failure(executable, QStringLiteral("不支持自由线程 CPython"));
        return std::nullopt;
    }

    PythonEnvironment environment;
    environment.version = QStringLiteral("%1.%2.%3")
                              .arg(major)
                              .arg(minor)
                              .arg(info.value(QStringLiteral("micro")).toInt())
                              .toStdString();
    environment.executable
        = QDir::cleanPath(info.value(QStringLiteral("executable")).toString()).toStdWString();
    environment.prefix     = QDir::cleanPath(info.value(QStringLiteral("prefix")).toString()).toStdWString();
    environment.basePrefix = QDir::cleanPath(info.value(QStringLiteral("base_prefix")).toString()).toStdWString();
    environment.dll        = QDir::cleanPath(info.value(QStringLiteral("dll")).toString()).toStdWString();
    environment.numpyVersion = info.value(QStringLiteral("numpy")).toString().toStdString();
    environment.numpyFile    = info.value(QStringLiteral("numpy_file")).toString().toStdString();
    for (const auto &entry : info.value(QStringLiteral("path")).toArray())
        environment.searchPaths.push_back(entry.toString().toStdWString());

    if (environment.dll.empty())
    {
        if (error)
            *error = failure(executable, QStringLiteral("无法定位 CPython 运行时 DLL"));
        return std::nullopt;
    }
    if (environment.numpyVersion.empty())
    {
        const auto reason = info.value(QStringLiteral("numpy_error")).toString();
        if (error)
            *error = failure(executable, QStringLiteral("缺少 NumPy：%1").arg(reason));
        return std::nullopt;
    }
    return environment;
}
} // namespace

const char *pythonEnvironmentPhaseKey(PythonEnvironmentPhase phase) noexcept
{
    switch (phase)
    {
    case PythonEnvironmentPhase::Unconfigured:
        return "unconfigured";
    case PythonEnvironmentPhase::Probing:
        return "probing";
    case PythonEnvironmentPhase::Ready:
        return "ready";
    case PythonEnvironmentPhase::Invalid:
        return "invalid";
    case PythonEnvironmentPhase::BackendFailed:
        return "backendFailed";
    }
    return "unconfigured";
}

QString resolvePythonExecutable(const QString &selection, QString *error)
{
    const auto trimmed = selection.trimmed();
    if (trimmed.isEmpty())
        return {};
    const QFileInfo info(trimmed);
    if (info.isFile())
        return QDir::cleanPath(info.absoluteFilePath());
    if (!info.isDir())
    {
        if (error)
            *error = QStringLiteral("路径不存在：%1").arg(QDir::toNativeSeparators(trimmed));
        return {};
    }
    // Only the layouts a Python installation actually creates are accepted. A directory with two of
    // them is ambiguous, and guessing would silently run a different interpreter than the one the
    // user pointed at.
    const auto direct = QFileInfo(info.absoluteFilePath() + QStringLiteral("/python.exe"));
    const auto venv   = QFileInfo(info.absoluteFilePath() + QStringLiteral("/Scripts/python.exe"));
    if (direct.isFile() && venv.isFile())
    {
        if (error)
            *error = QStringLiteral("目录同时包含 python.exe 与 Scripts/python.exe，请直接选择解释器文件");
        return {};
    }
    if (direct.isFile() || venv.isFile())
        return QDir::cleanPath((direct.isFile() ? direct : venv).absoluteFilePath());
    if (error)
        *error = QStringLiteral("目录中没有 python.exe：%1").arg(QDir::toNativeSeparators(trimmed));
    return {};
}

std::optional<PythonEnvironment> probePythonEnvironment(const QString &executable, QString *error)
{
    return runProbe(executable, nullptr, error);
}

struct PythonProbe::Impl
{
    std::thread       worker;
    std::atomic<bool> active{false};
    std::atomic<bool> cancelled{false};
    PythonProbeResult result;
};

PythonProbe::PythonProbe(QObject *parent) : QObject(parent), impl_(std::make_unique<Impl>()) {}

PythonProbe::~PythonProbe()
{
    impl_->cancelled.store(true);
    if (impl_->worker.joinable())
        impl_->worker.join();
}

bool PythonProbe::start(const QString &selection)
{
    if (impl_->active.load())
        return false;
    // A finished worker is reaped here: the previous check already delivered its result.
    if (impl_->worker.joinable())
        impl_->worker.join();

    impl_->cancelled.store(false);
    impl_->result = {makeStatus(PythonEnvironmentPhase::Probing, selection,
                                QStringLiteral("正在检查 Python 环境")),
                     std::nullopt};
    impl_->active.store(true);

    QPointer<PythonProbe> guard(this);
    impl_->worker = std::thread(
        [this, guard, selection]
        {
            PythonProbeResult outcome;
            QString           error;
            const auto        executable = resolvePythonExecutable(selection, &error);
            if (selection.trimmed().isEmpty())
            {
                outcome.status = makeStatus(PythonEnvironmentPhase::Unconfigured, selection,
                                            QStringLiteral("未配置 Python 环境"));
            }
            else if (!error.isEmpty())
            {
                outcome.status = makeStatus(PythonEnvironmentPhase::Invalid, selection, error);
            }
            else if (auto environment = runProbe(executable, &impl_->cancelled, &error))
            {
                outcome.status            = makeStatus(PythonEnvironmentPhase::Ready, selection, {});
                outcome.status.executable = pathText(environment->executable);
                outcome.status.version    = environment->version;
                outcome.status.numpyVersion = environment->numpyVersion;
                outcome.status.numpyFile    = environment->numpyFile;
                outcome.environment         = std::move(environment);
            }
            else
            {
                outcome.status = makeStatus(PythonEnvironmentPhase::Invalid, selection, error);
            }
            // The result is published on the object's thread; the worker never touches its state.
            QMetaObject::invokeMethod(
                this,
                [guard, outcome = std::move(outcome)]() mutable
                {
                    if (guard)
                        guard->finish(std::move(outcome));
                },
                Qt::QueuedConnection);
        });
    return true;
}

bool PythonProbe::running() const noexcept
{
    return impl_->active.load();
}

PythonProbeResult PythonProbe::result() const
{
    return impl_->result;
}

void PythonProbe::finish(PythonProbeResult outcome)
{
    impl_->result = std::move(outcome);
    impl_->active.store(false);
    emit finished();
}

} // namespace visionrt
