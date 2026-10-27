#pragma once

#include <QMetaType>
#include <QObject>
#include <QString>
#include <QVector>

#include <atomic>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <deque>
#include <functional>
#include <memory>
#include <mutex>
#include <string>
#include <string_view>

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

// One log line as the UI consumes it. The file keeps the same fields through the shared formatter.
struct LogRecord
{
    QString time;
    QString level;
    QString source;
    QString message;
};

// The starting configuration is a single source of truth for every bound in the log path. These
// are design targets, not measured results: the queue, the UI mailbox and the file rotation all
// read their limits from here.
struct LogConfig
{
    std::size_t queueCapacity    = 8192;
    std::size_t fileBytes        = 10 * 1024 * 1024;
    std::size_t fileCount        = 3;
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
// rotating file sink, one bounded UI mailbox and the Qt message bridge. Producers never block on
// the GUI; the GUI never waits on the worker.
class LogService final : public QObject
{
    Q_OBJECT

public:
    static LogService &instance();

    // Starts the service. A file that cannot be opened falls back to stderr and is counted; it
    // never raises a dialog and never recurses into logging. Repeated calls are ignored.
    bool start(const LogConfig &config = {}, const QString &filePath = {});
    // Stops producers first (restores the previous Qt handler), drains the queue, then releases the
    // logger and the thread pool. Safe to call when never started.
    void shutdown();
    bool running() const;
    // Drains the queue into the sinks without blocking the caller on the GUI.
    void flush();

    // Thread-safe, bounded producer entry. A message longer than maxMessageBytes is truncated in
    // the log only; tool output copy never inherits this limit.
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

    // Takes at most `maximum` records from the mailbox. Called on the GUI thread only.
    QVector<LogRecord> take(std::size_t maximum);
    std::uint64_t droppedRecords() const;
    std::uint64_t writeFailures() const;
    LogConfig     config() const;

signals:
    void recordsAvailable(const QVector<visionrt::LogRecord> &records);

private:
    explicit LogService(QObject *parent = nullptr);
    ~LogService() override;
    LogService(const LogService &)            = delete;
    LogService &operator=(const LogService &) = delete;

    class BufferedSink;

    void drain();
    void scheduleDrain();
    void writeImpl(LogLevel level, std::string_view source, std::string_view message);
    // Errors that repeat with the same key are collapsed into one record with a count, keeping the
    // first and the most recent occurrence instead of flooding the window.
    void writeCoalesced(LogLevel level, std::string_view source, std::string_view message,
                        const std::chrono::steady_clock::time_point &now);

    struct Impl;
    std::unique_ptr<Impl> impl_;
};

} // namespace visionrt

Q_DECLARE_METATYPE(visionrt::LogRecord)
Q_DECLARE_METATYPE(QVector<visionrt::LogRecord>)
