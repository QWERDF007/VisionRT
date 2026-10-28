#include "log/LogService.h"

#include <QCoreApplication>
#include <QDateTime>
#include <QDir>
#include <QFileInfo>
#include <QThread>
#include <QTimer>

#include <spdlog/async.h>
#include <spdlog/details/log_msg.h>
#include <spdlog/logger.h>
#include <spdlog/pattern_formatter.h>
#include <spdlog/sinks/base_sink.h>
#include <spdlog/sinks/daily_file_sink.h>
#include <spdlog/sinks/stdout_sinks.h>
#include <spdlog/spdlog.h>

#include <algorithm>
#include <cstdio>
#include <deque>
#include <map>
#include <memory>
#include <shared_mutex>
#include <string>
#include <utility>
#include <vector>

namespace visionrt
{
namespace
{
// One pattern for the file and the window. It deliberately has no %n: the logger name is not a
// business source and the old output repeated it on every line. %v already carries "[source] text",
// so the line stays self-describing without a second field.
constexpr const char *kLinePattern = "[%Y/%m/%d %T.%e] [%L] [%t] %v";

// The coalescing map is bounded so a storm of distinct errors cannot grow without limit.
constexpr std::size_t kPendingLimit = 32;

std::atomic<bool>          gRunning{false};
std::atomic<std::uint64_t> gWriteFailures{0};

void onSpdlogError(const std::string &)
{
    gWriteFailures.fetch_add(1, std::memory_order_relaxed);
}

spdlog::level::level_enum toSpd(LogLevel level)
{
    switch (level)
    {
    case LogLevel::Trace: return spdlog::level::trace;
    case LogLevel::Debug: return spdlog::level::debug;
    case LogLevel::Info: return spdlog::level::info;
    case LogLevel::Warn: return spdlog::level::warn;
    case LogLevel::Error: return spdlog::level::err;
    case LogLevel::Critical: return spdlog::level::critical;
    }
    return spdlog::level::info;
}

QString levelText(spdlog::level::level_enum level)
{
    switch (level)
    {
    case spdlog::level::trace: return QStringLiteral("跟踪");
    case spdlog::level::debug: return QStringLiteral("调试");
    case spdlog::level::info: return QStringLiteral("信息");
    case spdlog::level::warn: return QStringLiteral("警告");
    case spdlog::level::err: return QStringLiteral("错误");
    case spdlog::level::critical: return QStringLiteral("致命");
    default: return QStringLiteral("信息");
    }
}

std::unique_ptr<spdlog::formatter> makeFormatter()
{
    return std::make_unique<spdlog::pattern_formatter>(kLinePattern, spdlog::pattern_time_type::local);
}

// One record is one line: the format string cannot stop an embedded newline, so the line-breaking
// controls are escaped here instead of being written through. The tool's own output text keeps its
// original characters; only the log line carries the visible escape.
std::string singleLine(std::string_view text)
{
    std::string escaped;
    escaped.reserve(text.size());
    for (const char character : text)
    {
        switch (character)
        {
        case '\r': escaped.append("\\r"); break;
        case '\n': escaped.append("\\n"); break;
        case '\v': escaped.append("\\v"); break;
        case '\f': escaped.append("\\f"); break;
        case '\0': escaped.append("\\0"); break;
        default: escaped.push_back(character); break;
        }
    }
    return escaped;
}

// Caps on a UTF-8 character boundary: a truncated multi-byte sequence would put invalid bytes into
// the file and into the window, which is exactly the corruption the cap is supposed to avoid.
std::string capBytes(std::string_view text, std::size_t maximum)
{
    if (text.size() <= maximum)
        return std::string(text);
    std::size_t keep = maximum;
    while (keep > 0 && (static_cast<unsigned char>(text[keep]) & 0xC0U) == 0x80U)
        --keep;
    std::string capped(text.substr(0, keep));
    capped.append("…");
    return capped;
}

// %v carries the business source in brackets so the formatter owns time, level and thread only.
std::string buildPayload(std::string_view source, std::string_view message)
{
    std::string payload;
    payload.reserve(source.size() + message.size() + 3);
    payload.push_back('[');
    payload.append(source.data(), source.size());
    payload.append("] ");
    payload.append(message.data(), message.size());
    return payload;
}

// Formats one line outside the sink path (drop summaries). The sinks format through their own
// formatter, so both paths share kLinePattern and cannot drift apart.
QString formatLine(spdlog::level::level_enum level, std::string_view source, std::string_view message,
                   spdlog::log_clock::time_point when = spdlog::log_clock::now())
{
    const auto payload = singleLine(buildPayload(source, message));
    spdlog::details::log_msg msg(spdlog::string_view_t("visionrt", 9), level,
                                 spdlog::string_view_t(payload.data(), payload.size()));
    msg.time = when;
    spdlog::memory_buf_t formatted;
    auto                 formatter = makeFormatter();
    formatter->format(msg, formatted);
    auto line = QString::fromUtf8(formatted.data(), static_cast<int>(formatted.size()));
    while (line.endsWith(QLatin1Char('\n')) || line.endsWith(QLatin1Char('\r')))
        line.chop(1);
    return line;
}
} // namespace

class LogService::BufferedSink final : public spdlog::sinks::base_sink<std::mutex>
{
public:
    BufferedSink(std::size_t capacity, std::size_t maximumBytes, std::function<void()> onPush)
        : capacity_(std::max<std::size_t>(1, capacity)), maximumBytes_(maximumBytes), onPush_(std::move(onPush))
    {
    }

