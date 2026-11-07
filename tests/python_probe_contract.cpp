// The selected interpreter is the single source of Python facts. This contract covers the two
// halves that must not drift: a directory selection resolves to one unambiguous file, and the probe
// either reports the environment the interpreter actually runs in or explains why it cannot.

#include "python/PythonProbe.h"

#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QTemporaryDir>

#include <cassert>
#include <iostream>
#include <stdexcept>

using namespace visionrt;

namespace {

void require(bool condition, const char *message)
{
    if (!condition)
        throw std::runtime_error(message);
}

void writeFile(const QString &path)
{
    QDir().mkpath(QFileInfo(path).absolutePath());
    QFile file(path);
    require(file.open(QIODevice::WriteOnly), "probe fixture file could not be created");
    file.write("fixture");
}

} // namespace

int main(int argc, char **argv)
try
{
    QCoreApplication application(argc, argv);
    const auto       buildInterpreter = QString::fromUtf8(VISIONRT_TEST_PYTHON_EXECUTABLE);

    // "Not configured" is a legal choice and never an error.
    {
        QString error;
        require(resolvePythonExecutable(QString{}, &error).isEmpty() && error.isEmpty(),
                "an empty selection must stay empty");
    }

    // A file is the interpreter; the canonical absolute path is what the settings file stores.
    {
        QString error;
        const auto resolved = resolvePythonExecutable(buildInterpreter, &error);
        require(!resolved.isEmpty() && QFileInfo(resolved).isAbsolute(), "a selected interpreter file did not resolve");
        require(QDir::cleanPath(resolved) == QDir::cleanPath(QFileInfo(buildInterpreter).absoluteFilePath()),
                "the selected interpreter file changed identity");
    }

    // The Conda/standard-installation layout: the directory contains python.exe directly.
    {
        const auto directory = QFileInfo(buildInterpreter).absolutePath();
        QString    error;
        const auto resolved = resolvePythonExecutable(directory, &error);
        require(!resolved.isEmpty() && QFileInfo(resolved).fileName().compare("python.exe", Qt::CaseInsensitive) == 0,
                "a standard installation directory did not resolve to python.exe");
    }

    // Two known layouts in one directory is ambiguous: the user has to name the file.
    {
        QTemporaryDir temporary;
        require(temporary.isValid(), "probe fixture directory is unavailable");
        writeFile(temporary.filePath(QStringLiteral("python.exe")));
        writeFile(temporary.filePath(QStringLiteral("Scripts/python.exe")));
        QString error;
        require(resolvePythonExecutable(temporary.path(), &error).isEmpty(), "an ambiguous directory was guessed");
        require(!error.isEmpty(), "an ambiguous directory reported no reason");
    }

    // A path that does not exist is reported, never replaced by another interpreter.
    {
        QString error;
        require(resolvePythonExecutable(QStringLiteral("Z:/visionrt/missing/python.exe"), &error).isEmpty(),
                "a missing interpreter path resolved");
        require(!error.isEmpty(), "a missing interpreter path reported no reason");
    }

    // Positive sample: the build interpreter answers for itself.
    {
        QString error;
        const auto probed = probePythonEnvironment(buildInterpreter, &error);
        require(probed.has_value(), qPrintable(QStringLiteral("the build interpreter was rejected: ") + error));
        const auto &environment = probed->environment;
        require(!probed->version.isEmpty(), "the probe reported no version");
        require(environment.executable.is_absolute() && QFileInfo::exists(QString::fromStdWString(environment.executable.wstring())),
                "the probe reported no executable");
        require(!environment.prefix.empty() && !environment.basePrefix.empty(), "the probe reported no prefix");
        require(environment.dll.is_absolute() && QFileInfo::exists(QString::fromStdWString(environment.dll.wstring())),
                "the probe did not locate the running CPython DLL");
        require(!environment.searchPaths.empty(), "the probe reported no search paths");
        require(!environment.numpyVersion.empty(), "the probe accepted an interpreter without NumPy");
    }

    // Negative sample: an executable that is not CPython answers with a reason, not an environment.
    {
        QString    error;
        const auto probed = probePythonEnvironment(QStringLiteral("C:/Windows/System32/where.exe"), &error);
        require(!probed.has_value() && !error.isEmpty(), "a non-CPython executable was accepted");
    }

    std::cout << "Python probe contract passed\n";
    return 0;
}
catch (const std::exception &error)
{
    std::cerr << error.what() << '\n';
    return 1;
}
