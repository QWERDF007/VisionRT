#include "python/PythonProbe.h"

#include <QDir>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QProcess>

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
    "numpy_error": "",
}
try:
    import numpy
    info["numpy"] = getattr(numpy, "__version__", "")
except Exception as error:
    info["numpy_error"] = "%s: %s" % (type(error).__name__, error)
sys.stdout.write(json.dumps(info, ensure_ascii=True))
)PY";

QString failure(const QString &executable, const QString &reason)
{
    return QStringLiteral("所选 Python 环境不可用（%1）：%2").arg(QDir::toNativeSeparators(executable), reason);
}
} // namespace

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

std::optional<PythonProbeResult> probePythonEnvironment(const QString &executable, QString *error)
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
    if (!process.waitForStarted(10000))
    {
        if (error)
            *error = failure(executable, process.errorString());
        return std::nullopt;
    }
    if (!process.waitForFinished(30000))
    {
        // One process, one attempt: a hung interpreter is killed and reported, never retried.
        process.kill();
        process.waitForFinished(5000);
        if (error)
            *error = failure(executable, QStringLiteral("探测超时"));
        return std::nullopt;
    }
    if (process.exitStatus() != QProcess::NormalExit || process.exitCode() != 0)
    {
        const auto detail = QString::fromUtf8(process.readAllStandardError()).trimmed();
        if (error)
            *error = failure(executable, detail.isEmpty()
                                              ? QStringLiteral("进程退出码 %1").arg(process.exitCode())
                                              : detail);
        return std::nullopt;
    }

    QJsonParseError parse{};
    const auto      document = QJsonDocument::fromJson(process.readAllStandardOutput(), &parse);
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

    PythonProbeResult result;
    result.version                   = QStringLiteral("%1.%2.%3")
                                           .arg(major)
                                           .arg(minor)
                                           .arg(info.value(QStringLiteral("micro")).toInt());
    result.environment.executable    = QDir::cleanPath(info.value(QStringLiteral("executable")).toString()).toStdWString();
    result.environment.prefix        = QDir::cleanPath(info.value(QStringLiteral("prefix")).toString()).toStdWString();
    result.environment.basePrefix    = QDir::cleanPath(info.value(QStringLiteral("base_prefix")).toString()).toStdWString();
    result.environment.dll           = QDir::cleanPath(info.value(QStringLiteral("dll")).toString()).toStdWString();
    result.environment.numpyVersion  = info.value(QStringLiteral("numpy")).toString().toStdString();
    for (const auto &entry : info.value(QStringLiteral("path")).toArray())
        result.environment.searchPaths.push_back(entry.toString().toStdWString());

    if (result.environment.dll.empty())
    {
        if (error)
            *error = failure(executable, QStringLiteral("无法定位 CPython 运行时 DLL"));
        return std::nullopt;
    }
    if (result.environment.numpyVersion.empty())
    {
        const auto reason = info.value(QStringLiteral("numpy_error")).toString();
        if (error)
            *error = failure(executable, QStringLiteral("缺少 NumPy：%1").arg(reason));
        return std::nullopt;
    }
    return result;
}

} // namespace visionrt