    // Called on the GUI thread. Dropped lines are reported once as a synthetic summary record so a
    // saturated mailbox stays honest about what the window did not receive; the summary is one of
    // the requested records, never an extra one on top of the budget.
    QVector<LogRecord> take(std::size_t maximum, std::uint64_t &dropped)
    {
        std::lock_guard<std::mutex> lock(mutex_);
        QVector<LogRecord>          result;
        if (maximum == 0)
            return result;
        std::size_t budget = maximum;
        if (dropped_ > 0)
        {
            result.push_back(LogRecord{formatLine(spdlog::level::warn, "日志",
                                                  "队列已满，丢弃 " + std::to_string(dropped_) + " 条较早日志"),
                                       levelText(spdlog::level::warn)});
            dropped = dropped_;
            dropped_ = 0;
            --budget;
        }
        const auto count = std::min<std::size_t>(budget, records_.size());
        result.reserve(static_cast<int>(count) + result.size());
        for (std::size_t index = 0; index < count; ++index)
        {
            result.push_back(std::move(records_.front()));
            records_.pop_front();
        }
        return result;
    }

    std::size_t size()
    {
        std::lock_guard<std::mutex> lock(mutex_);
        return records_.size();
    }

protected:
    void sink_it_(const spdlog::details::log_msg &msg) override
    {
        // The window receives the formatter output itself, so a line can never be rendered
        // differently from the file. Only the trailing newline is dropped for the list row.
        spdlog::memory_buf_t formatted;
        base_sink<std::mutex>::formatter_->format(msg, formatted);
        auto line = QString::fromUtf8(formatted.data(), static_cast<int>(formatted.size()));
        while (line.endsWith(QLatin1Char('\n')) || line.endsWith(QLatin1Char('\r')))
            line.chop(1);

        LogRecord record;
        record.level = levelText(msg.level);
        const auto size = std::min<std::size_t>(static_cast<std::size_t>(line.size()), maximumBytes_);
        record.line     = line.left(static_cast<int>(size));
        if (size < static_cast<std::size_t>(line.size()))
            record.line += QStringLiteral("…");
        while (records_.size() >= capacity_)
        {
            records_.pop_front();
            ++dropped_;
        }
        records_.push_back(std::move(record));
        // Coalesced: the callback only flips one flag and posts a single queued drain request, so a
        // burst of lines cannot become a burst of Qt events.
        if (onPush_)
            onPush_();
    }

