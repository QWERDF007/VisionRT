#pragma once

#include "python/PythonEnvironment.h"

#include <QObject>
#include <QString>

#include <memory>
#include <optional>

namespace visionrt
{

// Resolves a user choice to the interpreter file. A file is that interpreter; a directory is
// accepted only when exactly one known layout exists inside it, so an ambiguous directory asks the
// user for the file instead of picking one. An empty selection stays empty: "not configured" is a
// legal setting, not an error.
QString resolvePythonExecutable(const QString &selection, QString *error = nullptr);

// Runs the selected interpreter once (no shell, -I -B) and reads the facts it reports about itself.
// Returns nothing when the process cannot run, is not the CPython this build was made for, or lacks
// NumPy; `error` is the sentence the user has to act on. There is no retry and no second candidate.
std::optional<PythonEnvironment> probePythonEnvironment(const QString &executable,
                                                       QString *error = nullptr);

struct PythonProbeResult
{
    PythonEnvironmentStatus status;
    // Only present when the status is Ready: the facts the bridge must start from.
    std::optional<PythonEnvironment> environment;
};

// One environment check at a time, off the GUI thread. The check resolves the selection and runs the
// interpreter on a worker thread, then delivers a single result to the thread that owns this object,
// so a slow or hung interpreter never freezes the window: the caller keeps rendering while the
// phase is Probing. Closing the object cancels the running check instead of waiting it out.
class PythonProbe final : public QObject
{
    Q_OBJECT
public:
    explicit PythonProbe(QObject *parent = nullptr);
    ~PythonProbe() override;
    PythonProbe(const PythonProbe &)            = delete;
    PythonProbe &operator=(const PythonProbe &) = delete;

    // Starts one check. A second request while one is running is refused rather than queued: one
    // selection has exactly one check, one timeout and no retry.
    bool start(const QString &selection);
    bool running() const noexcept;

    // Outcome of the last check; phase Probing while one runs.
    PythonProbeResult result() const;

signals:
    // Emitted on the object's thread, after result() holds the outcome.
    void finished();

private:
    // Runs on the object's thread when the worker delivers its outcome.
    void finish(PythonProbeResult outcome);

    struct Impl;
    std::unique_ptr<Impl> impl_;
};

} // namespace visionrt
