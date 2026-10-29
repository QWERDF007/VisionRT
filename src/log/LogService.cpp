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

constexpr std::size_t kMaxSourceBytes = 128;

} // namespace

#if defined(VISIONRT_LOG_TEST_HOOKS) && VISIONRT_LOG_TEST_HOOKS
namespace {
std::mutex gHooksMutex;
visionrt::LogTestHooks gTestHooks;
} // namespace

void setLogTestHooks(LogTestHooks hooks)
{
    std::lock_guard<std::mutex> lock(gHooksMutex);
    gTestHooks = std::move(hooks);
}
#endif

// Escapes control characters and bounds text within maxBytes on a UTF-8 character boundary.
// When truncated, total bytes including the "…" mark (3 bytes) will not exceed maxBytes.
BoundedText boundedSingleLine(std::string_view text, std::size_t maxBytes)
{
    if (maxBytes == 0)
        return {"", !text.empty()};

    constexpr std::string_view kEllipsis     = "…";
    constexpr std::size_t     kEllipsisBytes = 3;

    std::string out;
    out.reserve(std::min(text.size(), maxBytes));

    const std::string_view mark         = (maxBytes >= kEllipsisBytes) ? kEllipsis : "";
    const std::size_t      prefixBudget = maxBytes - mark.size();
    std::size_t            markerCut    = 0;

    std::size_t i = 0;
    while (i < text.size())
    {
        const auto       u = static_cast<unsigned char>(text[i]);
        std::string_view seq;
        std::size_t      advance = 1;

        switch (text[i])
        {
        case '\r': seq = "\\r"; advance = 1; break;
        case '\n': seq = "\\n"; advance = 1; break;
        case '\v': seq = "\\v"; advance = 1; break;
        case '\f': seq = "\\f"; advance = 1; break;
        case '\0': seq = "\\0"; advance = 1; break;
        default:
            if (u < 0x80)
            {
                seq = text.substr(i, 1);
                advance = 1;
            }
            else if (u >= 0xC2 && u <= 0xDF)
            {
                if (i + 1 < text.size() && (static_cast<unsigned char>(text[i + 1]) & 0xC0) == 0x80)
                {
                    seq = text.substr(i, 2);
                    advance = 2;
                }
                else
                {
                    seq = "?";
                    advance = 1;
                }
            }
            else if (u >= 0xE0 && u <= 0xEF)
            {
                if (i + 2 < text.size())
                {
                    const auto c1 = static_cast<unsigned char>(text[i + 1]);
                    const auto c2 = static_cast<unsigned char>(text[i + 2]);
                    bool ok = (c2 & 0xC0) == 0x80;
                    if (u == 0xE0) ok = ok && (c1 >= 0xA0 && c1 <= 0xBF);
                    else if (u >= 0xE1 && u <= 0xEC) ok = ok && ((c1 & 0xC0) == 0x80);
                    else if (u == 0xED) ok = ok && (c1 >= 0x80 && c1 <= 0x9F);
                    else if (u >= 0xEE && u <= 0xEF) ok = ok && ((c1 & 0xC0) == 0x80);
                    if (ok)
                    {
                        seq = text.substr(i, 3);
                        advance = 3;
                    }
                    else
                    {
                        seq = "?";
                        advance = 1;
                    }
                }
                else
                {
                    seq = "?";
                    advance = 1;
                }
            }
            else if (u >= 0xF0 && u <= 0xF4)
            {
                if (i + 3 < text.size())
                {
                    const auto c1 = static_cast<unsigned char>(text[i + 1]);
                    const auto c2 = static_cast<unsigned char>(text[i + 2]);
                    const auto c3 = static_cast<unsigned char>(text[i + 3]);
                    bool ok = ((c2 & 0xC0) == 0x80) && ((c3 & 0xC0) == 0x80);
                    if (u == 0xF0) ok = ok && (c1 >= 0x90 && c1 <= 0xBF);
                    else if (u >= 0xF1 && u <= 0xF3) ok = ok && ((c1 & 0xC0) == 0x80);
                    else if (u == 0xF4) ok = ok && (c1 >= 0x80 && c1 <= 0x8F);
                    if (ok)
                    {
                        seq = text.substr(i, 4);
                        advance = 4;
                    }
                    else
                    {
                        seq = "?";
                        advance = 1;
                    }
                }
                else
                {
                    seq = "?";
                    advance = 1;
                }
            }
            else
            {
                seq = "?";
                advance = 1;
            }
            break;
        }

        if (seq.size() > maxBytes - out.size())
        {
            out.resize(markerCut);
            out.append(mark);
            return {std::move(out), true};
        }

        out.append(seq);
        if (out.size() <= prefixBudget)
            markerCut = out.size();
        i += advance;
    }

    return {std::move(out), false};
}

namespace {

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
    const auto payload = buildPayload(source, message);
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
    BufferedSink(std::size_t capacity, std::function<void()> onPush)
        : capacity_(std::max<std::size_t>(1, capacity)), onPush_(std::move(onPush))
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
        record.line  = std::move(line);
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

    PendingError() = default;
    PendingError(LogLevel lvl, std::string src, std::string msg, std::uint64_t reps, spdlog::log_clock::time_point lst)
        : level(lvl), source(std::move(src)), message(std::move(msg)), repeats(reps), last(lst) {}
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