    void flush_() override {}

private:
    std::size_t           capacity_;
    std::size_t           maximumBytes_;
    std::deque<LogRecord> records_;
    std::uint64_t         dropped_ = 0;
    std::function<void()> onPush_;
};

// One error burst: the first line is already on its way, everything the burst adds is counted here
// together with the level and the time of the most recent occurrence.
struct LogService::PendingError
{
    LogLevel                      level = LogLevel::Error;
    std::string                   source;
    std::string                   message;
    std::uint64_t                 repeats = 0;
    spdlog::log_clock::time_point last;
};

struct LogService::Impl
{
    LogConfig                       config;
    QString                         path;
    std::shared_ptr<spdlog::logger> logger;
    std::shared_ptr<BufferedSink>   uiSink;
    std::shared_ptr<spdlog::sinks::sink> fileSink;
    bool                            fileReady = false;
    std::atomic<bool>               running{false};
    std::atomic<bool>               viewActive{false};
    std::atomic<bool>               notifyPending{false};
    // Written by start() before the producers are admitted and only read on the Qt thread and in
    // shutdown(), so the handler pair needs no separate lock.
    bool                            handlerInstalled = false;
    QtMessageHandler                previousHandler  = nullptr;
    QTimer                          uiTimer;
    QTimer                          flushTimer;
    std::shared_mutex               writeMutex;
    std::mutex                      coalesceMutex;
    std::map<std::string, PendingError, std::less<>> pendingErrors;
    std::uint64_t                   reportedDropped = 0;

