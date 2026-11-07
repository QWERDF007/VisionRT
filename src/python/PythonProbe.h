#pragma once

#include "python/PythonEnvironment.h"

#include <QString>

#include <optional>

namespace visionrt
{

struct PythonProbeResult
{
    PythonEnvironment environment;
    // "3.12.4" as the interpreter reports it, for the settings window and the diagnostics.
    QString version;
};

// Resolves a user choice to the interpreter file. A file is that interpreter; a directory is
// accepted only when exactly one known layout exists inside it, so an ambiguous directory asks the
// user for the file instead of picking one. An empty selection stays empty: "not configured" is a
// legal setting, not an error.
QString resolvePythonExecutable(const QString &selection, QString *error = nullptr);

// Runs the selected interpreter once (no shell, -I -B) and reads the facts it reports about itself.
// Returns nothing when the process cannot run, is not the CPython this build was made for, or lacks
// NumPy; `error` is the sentence the user has to act on. There is no retry and no second candidate.
std::optional<PythonProbeResult> probePythonEnvironment(const QString &executable, QString *error = nullptr);

} // namespace visionrt