    {
        std::unique_lock<std::shared_mutex> lock(impl_->writeMutex);
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
    }
    // Startup log lines are dispatched outside the exclusive writeMutex to avoid recursive lock acquisition
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
    const auto payload = buildPayload(source, message);
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

void LogService::postPendingSummaryLocked(const PendingError &entry, std::size_t maxBytes)
{
    const std::string suffix = "（重复 " + std::to_string(entry.repeats) + " 次）";
    std::string summary;
    if (maxBytes > suffix.size())
    {
        const auto bounded = boundedSingleLine(entry.message, maxBytes - suffix.size());
        summary = bounded.text + suffix;
    }
    else
    {
        const std::string shortSuffix = "×" + std::to_string(entry.repeats);
        if (maxBytes > shortSuffix.size())
            summary = boundedSingleLine(entry.message, maxBytes - shortSuffix.size()).text + shortSuffix;
        else
            summary = boundedSingleLine(shortSuffix, maxBytes).text;
    }
    postLocked(entry.level, entry.source, summary, entry.last);
}

void LogService::postPendingLocked()
{
    if (!impl_->logger)
        return;
    const auto maxBytes = impl_->config.maxMessageBytes;
    for (const auto &entry : takePending())
    {
        if (entry.repeats == 0)
            continue;
        postPendingSummaryLocked(entry, maxBytes);
    }
}

void LogService::writeImpl(LogLevel level, std::string_view source, std::string_view message)
{
    if (level == LogLevel::Error)
    {
        writeError(source, message);
        return;
    }

#if defined(VISIONRT_LOG_TEST_HOOKS) && VISIONRT_LOG_TEST_HOOKS
    {
        std::function<void(std::string_view)> hook;
        {
            std::lock_guard<std::mutex> hLock(gHooksMutex);
            hook = gTestHooks.onBeforeWriteLock;
        }
        if (hook)
            hook(source);
    }
#endif

    std::shared_lock<std::shared_mutex> lock(impl_->writeMutex);
    if (!impl_->running.load(std::memory_order_acquire) || !impl_->logger)
        return;

#if defined(VISIONRT_LOG_TEST_HOOKS) && VISIONRT_LOG_TEST_HOOKS
    {
        std::function<void(std::string_view)> hook;
        {
            std::lock_guard<std::mutex> hLock(gHooksMutex);
            hook = gTestHooks.onInsideWriteLock;
        }
        if (hook)
            hook(source);
    }
#endif

    const auto maxBytes       = impl_->config.maxMessageBytes;
    const auto boundedSource  = boundedSingleLine(source, kMaxSourceBytes);
    const auto boundedMessage = boundedSingleLine(message, maxBytes);
    postLocked(level, boundedSource.text, boundedMessage.text, spdlog::log_clock::now());
}

void LogService::writeError(std::string_view source, std::string_view message)
{
#if defined(VISIONRT_LOG_TEST_HOOKS) && VISIONRT_LOG_TEST_HOOKS
    {
        std::function<void(std::string_view)> hook;
        {
            std::lock_guard<std::mutex> hLock(gHooksMutex);
            hook = gTestHooks.onBeforeWriteLock;
        }
        if (hook)
            hook(source);
    }
#endif

    std::shared_lock<std::shared_mutex> lock(impl_->writeMutex);
    if (!impl_->running.load(std::memory_order_acquire) || !impl_->logger)
        return;

#if defined(VISIONRT_LOG_TEST_HOOKS) && VISIONRT_LOG_TEST_HOOKS
    {
        std::function<void(std::string_view)> hook;
        {
            std::lock_guard<std::mutex> hLock(gHooksMutex);
            hook = gTestHooks.onInsideWriteLock;
        }
        if (hook)
            hook(source);
    }
#endif

    const auto maxBytes       = impl_->config.maxMessageBytes;
    const auto boundedSource  = boundedSingleLine(source, kMaxSourceBytes);
    const auto boundedMessage = boundedSingleLine(message, maxBytes);

    // Minimum budget required to display repeat count without losing digits:
    // "×" (2 bytes in UTF-8) + 20 (max decimal digits of uint64) = 22 bytes.
    constexpr std::size_t minCountBudget = 22;
    if (boundedSource.truncated || boundedMessage.truncated || maxBytes < minCountBudget)
    {
        postLocked(LogLevel::Error, boundedSource.text, boundedMessage.text, spdlog::log_clock::now());
        return;
    }

    std::vector<PendingError> released;
    bool                      announce = false;
    const auto now = spdlog::log_clock::now();
    {
        std::lock_guard<std::mutex> coalesce(impl_->coalesceMutex);
        std::string                 key = boundedSource.text;
        key.push_back('\n');
        key.append(boundedMessage.text);
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
            impl_->pendingErrors.emplace(key, PendingError{LogLevel::Error, boundedSource.text,
                                                           boundedMessage.text, 0, now});
            announce = true;
        }
        else
        {
            ++found->second.repeats;
            found->second.last = now;
        }
    }

    if (announce)
        postLocked(LogLevel::Error, boundedSource.text, boundedMessage.text, now);

    for (const auto &entry : released)
        if (entry.repeats > 0)
            postPendingSummaryLocked(entry, maxBytes);
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