    // The default location is the software directory, not a per-user data directory: the log has to
    // sit next to the executable the user actually launched.
    static QString defaultFile()
    {
        const auto directory = QCoreApplication::applicationDirPath();
        return (directory.isEmpty() ? QDir::currentPath() : directory) + QStringLiteral("/logs/visionrt.log");
    }
};

LogService::LogService(QObject *parent) : QObject(parent), impl_(std::make_unique<Impl>())
{
    qRegisterMetaType<QVector<LogRecord>>("QVector<visionrt::LogRecord>");
    // Connected once for the lifetime of the service: a stop/start cycle changes the intervals and
    // restarts the timers, but never installs a second connection that would double every batch.
    connect(&impl_->uiTimer, &QTimer::timeout, this, &LogService::drain);
    connect(&impl_->flushTimer, &QTimer::timeout, this, &LogService::flushPending);
}

LogService::~LogService()
{
    // Static destruction can run after QCoreApplication is gone; the owner (main or a test) has
    // already shut the service down at that point.
    if (QCoreApplication::instance())
        shutdown();
}

LogService &LogService::instance()
{
    static LogService service;
    return service;
}

bool LogService::start(const LogConfig &config, const QString &filePath)
{
    if (impl_->running.load(std::memory_order_acquire))
        return true;
    if (QCoreApplication::instance() && QThread::currentThread() != QCoreApplication::instance()->thread())
        return false;

    impl_->config = config;
    impl_->path   = filePath.isEmpty() ? Impl::defaultFile() : filePath;
    QDir().mkpath(QFileInfo(impl_->path).absolutePath());

    std::vector<spdlog::sink_ptr> sinks;
    try
    {
        // The base name keeps the date suffix implicit: spdlog writes today's local date into
        // `visionrt_YYYY-MM-DD.log` and switches file by itself at local midnight.
        auto file = std::make_shared<spdlog::sinks::daily_file_sink_mt>(impl_->path.toStdString(), 0, 0, false);
        file->set_formatter(makeFormatter());
        impl_->fileSink  = file;
        impl_->fileReady = true;
        sinks.push_back(file);
    }
    catch (const std::exception &)
    {
        // A log file that cannot be opened never blocks startup and never opens a dialog: the
        // service keeps running on stderr, counts the failure and says so in one diagnostic line.
        gWriteFailures.fetch_add(1, std::memory_order_relaxed);
        auto fallback = std::make_shared<spdlog::sinks::stderr_sink_mt>();
        fallback->set_formatter(makeFormatter());
        impl_->fileSink  = fallback;
        impl_->fileReady = false;
        sinks.push_back(fallback);
    }

    impl_->uiSink = std::make_shared<BufferedSink>(impl_->config.uiCapacity,
                                                   std::max<std::size_t>(256, impl_->config.maxMessageBytes * 2),
                                                   [this] { scheduleDrain(); });
    impl_->uiSink->set_formatter(makeFormatter());
    sinks.push_back(impl_->uiSink);

    spdlog::init_thread_pool(impl_->config.queueCapacity, 1);
    impl_->logger = std::make_shared<spdlog::async_logger>("visionrt", sinks.begin(), sinks.end(),
                                                           spdlog::thread_pool(),
                                                           spdlog::async_overflow_policy::overrun_oldest);
    impl_->logger->set_level(spdlog::level::trace);
    impl_->logger->flush_on(spdlog::level::err);
    spdlog::set_default_logger(impl_->logger);
    spdlog::flush_every(std::chrono::seconds(std::max(1, impl_->config.flushSeconds)));
    spdlog::set_error_handler(onSpdlogError);

    impl_->uiTimer.setInterval(std::max(10, impl_->config.uiBatchIntervalMs));
    impl_->flushTimer.setInterval(500);
    impl_->flushTimer.start();

    // Qt/QML logs are bridged with mapped levels; the original handler is restored at shutdown and
    // the bridge never recurses into qDebug.
    impl_->previousHandler = qInstallMessageHandler(
        [](QtMsgType type, const QMessageLogContext &context, const QString &message)
        {
            thread_local bool inside = false;
            if (inside)
            {
                std::fputs(message.toUtf8().constData(), stderr);
                std::fputc('\n', stderr);
                return;
            }
            inside = true;
            LogLevel level = LogLevel::Info;
            switch (type)
            {
            case QtDebugMsg: level = LogLevel::Debug; break;
            case QtInfoMsg: level = LogLevel::Info; break;
            case QtWarningMsg: level = LogLevel::Warn; break;
            case QtCriticalMsg: level = LogLevel::Error; break;
            case QtFatalMsg: level = LogLevel::Critical; break;
            }
            const std::string_view source = context.category ? std::string_view(context.category)
                                                             : std::string_view("Qt");
            LogService::instance().write(level, source, message.toStdString());
            if (type == QtFatalMsg)
            {
                std::fputs("VisionRT fatal: ", stderr);
                std::fputs(message.toUtf8().constData(), stderr);
                std::fputc('\n', stderr);
            }
            inside = false;
        });
    impl_->handlerInstalled = true;

    impl_->running.store(true, std::memory_order_release);
    gRunning.store(true, std::memory_order_release);
    if (impl_->fileReady)
        info("日志", ("日志服务已启动：" + impl_->path).toStdString());
    else
        warn("日志", ("日志目录不可写，日志仅输出到标准错误：" + QFileInfo(impl_->path).absolutePath()).toStdString());
    return true;
}

void LogService::shutdown()
{
    if (!impl_ || !impl_->running.load(std::memory_order_acquire))
        return;
    info("日志", "日志服务正在关闭");
    impl_->running.store(false, std::memory_order_release);
    gRunning.store(false, std::memory_order_release);

    // A handler of nullptr is the default handler, and restoring it is exactly what Qt documents;
    // the installed flag is therefore separate from the stored pointer.
    if (impl_->handlerInstalled)
    {
        qInstallMessageHandler(impl_->previousHandler);
        impl_->handlerInstalled = false;
        impl_->previousHandler  = nullptr;
    }
    impl_->uiTimer.stop();
    impl_->flushTimer.stop();
    impl_->viewActive.store(false, std::memory_order_release);

    // The exclusive side of the writer lock is the drain barrier: once it is held, every producer
    // that passed the admission check has already posted, and no new line can be enqueued while the
    // remaining summaries are written and the pool is joined.
    {
        std::unique_lock<std::shared_mutex> lock(impl_->writeMutex);
        postPendingLocked();
        if (impl_->logger)
        {
            impl_->logger->flush();
            spdlog::drop("visionrt");
        }
        spdlog::shutdown();
        impl_->uiSink.reset();
        impl_->fileSink.reset();
        impl_->logger.reset();
    }
    impl_->fileReady = false;
}

void LogService::flush()
{
    std::shared_lock<std::shared_mutex> lock(impl_->writeMutex);
    if (impl_->logger)
        impl_->logger->flush();
}

bool LogService::running() const
{
    return impl_ && impl_->running.load(std::memory_order_acquire);
}

bool LogService::fileSinkReady() const
{
    return impl_ && impl_->fileReady;
}

QString LogService::baseFilePath() const
{
    return impl_ ? impl_->path : QString();
}

void LogService::write(LogLevel level, std::string_view source, std::string_view message)
{
    if (!gRunning.load(std::memory_order_acquire))
        return;
    instance().writeImpl(level, source, message);
}

void LogService::postLocked(LogLevel level, std::string_view source, std::string_view message,
                            std::chrono::system_clock::time_point when)
{
    // One cap for both sinks: the line the window shows is the line the file contains. Escaping
    // comes first so the cap counts the bytes that are actually written.
    const auto escape = singleLine(message);
    const auto capped = capBytes(escape, impl_->config.maxMessageBytes);
    const auto payload = buildPayload(source, capped);
    // The recorded time travels with the line: a coalesced summary is stamped when the burst was
    // last seen, not when the timer happened to drain it.
    impl_->logger->log(when, spdlog::source_loc{}, toSpd(level),
                       spdlog::string_view_t(payload.data(), payload.size()));
}

std::vector<LogService::PendingError> LogService::takePending()
{
    std::vector<PendingError> pending;
    std::lock_guard<std::mutex> lock(impl_->coalesceMutex);
    pending.reserve(impl_->pendingErrors.size());
    for (auto &[key, entry] : impl_->pendingErrors)
        pending.push_back(std::move(entry));
    impl_->pendingErrors.clear();
    return pending;
}

void LogService::postPendingLocked()
{
    if (!impl_->logger)
        return;
    for (const auto &entry : takePending())
    {
        if (entry.repeats == 0)
            continue;
        // The summary keeps the level it was recorded with and the time of its last occurrence:
        // a critical never becomes a plain error, and a burst does not look like it happened now.
        postLocked(entry.level, entry.source,
                   entry.message + "（重复 " + std::to_string(entry.repeats) + " 次）", entry.last);
    }
}

void LogService::writeImpl(LogLevel level, std::string_view source, std::string_view message)
{
    if (level == LogLevel::Error)
    {
        writeError(source, message);
        return;
    }
    std::shared_lock<std::shared_mutex> lock(impl_->writeMutex);
    if (!impl_->running.load(std::memory_order_acquire) || !impl_->logger)
        return;
    // Critical is never collapsed and never waits for a timer: it is the one level that must reach
    // the file and the window as soon as it happens.
    postLocked(level, source, message, spdlog::log_clock::now());
}

void LogService::writeError(std::string_view source, std::string_view message)
{
    std::vector<PendingError> released;
    bool                      announce = false;
    std::shared_lock<std::shared_mutex> lock(impl_->writeMutex);
    if (!impl_->running.load(std::memory_order_acquire) || !impl_->logger)
        return;
    const auto now = spdlog::log_clock::now();
    {
        std::lock_guard<std::mutex> coalesce(impl_->coalesceMutex);
        std::string                 key(source);
        key.push_back('\n');
        key.append(message.data(), message.size());
        const auto found = impl_->pendingErrors.find(key);
        if (found == impl_->pendingErrors.end())
        {
            if (impl_->pendingErrors.size() >= kPendingLimit)
            {
                released.reserve(impl_->pendingErrors.size());
                for (auto &entry : impl_->pendingErrors)
                    released.push_back(std::move(entry.second));
                impl_->pendingErrors.clear();
            }
            impl_->pendingErrors.emplace(key, PendingError{LogLevel::Error, std::string(source),
                                                           std::string(message), 0, now});
            announce = true;
        }
        else
        {
            ++found->second.repeats;
            found->second.last = now;
        }
    }
    if (announce)
        postLocked(LogLevel::Error, source, message, now);
    for (const auto &entry : released)
        if (entry.repeats > 0)
            postLocked(entry.level, entry.source,
                       entry.message + "（重复 " + std::to_string(entry.repeats) + " 次）", entry.last);
}

void LogService::flushPending()
{
    std::shared_lock<std::shared_mutex> lock(impl_->writeMutex);
    if (!impl_->running.load(std::memory_order_acquire))
        return;
    postPendingLocked();
}

void LogService::write(LogLevel level, const QString &source, const QString &message)
{
    const auto sourceText = source.toUtf8();
    const auto messageText = message.toUtf8();
    write(level, std::string_view(sourceText.constData(), static_cast<std::size_t>(sourceText.size())),
          std::string_view(messageText.constData(), static_cast<std::size_t>(messageText.size())));
}

void LogService::debug(std::string_view source, std::string_view message)
{
    write(LogLevel::Debug, source, message);
}

void LogService::info(std::string_view source, std::string_view message)
{
    write(LogLevel::Info, source, message);
}

void LogService::warn(std::string_view source, std::string_view message)
{
    write(LogLevel::Warn, source, message);
}

void LogService::error(std::string_view source, std::string_view message)
{
    write(LogLevel::Error, source, message);
}

void LogService::setViewActive(bool active)
{
    if (impl_->viewActive.exchange(active, std::memory_order_acq_rel) == active)
        return;
    if (!active)
    {
        impl_->uiTimer.stop();
        return;
    }
    impl_->notifyPending.store(false, std::memory_order_release);
    scheduleDrain();
}

bool LogService::viewActive() const
{
    return impl_->viewActive.load(std::memory_order_acquire);
}

void LogService::scheduleDrain()
{
    if (!impl_->running.load(std::memory_order_acquire) || !impl_->viewActive.load(std::memory_order_acquire))
        return;
    bool expected = false;
    if (!impl_->notifyPending.compare_exchange_strong(expected, true, std::memory_order_acq_rel))
        return;
    QMetaObject::invokeMethod(
        this,
        [this]
        {
            impl_->notifyPending.store(false, std::memory_order_release);
            if (!impl_->running.load(std::memory_order_acquire) || !impl_->viewActive.load(std::memory_order_acquire))
                return;
            if (!impl_->uiTimer.isActive())
                impl_->uiTimer.start();
        },
        Qt::QueuedConnection);
}

void LogService::drain()
{
    if (!impl_->running.load(std::memory_order_acquire) || !impl_->uiSink)
    {
        impl_->uiTimer.stop();
        return;
    }
    std::uint64_t dropped = 0;
    const auto    batch =
        impl_->uiSink->take(static_cast<std::size_t>(std::max(1, impl_->config.uiBatchMaximum)), dropped);
    impl_->reportedDropped += dropped;
    if (!batch.isEmpty())
        Q_EMIT recordsAvailable(batch);
    if (impl_->uiSink->size() == 0)
        impl_->uiTimer.stop();
}

QVector<LogRecord> LogService::take(std::size_t maximum)
{
    if (!impl_->uiSink)
        return {};
    std::uint64_t dropped = 0;
    return impl_->uiSink->take(maximum, dropped);
}

std::uint64_t LogService::droppedRecords() const
{
    return impl_->reportedDropped;
}

std::uint64_t LogService::writeFailures() const
{
    return gWriteFailures.load(std::memory_order_relaxed);
}

LogConfig LogService::config() const
{
    return impl_->config;
}

} // namespace visionrt
