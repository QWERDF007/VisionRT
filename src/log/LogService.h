#pragma once

#include <QMetaType>
#include <QObject>
#include <QString>
#include <QVector>

#include <atomic>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <mutex>
#include <shared_mutex>
#include <string>
#include <string_view>
#include <vector>

namespace spdlog
{
class logger;
namespace sinks
{
class sink;
}
} // namespace spdlog

namespace visionrt
{

// One log line exactly as it is written to the file. `level` is the already-rendered level name
// and exists only so the window can colour the row; the text itself is never re-composed.
struct LogRecord
{
    QString line;
    QString level;
};

// Every bound of the log path lives here: the async queue, the UI mailbox, the batch window and
// the per-message cap. There is deliberately no size rotation - the file sink rotates per local
// day, so byte/count limits would be a second, conflicting policy.
struct LogConfig
{
    std::size_t queueCapacity    = 8192;
    std::size_t uiCapacity       = 2000;
    std::size_t maxMessageBytes  = 4096;
    int         flushSeconds     = 3;
    int         uiBatchIntervalMs = 50;
    int         uiBatchMaximum   = 200;
};

enum class LogLevel
{
    Trace,
    Debug,
    Info,
    Warn,
    Error,
    Critical
};

// Process-wide logging service: one async spdlog logger (bounded queue, overrun_oldest), one
// daily file sink under <executable>/logs, one bounded UI mailbox and the Qt message bridge.
// Producers never block on the GUI; the GUI never waits on the worker. The window and the file
// receive the same line, produced by one formatter.
//
// Threading contract: producers take the shared side of `writeMutex` for the whole post, so a
// shutdown that takes it exclusively knows every in-flight line already reached the queue before
// the worker, the sinks and the logger are released.
class LogService final : public QObject
{
    Q_OBJECT

public:
    static LogService &instance();

    // Starts the service. `filePath` is the daily base name (`visionrt.log` produces
    // `visionrt_YYYY-MM-DD.log`); an empty path means `<executable directory>/logs/visionrt.log`.
    // A directory or file that cannot be written falls back to stderr, is counted in
    // writeFailures() and is reported as one diagnostic line - never silently relocated.
    bool start(const LogConfig &config = {}, const QString &filePath = {});
    // Stops producers first (restores the previous Qt handler, including the default one), drains
    // the queue, then releases the logger and the thread pool. Safe to call when never started and
    // safe to call twice; a later start() restarts a clean service.
    void shutdown();
    bool running() const;
    // Drains the queue into the sinks without blocking the caller on the GUI.
    void flush();

    // Thread-safe, bounded producer entry. The source is rendered into the line as `[source]`, so
    // the text keeps the business origin while the formatter owns time, level and thread.
    static void write(LogLevel level, std::string_view source, std::string_view message);
    static void write(LogLevel level, const QString &source, const QString &message);
    void debug(std::string_view source, std::string_view message);
    void info(std::string_view source, std::string_view message);
    void warn(std::string_view source, std::string_view message);
    void error(std::string_view source, std::string_view message);

    // Called by the log window: while active the service drains the mailbox in batches; while
    // hidden nothing is copied to the GUI and the mailbox stays bounded.
    void setViewActive(bool active);
    bool viewActive() const;

    // Takes at most `maximum` records from the mailbox, including the drop summary. Called on the
    // GUI thread only.
    QVector<LogRecord> take(std::size_t maximum);
    std::uint64_t droppedRecords() const;
    std::uint64_t writeFailures() const;
    LogConfig     config() const;
    // Daily base name the service was started with, without the date suffix.
    QString baseFilePath() const;

    // True when the file sink was created. False means the session logs to stderr only.
    bool fileSinkReady() const;

signals:
    void recordsAvailable(const QVector<visionrt::LogRecord> &records);

private:
    explicit LogService(QObject *parent = nullptr);
    ~LogService() override;
    LogService(const LogService &)            = delete;
    LogService &operator=(const LogService &) = delete;

    class BufferedSink;

    struct PendingError;

    void drain();
    void scheduleDrain();
    void writeImpl(LogLevel level, std::string_view source, std::string_view message);
    // Errors are the only level that coalesces: the first occurrence is posted immediately and the
    // repeats are counted until the next flush, so a burst stays one readable row without delaying
    // the first report.
    void writeError(std::string_view source, std::string_view message);
    void flushPending();
    // `postLocked` and `postPendingLocked` require the caller to hold `writeMutex` (shared or
    // exclusive); `takePending` also needs it and takes the coalesce mutex itself. That single lock
    // order - writer lock, then coalesce - is what keeps producers and shutdown deadlock-free.
    void postLocked(LogLevel level, std::string_view source, std::string_view message,
                    std::chrono::system_clock::time_point when);
    void postPendingLocked();
    void postPendingSummaryLocked(const PendingError &entry, std::size_t maxBytes);
    std::vector<PendingError> takePending();

    struct Impl;
    std::unique_ptr<Impl> impl_;
};

} // namespace visionrt

Q_DECLARE_METATYPE(visionrt::LogRecord)
Q_DECLARE_METATYPE(QVector<visionrt::LogRecord>)
