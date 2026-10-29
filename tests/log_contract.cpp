// Contract for the log service: one plain-text line per record, the same line in the window and in
// the daily file, an unwritable location that degrades honestly, bounded queues, batching, drop
// accounting, error coalescing, the Qt bridge and shutdown idempotence. The service is driven
// through its public producer API only.
#include "log/LogModel.h"
#include "log/LogService.h"

#include <QCoreApplication>
#include <QDate>
#include <QDir>
#include <QFile>
#include <QRegularExpression>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QTest>

#include <atomic>
#include <condition_variable>
#include <functional>
#include <future>
#include <iostream>
#include <stdexcept>
#include <thread>
#include <vector>

// Thread-local allocation probe to observe heap allocations in isolation
static thread_local bool        g_trackAllocations = false;
static thread_local std::size_t g_allocatedCount   = 0;
static thread_local std::size_t g_allocatedBytes   = 0;

void* operator new(std::size_t size)
{
    if (g_trackAllocations)
    {
        ++g_allocatedCount;
        g_allocatedBytes += size;
    }
    void* ptr = std::malloc(size);
    if (!ptr)
        throw std::bad_alloc();
    return ptr;
}

void operator delete(void* ptr) noexcept
{
    std::free(ptr);
}

void operator delete(void* ptr, std::size_t) noexcept
{
    std::free(ptr);
}

using namespace visionrt;

namespace {
void require(bool condition, const char *message)
{
    if (!condition)
        throw std::runtime_error(message);
}

QString fileText(const QString &path)
{
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly))
        return {};
    return QString::fromUtf8(file.readAll());
}

QString rowLine(const LogModel &model, int row)
{
    return model.data(model.index(row, 0), LogModel::LineRole).toString();
}

void probeHandler(QtMsgType, const QMessageLogContext &, const QString &) {}

bool anyRow(const LogModel &model, const std::function<bool(const QString &)> &match)
{
    for (int row = 0; row < model.rowCount(); ++row)
        if (match(rowLine(model, row)))
            return true;
    return false;
}
} // namespace

int main(int argc, char **argv)
try
{
    QCoreApplication application(argc, argv);
    QTemporaryDir  directory;
    require(directory.isValid(), "log fixture directory is invalid");

    LogConfig config;
    config.queueCapacity     = 1024;
    config.uiCapacity        = 32;
    config.maxMessageBytes   = 64;
    config.flushSeconds      = 1;
    config.uiBatchIntervalMs = 20;
    config.uiBatchMaximum    = 8;

    auto &service = LogService::instance();

    // 1. An unwritable location is never silently relocated: the session keeps running on stderr,
    //    counts the failure and leaves exactly one diagnostic line in the window.
    const auto blockedParent = directory.filePath(QStringLiteral("blocked"));
    {
        QFile blocker(blockedParent);
        require(blocker.open(QIODevice::WriteOnly), "cannot create the unwritable-path fixture");
        blocker.write("x");
    }
    require(service.start(config, blockedParent + QStringLiteral("/logs/visionrt.log")),
            "the service refused to start on an unwritable directory");
    require(!service.fileSinkReady(), "an unwritable directory reported a ready file sink");
    require(service.writeFailures() > 0, "an unwritable directory was not counted as a write failure");
    LogService::write(LogLevel::Info, QStringLiteral("测试"), QStringLiteral("降级会话"));
    service.flush();
    QTest::qWait(120);
    bool degradedReported = false;
    for (const auto &record : service.take(64))
        degradedReported = degradedReported || record.line.contains(QStringLiteral("日志目录不可写"));
    require(degradedReported, "the unwritable log directory was not reported to the window");
    service.shutdown();
    require(!service.running(), "shutdown did not stop the service");

    // 2. The default location is the software directory and the file name carries the local date.
    require(service.start(config), "the service refused to start on its own thread");
    require(service.running(), "the service did not report running");
    require(LogService::instance().start(config), "starting twice was not idempotent");
    require(service.fileSinkReady(), "the default log location is not writable");
    const auto basePath = service.baseFilePath();
    require(QFileInfo(basePath).absolutePath()
                == QCoreApplication::applicationDirPath() + QStringLiteral("/logs"),
            "the default log directory is not the software directory");
    const auto datedFile = QFileInfo(basePath).absolutePath() + QStringLiteral("/visionrt_")
                         + QDate::currentDate().toString(QStringLiteral("yyyy-MM-dd")) + QStringLiteral(".log");
    require(QFile::exists(datedFile), "the daily log file was not created next to the executable");

    LogModel model;
    model.setCapacity(static_cast<std::size_t>(config.uiCapacity));

    // 3. Hidden window: records stay in the bounded mailbox and nothing reaches the model.
    for (int index = 0; index < 12; ++index)
        LogService::write(LogLevel::Info, QStringLiteral("测试"),
                          QStringLiteral("隐藏批次 %1").arg(index));
    QTest::qWait(120);
    require(model.rowCount() == 0, "a hidden log window received records");

    QSignalSpy batches(&service, &LogService::recordsAvailable);
    service.setViewActive(true);
    // The startup line shares the same mailbox, so the batch is checked by content, not by an
    // exact count that would depend on when the service itself logged.
    require(QTest::qWaitFor([&] { return model.rowCount() >= 12; }, 2000),
            "the visible window did not receive the bounded batch");
    require(anyRow(model, [](const QString &line) { return line.endsWith(QStringLiteral("隐藏批次 11")); }),
            "the visible window lost the last record of the hidden batch");
    require(!batches.isEmpty(), "visible batches were not reported");
    for (const auto &arguments : batches)
    {
        const auto records = arguments.front().value<QVector<LogRecord>>();
        require(records.size() <= config.uiBatchMaximum,
                "the window received more than one bounded batch in a single delivery");
    }

    // 4. Every row is one finished line: time, level, thread, business source and text, with no
    //    logger name and no ANSI escape. The file receives the identical line.
    const QRegularExpression linePattern(
        QStringLiteral("^\\[\\d{4}/\\d{2}/\\d{2} \\d{2}:\\d{2}:\\d{2}\\.\\d{3}\\] \\[[A-Z]\\] \\[\\d+\\] "
                       "\\[[^\\]]+\\] .*$"));
    require(anyRow(model, [&](const QString &line) { return linePattern.match(line).hasMatch(); }),
            "a log row is not the expected single-line pattern");
    require(!anyRow(model, [](const QString &line) { return line.contains(QStringLiteral("[visionrt]")); }),
            "a log row still contains the logger name");
    require(!anyRow(model, [](const QString &line) { return line.contains(QChar(0x1b)); }),
            "a log row contains ANSI escapes");
    const auto visibleLine = rowLine(model, model.rowCount() - 1);
    service.flush();
    QTest::qWait(120);
    require(fileText(datedFile).contains(visibleLine),
            "the file does not contain the identical line the window displays");

    // 5. Overflow uses overrun_oldest and reports one summary for the discarded records.
    service.setViewActive(false);
    for (int index = 0; index < static_cast<int>(config.uiCapacity) * 3; ++index)
        LogService::write(LogLevel::Info, QStringLiteral("测试"),
                          QStringLiteral("溢出 %1").arg(index));
    QTest::qWait(100);
    service.setViewActive(true);
    require(QTest::qWaitFor([&] { return anyRow(model, [](const QString &line) { return line.contains(QStringLiteral("丢弃")); }); },
                            3000),
            "mailbox overflow was not reported to the window");
    require(service.droppedRecords() > 0, "discarded records were not counted");

    // 6. Error bursts: the first occurrence is queued immediately with its own level, and only the
    //    repeats are counted until the next flush.
    service.setViewActive(true);
    for (int index = 0; index < 5; ++index)
        LogService::write(LogLevel::Error, QStringLiteral("测试工具"), QStringLiteral("同一个错误"));
    require(QTest::qWaitFor(
                [&]
                {
                    for (int row = 0; row < model.rowCount(); ++row)
                    {
                        if (rowLine(model, row).endsWith(QStringLiteral("同一个错误"))
                            && model.data(model.index(row, 0), LogModel::LevelRole).toString()
                                   == QStringLiteral("错误"))
                            return true;
                    }
                    return false;
                },
                1000),
            "the first error of a burst was not queued immediately");
    require(QTest::qWaitFor([&] { return anyRow(model, [](const QString &line) { return line.contains(QStringLiteral("重复 4 次")); }); },
                            3000),
            "the repeats of an error burst were not summarised with their count");

    // 6b. Critical never waits for a timer and is never reported under another level.
    LogService::write(LogLevel::Critical, QStringLiteral("测试工具"), QStringLiteral("致命错误"));
    require(QTest::qWaitFor(
                [&] { return anyRow(model, [](const QString &line) { return line.endsWith(QStringLiteral("致命错误")); }); },
                500),
            "a critical line waited for the batch timer");
    for (int row = 0; row < model.rowCount(); ++row)
        if (rowLine(model, row).endsWith(QStringLiteral("致命错误")))
            require(model.data(model.index(row, 0), LogModel::LevelRole).toString() == QStringLiteral("致命"),
                    "a critical line was reported under a different level");

    // 6c. One record is one line: CR/LF and other line breaks are escaped, never written through.
    LogService::write(LogLevel::Info, QStringLiteral("测试"), QStringLiteral("第一行\r\n第二行\n第三行"));
    require(QTest::qWaitFor([&] { return anyRow(model, [](const QString &line) { return line.contains(QStringLiteral("第一行")); }); },
                            2000),
            "the multi-line record did not reach the window");
    require(anyRow(model, [](const QString &line) { return line.contains(QStringLiteral("第一行\\r\\n第二行\\n第三行")); }),
            "line breaks were not escaped into visible sequences");
    for (int row = 0; row < model.rowCount(); ++row)
    {
        const auto line = rowLine(model, row);
        require(!line.contains(QLatin1Char('\n')) && !line.contains(QLatin1Char('\r')),
                "a log row contains a raw line break");
    }

    // 7. Long messages are capped identically for the window and the file.
    const auto oversized = QString(200, QLatin1Char('x'));
    LogService::write(LogLevel::Info, QStringLiteral("测试"), oversized);
    require(QTest::qWaitFor([&] { return anyRow(model, [](const QString &line) { return line.endsWith(QStringLiteral("…")); }); },
                            2000),
            "an oversized log message was not truncated");
    for (int row = 0; row < model.rowCount(); ++row)
        require(rowLine(model, row).size() < 200, "a truncated row kept the full payload");

    // 8. The Qt bridge maps levels and categories and does not recurse.
    qWarning("日志桥接警告");
    require(QTest::qWaitFor([&] { return anyRow(model, [](const QString &line) { return line.endsWith(QStringLiteral("日志桥接警告")); }); },
                            2000),
            "qWarning did not reach the log window");
    for (int row = 0; row < model.rowCount(); ++row)
        if (rowLine(model, row).endsWith(QStringLiteral("日志桥接警告")))
            require(model.data(model.index(row, 0), LogModel::LevelRole).toString() == QStringLiteral("警告"),
                    "Qt warning level was not mapped to the log warning level");

    // 9. The window trims its own history and the file grows by day instead of rotating by size:
    //    a burst of long lines must not create a second numbered file.
    for (int index = 0; index < 80; ++index)
        LogService::write(LogLevel::Info, QStringLiteral("文件"),
                          QStringLiteral("每日文件测试行 %1 0123456789").arg(index));
    service.flush();
    QTest::qWait(200);
    require(model.rowCount() <= config.uiCapacity, "the window model exceeded its capacity");
    require(!QFile::exists(QFileInfo(basePath).absolutePath() + QStringLiteral("/visionrt_1.log")),
            "the daily file sink performed a size rotation");
    require(fileText(datedFile).contains(QStringLiteral("每日文件测试行 79")),
            "the daily file lost the newest line");

    // 10. A restart on the same day appends to the same file instead of truncating it.
    service.setViewActive(false);
    service.shutdown();
    service.start(config);
    LogService::write(LogLevel::Info, QStringLiteral("文件"), QStringLiteral("同日重启追加标记"));
    service.flush();
    QTest::qWait(200);
    const auto restarted = fileText(datedFile);
    require(restarted.contains(QStringLiteral("每日文件测试行 79")),
            "a same-day restart truncated the earlier file contents");
    require(restarted.contains(QStringLiteral("同日重启追加标记")),
            "a same-day restart did not append the new line");
    require(service.writeFailures() == 1, "a healthy log file reported additional write failures");

    // 11. The mailbox budget includes the drop summary: asking for one record after a saturation
    //     returns the summary alone instead of the summary plus a record.
    service.setViewActive(false);
    for (int index = 0; index < static_cast<int>(config.uiCapacity) * 2; ++index)
        LogService::write(LogLevel::Info, QStringLiteral("测试"), QStringLiteral("预算 %1").arg(index));
    QTest::qWait(100);
    const auto single = service.take(1);
    require(single.size() == 1, "the drop summary ignored the requested record budget");
    require(single.front().line.contains(QStringLiteral("丢弃")),
            "the first record of a saturated mailbox is not the drop summary");
    require(service.take(0).isEmpty(), "a zero budget still returned records");
    service.take(static_cast<std::size_t>(config.uiCapacity) * 2);

    // 12. A byte cap never splits a UTF-8 character: the kept prefix ends on a character boundary
    //     and the window never shows a replacement character.
    service.shutdown();
    auto narrow            = config;
    narrow.maxMessageBytes = 7;
    require(service.start(narrow), "the service refused to restart with a narrow cap");
    LogService::write(LogLevel::Info, QStringLiteral("测试"), QStringLiteral("中文中文"));
    require(QTest::qWaitFor(
                [&]
                {
                    for (const auto &record : service.take(64))
                        if (record.line.contains(QStringLiteral("中…")))
                            return true;
                    return false;
                },
                2000),
            "a multi-byte message was not capped on a character boundary");
    service.flush();
    QTest::qWait(100);
    const auto narrowFile = fileText(datedFile);
    require(!narrowFile.contains(QChar(0xFFFD)), "the daily file contains a truncated UTF-8 sequence");

    // 13. The window model trims the incoming batch before it removes history: a batch larger than
    //     the window keeps only the newest rows and never exceeds its capacity.
    LogModel small;
    small.setCapacity(1);
    small.append({LogRecord{QStringLiteral("旧"), QStringLiteral("信息")},
                  LogRecord{QStringLiteral("新"), QStringLiteral("信息")}});
    require(small.rowCount() == 1 && small.text(0) == QStringLiteral("新"),
            "a batch larger than the window did not keep only its newest row");
    small.setCapacity(2);
    small.append({LogRecord{QStringLiteral("一"), QStringLiteral("信息")},
                  LogRecord{QStringLiteral("二"), QStringLiteral("信息")},
                  LogRecord{QStringLiteral("三"), QStringLiteral("信息")}});
    require(small.rowCount() == 2 && small.text(0) == QStringLiteral("二") && small.text(1) == QStringLiteral("三"),
            "the window model exceeded or mis-trimmed its capacity");

    service.setViewActive(false);
    service.shutdown();
    require(!service.running(), "shutdown did not stop the service");
    LogService::write(LogLevel::Info, QStringLiteral("测试"), QStringLiteral("关闭后写入"));
    service.shutdown();
    require(service.writeFailures() == 1, "a healthy log file reported write failures");

    // 15. Shutdown while producers are writing: the barrier must let every in-flight post finish
    //     before the worker, the sinks and the logger are released.
    require(service.start(config), "the service refused to start for the concurrency check");
    std::atomic<bool>        stop{false};
    std::vector<std::thread> producers;
    for (int index = 0; index < 4; ++index)
        producers.emplace_back(
            [&stop]
            {
                while (!stop.load(std::memory_order_relaxed))
                    LogService::write(LogLevel::Info, QStringLiteral("并发"), QStringLiteral("关闭竞争"));
            });
    QTest::qWait(20);
    service.shutdown();
    stop.store(true, std::memory_order_relaxed);
    for (auto &thread : producers)
        thread.join();
    require(!service.running(), "shutdown under concurrent producers did not stop the service");

    // 14. Shutdown restores the handler that was installed before the service started. Qt's own
    //     default handler is a non-null pointer, so both the default and a custom handler are
    //     compared against the value actually observed instead of against nullptr.
    const auto baseline = qInstallMessageHandler(probeHandler);
    require(service.start(config), "the service refused to start for the handler restore check");
    service.shutdown();
    require(qInstallMessageHandler(baseline) == probeHandler, "shutdown left the Qt message bridge installed");
    qInstallMessageHandler(nullptr);
    require(service.start(config), "the service refused to restart for the default handler check");
    service.shutdown();
    require(qInstallMessageHandler(nullptr) == baseline, "shutdown did not restore the default Qt handler");

    // 15. Single-line bounded UTF-8 text truncation: table-driven matrix
    {
        struct MatrixCase {
            std::size_t cap;
            std::string_view text;
            std::string_view expected;
            bool truncated;
        };

        const MatrixCase cases[] = {
            {0, "", "", false},
            {0, "a", "", true},
            {1, "a", "a", false},
            {1, "ab", "a", true},
            {1, "中文", "", true},
            {2, "ab", "ab", false},
            {2, "abc", "ab", true},
            {2, "中文", "", true},
            {3, "abc", "abc", false},
            {3, "abcd", "…", true},
            {3, "中文", "…", true},
            {4, "中abc", "…", true},
            {5, "中abc", "…", true},
            {6, "中abcd", "中…", true},
            {6, "中abc", "中abc", false},
            {7, "中文abc", "中…", true},
            {6, "中文", "中文", false},
            {7, "中文", "中文", false},
            // 4-byte UTF-8 (Emoji 😀 = \xF0\x9F\x98\x80)
            {3, "\xF0\x9F\x98\x80", "…", true},
            {4, "\xF0\x9F\x98\x80", "\xF0\x9F\x98\x80", false},
            {5, "\xF0\x9F\x98\x80" "ab", "…", true},
            {5, "\xF0\x9F\x98\x80" "a", "\xF0\x9F\x98\x80" "a", false},
            {7, "\xF0\x9F\x98\x80" "abcd", "\xF0\x9F\x98\x80…", true},
            {7, "\xF0\x9F\x98\x80" "abc", "\xF0\x9F\x98\x80" "abc", false},
            {8, "\xF0\x9F\x98\x80" "abcde", "\xF0\x9F\x98\x80" "a…", true},
            // Escape sequences
            {2, "\n", "\\n", false},
            {3, "\r\n", "…", true},
            {4, "\r\n", "\\r\\n", false},
            {2, "\v", "\\v", false},
            {2, "\f", "\\f", false},
            {2, std::string_view("\0", 1), "\\0", false},
            // TAB & 0x1F control byte preservation
            {16, "a\tb", "a\tb", false},
            {16, "a\x1F" "b", "a\x1F" "b", false},
            // Malformed UTF-8 & newline leak prevention
            {16, "\xE2\x0A\x78", "?\\nx", false},
            // Capacity larger than message
            {4096, "abc", "abc", false},
        };

        for (std::size_t idx = 0; idx < sizeof(cases) / sizeof(cases[0]); ++idx)
        {
            const auto &c = cases[idx];
            const auto res = boundedSingleLine(c.text, c.cap);
            require(res.text == c.expected && res.truncated == c.truncated,
                    QString("boundedSingleLine case %1 failed: cap=%2 text='%3' got='%4' (trunc=%5)")
                        .arg(idx)
                        .arg(c.cap)
                        .arg(QString::fromUtf8(c.text.data(), static_cast<int>(c.text.size())))
                        .arg(QString::fromUtf8(res.text.data(), static_cast<int>(res.text.size())))
                        .arg(res.truncated)
                        .toUtf8()
                        .constData());
        }

        // Output sizing check: large string exact boundary and truncation
        std::string large(4096, 'x');
        const auto rLarge = boundedSingleLine(large, 4096);
        require(rLarge.text.size() == 4096 && !rLarge.truncated, "output sizing: 4096 byte string must bound accurately");
        const auto rLargeCut = boundedSingleLine(large, 100);
        require(rLargeCut.text.size() == 100 && rLargeCut.truncated, "output sizing: large string with 100 cap must truncate to 100 bytes");

        // Return string capacity check: capacity retains bounded buffer size
        // 1. Short text (3 bytes) with large cap (4096): capacity must stay small, not allocating 4096 bytes
        const auto rAllocShort = boundedSingleLine("abc", 4096);
        require(rAllocShort.text == "abc" && !rAllocShort.truncated, "return string capacity check: short text content check");
        require(rAllocShort.text.capacity() < 4096, "return string capacity check: short input with large cap must not pre-allocate 4096 bytes");

        // 2. Long text (4096 bytes) with small cap (100): capacity must stay small, not allocating 4096 bytes
        const auto rAllocLongCut = boundedSingleLine(large, 100);
        require(rAllocLongCut.text.size() == 100 && rAllocLongCut.truncated, "return string capacity check: long input with small cap content check");
        require(rAllocLongCut.text.capacity() < 4096, "return string capacity check: long input with small cap must not allocate full text size");

        // Isolated allocation probe: measure exact heap allocations during call
        // 1. Short text (3 bytes) with large cap (4096):
        // Input is 3 bytes, fits into SSO buffer or min(3, 4096) bytes.
        // Must NOT heap-allocate 4096 bytes (MSVC/STL environment).
        g_allocatedCount = 0;
        g_allocatedBytes = 0;
        g_trackAllocations = true;
        const auto probeShort = boundedSingleLine("abc", 4096);
        g_trackAllocations = false;
        require(probeShort.text == "abc" && !probeShort.truncated, "allocation probe: short text content check");
        require(g_allocatedBytes < 4096, "allocation probe: short input with large cap must not heap-allocate 4096 bytes");

        // 2. Long text (4096 bytes) with small cap (100):
        // Output is bounded to 100 bytes. Must NOT allocate 4096 bytes.
        g_allocatedCount = 0;
        g_allocatedBytes = 0;
        g_trackAllocations = true;
        const auto probeLongCut = boundedSingleLine(large, 100);
        g_trackAllocations = false;
        require(probeLongCut.text.size() == 100 && probeLongCut.truncated, "allocation probe: long input with small cap content check");
        require(g_allocatedBytes < 4096, "allocation probe: long input with small cap must not allocate full text size on heap");
    }

    // 16. UTF-8 boundary & invalid bytes: invalid byte sequences do not leak raw newlines and produce valid UTF-8
    service.shutdown();
    require(service.start(config), "the service refused to restart for UTF-8 check");
    service.setViewActive(true);
    std::string badSeq = "\xE2\x0A\x78";
    LogService::write(LogLevel::Info, std::string_view("测试"), std::string_view(badSeq));
    require(QTest::qWaitFor(
                [&]
                {
                    for (const auto &record : service.take(64))
                        if (record.line.contains(QStringLiteral("\\n")))
                            return true;
                    return false;
                },
                2000),
            "embedded newline in malformed UTF-8 was not escaped");

    // 17. Error coalescing matrix: Critical bypass, escape expansion, message & source anti-collision, budget threshold, and summary repeat
    {
        // 17a. Critical logs never coalesce or delay, and arrive at both UI and daily log file
        service.shutdown();
        QTemporaryDir critDir;
        const QString critLog = critDir.filePath("visionrt.log");
        require(service.start(config, critLog), "restart for critical check failed");
        service.setViewActive(true);
        for (int i = 0; i < 5; ++i)
            LogService::write(LogLevel::Critical, std::string_view("critical-probe"), std::string_view("panic payload"));
        int critCount = 0;
        require(QTest::qWaitFor(
                    [&]
                    {
                        for (const auto &record : service.take(64))
                            if (record.line.contains(QStringLiteral("critical-probe")) && record.line.contains(QStringLiteral("panic payload")))
                                ++critCount;
                        return critCount == 5;
                    },
                    2000),
                "critical records were coalesced or delayed in UI");
        service.flush();
        service.shutdown();
        const QString critTodayFile = QDir(critDir.path()).filePath(QString("visionrt_%1.log").arg(QDate::currentDate().toString("yyyy-MM-dd")));
        const QString critContent = fileText(critTodayFile);
        int critFileCount = 0;
        for (const auto &line : critContent.split('\n'))
            if (line.contains("critical-probe") && line.contains("panic payload"))
                ++critFileCount;
        require(critFileCount == 5, "critical records missing from disk file");

        // 17b. Escape expansion A/B & Message anti-collision (16-byte and 64-byte caps via writeError)
        // Part 1: Escape expansion boundedSingleLine baseline
        const auto resEsc8 = boundedSingleLine("\n\n\n\n\n", 8);
        require(resEsc8.truncated && resEsc8.text == "\\n\\n…", "escape expansion with cap 8 must truncate to \\n\\n…");
        const auto resEsc16 = boundedSingleLine("\n\n\n\n\n", 16);
        require(!resEsc16.truncated && resEsc16.text == "\\n\\n\\n\\n\\n", "escape expansion with cap 16 must fit without truncation");

        // Part 2: Escape expansion anti-collision via writeError under 16-byte budget
        // Messages A and B exceed 16 bytes after escaping and truncate to identical prefix + "…".
        // Service must deliver both distinct initial records without coalescing into repeat summaries.
        service.shutdown();
        QTemporaryDir esc16Dir;
        const QString esc16Log = esc16Dir.filePath("visionrt.log");
        auto cap16 = config;
        cap16.maxMessageBytes = 16;
        require(service.start(cap16, esc16Log), "restart for cap 16 escape collision check failed");
        service.setViewActive(true);
        std::string escA16 = std::string(8, '\n') + "AAAA";
        std::string escB16 = std::string(8, '\n') + "BBBB";
        service.error("esc16-collision-probe", escA16);
        service.error("esc16-collision-probe", escB16);
        service.flush();
        flushPendingForTesting();
        int esc16Count = 0;
        require(QTest::qWaitFor(
                    [&]
                    {
                        for (const auto &record : service.take(64))
                        {
                            if (record.line.contains(QStringLiteral("esc16-collision-probe")))
                            {
                                require(!record.line.contains(QStringLiteral("重复")) && !record.line.contains(QStringLiteral("×")),
                                        "cap 16 truncated escaped messages collided into repeat summary in UI");
                                ++esc16Count;
                            }
                        }
                        return esc16Count == 2;
                    },
                    2000),
                "cap 16 escaped messages failed to deliver both distinct initial records to UI");
        service.flush();
        service.shutdown();
        const QString esc16Today = QDir(esc16Dir.path()).filePath(QString("visionrt_%1.log").arg(QDate::currentDate().toString("yyyy-MM-dd")));
        const QString esc16Content = fileText(esc16Today);
        int esc16FileCount = 0;
        for (const auto &line : esc16Content.split('\n'))
            if (line.contains("esc16-collision-probe"))
                ++esc16FileCount;
        require(esc16FileCount == 2, "cap 16 escaped messages must produce 2 distinct lines on disk");

        // Part 3: Message anti-collision at 64 budget (both ASCII and escaped newlines)
        // At 64 budget, minCountBudget (22) threshold is exceeded.
        // Therefore, omitting the message truncation guard would erroneously coalesce these!
        QTemporaryDir cap64Dir;
        const QString cap64Log = cap64Dir.filePath("visionrt.log");
        auto cap64 = config;
        cap64.maxMessageBytes = 64;
        require(service.start(cap64, cap64Log), "restart for message collision check failed");
        service.setViewActive(true);
        std::string msgA = std::string(61, 'M') + "111111111";
        std::string msgB = std::string(61, 'M') + "222222222";
        service.error("msg-collision-probe", msgA);
        service.error("msg-collision-probe", msgB);

        // Escaped message collision at 64 budget
        std::string escA64 = std::string(30, '\n') + "111111111";
        std::string escB64 = std::string(30, '\n') + "222222222";
        service.error("esc64-collision-probe", escA64);
        service.error("esc64-collision-probe", escB64);

        service.flush();
        flushPendingForTesting();
        int msgCollisionCount = 0;
        int esc64CollisionCount = 0;
        require(QTest::qWaitFor(
                    [&]
                    {
                        for (const auto &record : service.take(64))
                        {
                            if (record.line.contains(QStringLiteral("msg-collision-probe")))
                            {
                                require(!record.line.contains(QStringLiteral("重复")) && !record.line.contains(QStringLiteral("×")),
                                        "truncated long messages collided into a repeat summary");
                                ++msgCollisionCount;
                            }
                            if (record.line.contains(QStringLiteral("esc64-collision-probe")))
                            {
                                require(!record.line.contains(QStringLiteral("重复")) && !record.line.contains(QStringLiteral("×")),
                                        "truncated escaped messages at 64 budget collided into a repeat summary");
                                ++esc64CollisionCount;
                            }
                        }
                        return msgCollisionCount == 2 && esc64CollisionCount == 2;
                    },
                    2000),
                "truncated messages failed to deliver both distinct initial records");
        service.flush();
        service.shutdown();
        const QString cap64Today = QDir(cap64Dir.path()).filePath(QString("visionrt_%1.log").arg(QDate::currentDate().toString("yyyy-MM-dd")));
        const QString cap64Content = fileText(cap64Today);
        int cap64AsciiFileCount = 0;
        int cap64EscFileCount = 0;
        for (const auto &line : cap64Content.split('\n'))
        {
            if (line.contains("msg-collision-probe")) ++cap64AsciiFileCount;
            if (line.contains("esc64-collision-probe")) ++cap64EscFileCount;
        }
        require(cap64AsciiFileCount == 2, "64 budget ASCII collision messages must produce 2 lines on disk");
        require(cap64EscFileCount == 2, "64 budget escaped collision messages must produce 2 lines on disk");

        // 17c. Truncated source anti-collision: two long sources sharing prefix but differing in tail
        service.shutdown();
        require(service.start(config), "restart for collision check failed");
        service.setViewActive(true);
        std::string longA = std::string(130, 'X') + "AAA";
        std::string longB = std::string(130, 'X') + "BBB";
        service.error(longA, "collision payload");
        service.error(longB, "collision payload");
        service.flush();
        flushPendingForTesting();
        int collisionCount = 0;
        require(QTest::qWaitFor(
                    [&]
                    {
                        for (const auto &record : service.take(64))
                        {
                            if (record.line.contains(QStringLiteral("collision payload")))
                            {
                                require(!record.line.contains(QStringLiteral("重复")) && !record.line.contains(QStringLiteral("×")),
                                        "truncated long sources collided into a repeat summary");
                                ++collisionCount;
                            }
                        }
                        return collisionCount == 2;
                    },
                    2000),
                "truncated long sources failed to deliver both distinct records");

        // 17d. Budget threshold 21 vs 22:
        // Budget 21 is under minCountBudget (22), so errors post directly without coalescing
        service.shutdown();
        auto cap21 = config;
        cap21.maxMessageBytes = 21;
        require(service.start(cap21), "restart for cap 21 failed");
        service.setViewActive(true);
        for (int i = 0; i < 3; ++i)
            service.error("cap21-probe", "threshold payload");
        service.flush();
        int cap21Count = 0;
        require(QTest::qWaitFor(
                    [&]
                    {
                        for (const auto &record : service.take(64))
                            if (record.line.contains(QStringLiteral("cap21-probe")))
                                ++cap21Count;
                        return cap21Count == 3;
                    },
                    2000),
                "cap 21 erroneously coalesced errors");

        // Budget 22 reaches minCountBudget (22), so repeats coalesce: 1 initial + 1 summary (exact repeat count 2)
        service.shutdown();
        QTemporaryDir cap22Dir;
        const QString cap22Log = cap22Dir.filePath("visionrt.log");
        auto cap22 = config;
        cap22.maxMessageBytes = 22;
        require(service.start(cap22, cap22Log), "restart for cap 22 failed");
        service.setViewActive(true);
        for (int i = 0; i < 3; ++i)
            service.error("cap22-probe", "1234567890");
        service.flush();
        flushPendingForTesting();
        int cap22Initial = 0;
        int cap22Summary = 0;
        require(QTest::qWaitFor(
                    [&]
                    {
                        for (const auto &record : service.take(64))
                        {
                            if (record.line.contains(QStringLiteral("cap22-probe")))
                            {
                                if (record.line.contains(QStringLiteral("（重复 2 次）")) || record.line.contains(QStringLiteral("×2")))
                                    ++cap22Summary;
                                else
                                    ++cap22Initial;
                            }
                        }
                        return cap22Initial == 1 && cap22Summary == 1;
                    },
                    2000),
                "cap 22 failed to coalesce into exact 1 initial + 1 summary (repeats=2)");
        service.flush();
        service.shutdown();
        const QString cap22Today = QDir(cap22Dir.path()).filePath(QString("visionrt_%1.log").arg(QDate::currentDate().toString("yyyy-MM-dd")));
        const QString cap22FileContent = fileText(cap22Today);
        int cap22FileLineCount = 0;
        for (const auto &line : cap22FileContent.split('\n'))
            if (line.contains("cap22-probe"))
                ++cap22FileLineCount;
        require(cap22FileLineCount == 2, "cap 22 disk file must contain exactly 2 lines (initial + summary)");

        // 17e. Negative witness: exactly 1 error across full refresh window produces NO summary indicators
        service.shutdown();
        QTemporaryDir negDir;
        const QString negLog = negDir.filePath("visionrt.log");
        require(service.start(config, negLog), "restart for negative witness failed");
        service.setViewActive(true);
        service.error("single-refresh-probe", "single event payload");
        service.flush();
        int probeCount = 0;
        require(QTest::qWaitFor(
                    [&]
                    {
                        for (const auto &rec : service.take(64))
                        {
                            if (rec.line.contains(QStringLiteral("single-refresh-probe")))
                            {
                                ++probeCount;
                                require(!rec.line.contains(QStringLiteral("重复")) && !rec.line.contains(QStringLiteral("×")),
                                        "single error event produced an unexpected repeat summary indicator");
                            }
                        }
                        return probeCount == 1;
                    },
                    2000),
                "expected initial error record for single error event");

        flushPendingForTesting();
        QTest::qWait(600); // Wait beyond 500ms flush timer
        for (const auto &rec : service.take(64))
        {
            if (rec.line.contains(QStringLiteral("single-refresh-probe")))
            {
                ++probeCount;
                require(!rec.line.contains(QStringLiteral("重复")) && !rec.line.contains(QStringLiteral("×")),
                        "unexpected repeat summary produced for single error event");
            }
        }
        require(probeCount == 1, "unexpected extra record appeared across refresh window in UI");
        service.flush();
        service.shutdown();
        const QString negToday = QDir(negDir.path()).filePath(QString("visionrt_%1.log").arg(QDate::currentDate().toString("yyyy-MM-dd")));
        const QString negFileContent = fileText(negToday);
        int negFileLineCount = 0;
        for (const auto &line : negFileContent.split('\n'))
            if (line.contains("single-refresh-probe"))
                ++negFileLineCount;
        require(negFileLineCount == 1, "single error event must produce exactly 1 line on disk");

        // 17f. 5 occurrences: first is immediate, next 4 produce repeat count 4 summary (UI and file consistent)
        service.shutdown();
        QTemporaryDir repDir;
        const QString repLog = repDir.filePath("visionrt.log");
        require(service.start(config, repLog), "restart for repeat summary failed");
        service.setViewActive(true);
        for (int i = 0; i < 5; ++i)
            service.error("rep-probe", "repeat event payload");
        service.flush();
        flushPendingForTesting();
        int repInitial = 0;
        int repSummary = 0;
        require(QTest::qWaitFor(
                    [&]
                    {
                        for (const auto &record : service.take(64))
                        {
                            if (record.line.contains(QStringLiteral("rep-probe")))
                            {
                                if (record.line.contains(QStringLiteral("（重复 4 次）")) || record.line.contains(QStringLiteral("×4")))
                                    ++repSummary;
                                else
                                    ++repInitial;
                            }
                        }
                        return repInitial == 1 && repSummary == 1;
                    },
                    2000),
                "5 identical errors did not produce exact 1 initial + 1 summary (repeats=4) in UI");
        service.flush();
        service.shutdown();
        const QString repToday = QDir(repDir.path()).filePath(QString("visionrt_%1.log").arg(QDate::currentDate().toString("yyyy-MM-dd")));
        const QString repFileContent = fileText(repToday);
        int repFileLineCount = 0;
        for (const auto &line : repFileContent.split('\n'))
            if (line.contains("rep-probe"))
                ++repFileLineCount;
        require(repFileLineCount == 2, "5 identical errors must produce exactly 2 lines on disk (initial + summary)");
    }

    // 18. Controlled life-cycle concurrency & lock interleaving (Scenarios A, B, C for Info and Error)
#if defined(VISIONRT_LOG_TEST_HOOKS) && VISIONRT_LOG_TEST_HOOKS
    struct ThreadScopeGuard {
        std::vector<std::thread*> threads;
        std::vector<std::function<void()>> unblockActions;
        ~ThreadScopeGuard() noexcept {
            for (auto &action : unblockActions) {
                try {
                    if (action) action();
                } catch (...) {}
            }
            for (auto *t : threads) {
                if (t && t->joinable()) {
                    try {
                        t->join();
                    } catch (...) {}
                }
            }
            setLogTestHooks({});
        }
    };

    auto runConcurrencyScenario = [&](LogLevel level, const char *levelName) {
        const std::string tag = std::string(levelName);

        // --- Scenario A: In-lock writer paused; shutdown barrier waits; coordination thread unblocks writer; shutdown finishes and drains
        {
            service.shutdown();
            QTemporaryDir dirA;
            dirA.setAutoRemove(false);
            const QString logPathA = dirA.filePath("visionrt.log");
            require(service.start(config, logPathA), "restart for Scenario A failed");
            service.setViewActive(true);

            std::promise<void> writerInLockPromise;
            auto writerInLockFuture = writerInLockPromise.get_future().share();
            std::promise<void> allowWriterFinishPromise;
            auto allowWriterFinishFuture = allowWriterFinishPromise.get_future().share();
            std::promise<void> shutdownBeforeLockPromise;
            auto shutdownBeforeLockFuture = shutdownBeforeLockPromise.get_future().share();

            std::atomic<bool> unblockTriggeredA{false};
            auto triggerUnblockA = [&]() {
                if (!unblockTriggeredA.exchange(true)) {
                    try { allowWriterFinishPromise.set_value(); } catch (...) {}
                }
            };

            std::mutex eventMutex;
            std::vector<std::string> events;
            auto recordEvent = [&](std::string ev) {
                std::lock_guard<std::mutex> lk(eventMutex);
                events.push_back(std::move(ev));
            };

            setLogTestHooks({
                nullptr, // onBeforeWriteLock
                [&](std::string_view source) {
                    if (source == "scenario-a-writer-" + tag) {
                        recordEvent("writer_inside_lock");
                        try { writerInLockPromise.set_value(); } catch (...) {}
                        const auto st = allowWriterFinishFuture.wait_for(std::chrono::seconds(5));
                        require(st == std::future_status::ready, "writer wait_for allowWriterFinish timed out");
                        recordEvent("writer_done_in_lock");
                    }
                },
                [&]() {
                    // onBeforeShutdownLock
                    recordEvent("shutdown_before_lock");
                    try { shutdownBeforeLockPromise.set_value(); } catch (...) {}
                },
                [&]() {
                    // onInsideShutdownLock (drain barrier entered)
                    recordEvent("shutdown_inside_lock");
                }
            });

            std::exception_ptr writerExA = nullptr;
            std::thread writerThread([&] {
                try {
                    const std::string src = "scenario-a-writer-" + tag;
                    const std::string msg = "in-lock payload " + tag;
                    if (level == LogLevel::Error)
                        service.error(src, msg);
                    else
                        service.info(src, msg);
                } catch (...) {
                    writerExA = std::current_exception();
                }
            });

            std::exception_ptr coordExA = nullptr;
            std::thread coordThread([&] {
                try {
                    const auto st = shutdownBeforeLockFuture.wait_for(std::chrono::seconds(5));
                    require(st == std::future_status::ready, "coord thread: shutdown failed to reach onBeforeShutdownLock");
                    recordEvent("coord_releasing_writer");
                    triggerUnblockA();
                } catch (...) {
                    coordExA = std::current_exception();
                    triggerUnblockA();
                }
            });

            ThreadScopeGuard guard;
            guard.threads.push_back(&writerThread);
            guard.threads.push_back(&coordThread);
            guard.unblockActions.push_back(triggerUnblockA);

            // 1. Wait until writer is confirmed inside write lock
            require(writerInLockFuture.wait_for(std::chrono::seconds(5)) == std::future_status::ready,
                    "Scenario A writer failed to reach insideLock hook");

            // 2. Shut down directly from the main thread (QObject owner thread -> 0 Qt timer warnings)
            service.shutdown();

            // 3. Join threads
            if (coordThread.joinable())
                coordThread.join();
            if (writerThread.joinable())
                writerThread.join();
            setLogTestHooks({});

            if (writerExA) std::rethrow_exception(writerExA);
            if (coordExA) std::rethrow_exception(coordExA);

            // 4. Verify deterministic ordering without sleep:
            // "writer_inside_lock" < "shutdown_before_lock" < "coord_releasing_writer" < "writer_done_in_lock" < "shutdown_inside_lock"
            {
                std::lock_guard<std::mutex> lk(eventMutex);
                auto posOf = [&](const std::string &name) -> int {
                    for (int i = 0; i < static_cast<int>(events.size()); ++i)
                        if (events[i] == name) return i;
                    return -1;
                };
                const int pWriterIn = posOf("writer_inside_lock");
                const int pShutBefore = posOf("shutdown_before_lock");
                const int pCoordRel = posOf("coord_releasing_writer");
                const int pWriterDone = posOf("writer_done_in_lock");
                const int pShutInside = posOf("shutdown_inside_lock");

                require(pWriterIn >= 0 && pShutBefore >= 0 && pCoordRel >= 0 && pWriterDone >= 0 && pShutInside >= 0,
                        "Scenario A: one or more critical synchronization events did not occur");
                require(pWriterIn < pShutBefore, "Scenario A: writer must enter lock before shutdown attempts lock");
                require(pShutBefore < pCoordRel, "Scenario A: shutdown must reach lock point before coord releases writer");
                require(pWriterDone < pShutInside, "Scenario A: writer must release shared lock before shutdown drain enters exclusive lock!");
            }

            // 5. Verify target line exists in disk file
            const QString todayFileA = QDir(dirA.path()).filePath(QString("visionrt_%1.log").arg(QDate::currentDate().toString("yyyy-MM-dd")));
            QFile fileA(todayFileA);
            require(fileA.open(QIODevice::ReadOnly), "Scenario A: log file must exist and be readable");
            const QString contentA = QString::fromUtf8(fileA.readAll());
            require(contentA.contains(QString::fromStdString("scenario-a-writer-" + tag))
                        && contentA.contains(QString::fromStdString("in-lock payload " + tag)),
                    "Scenario A: writer record lost during shutdown drain");
            QDir(dirA.path()).removeRecursively();
        }

        // --- Scenario B: Before-lock writer paused; shutdown finishes; release writer; writer sees closed service and drops
        {
            service.shutdown();
            QTemporaryDir dirB;
            dirB.setAutoRemove(false);
            const QString logPathB = dirB.filePath("visionrt.log");
            const QString todayFileB = QDir(dirB.path()).filePath(QString("visionrt_%1.log").arg(QDate::currentDate().toString("yyyy-MM-dd")));
            require(service.start(config, logPathB), "restart for Scenario B failed");
            service.setViewActive(true);

            std::promise<void> writerBeforeLockPromise;
            auto writerBeforeLockFuture = writerBeforeLockPromise.get_future().share();
            std::promise<void> allowWriterProceedPromise;
            auto allowWriterProceedFuture = allowWriterProceedPromise.get_future().share();

            std::atomic<bool> unblockB{false};
            auto triggerUnblockB = [&]() {
                if (!unblockB.exchange(true)) {
                    try { allowWriterProceedPromise.set_value(); } catch (...) {}
                }
            };

            setLogTestHooks({
                [&](std::string_view source) {
                    if (source == "scenario-b-writer-" + tag) {
                        try { writerBeforeLockPromise.set_value(); } catch (...) {}
                        const auto st = allowWriterProceedFuture.wait_for(std::chrono::seconds(5));
                        require(st == std::future_status::ready, "writer wait_for allowWriterProceed timed out");
                    }
                },
                nullptr, // onInsideWriteLock
                nullptr, // onBeforeShutdownLock
                nullptr  // onInsideShutdownLock
            });

            std::exception_ptr writerExB = nullptr;
            std::thread writerThread([&] {
                try {
                    const std::string src = "scenario-b-writer-" + tag;
                    const std::string msg = "late payload " + tag;
                    if (level == LogLevel::Error)
                        service.error(src, msg);
                    else
                        service.info(src, msg);
                } catch (...) {
                    writerExB = std::current_exception();
                }
            });

            ThreadScopeGuard guard;
            guard.threads.push_back(&writerThread);
            guard.unblockActions.push_back(triggerUnblockB);

            // 1. Wait for writer to reach before-lock hook
            require(writerBeforeLockFuture.wait_for(std::chrono::seconds(5)) == std::future_status::ready,
                    "Scenario B writer failed to reach beforeLock hook");

            // 2. Shut down service on main thread
            service.shutdown();
            require(!service.running(), "service must be stopped after shutdown");

            // 3. Release writer
            triggerUnblockB();

            // 4. Join writer
            if (writerThread.joinable())
                writerThread.join();
            setLogTestHooks({});

            if (writerExB) std::rethrow_exception(writerExB);

            // 5. Verify old file exists, is openable and readable, and does NOT contain the target line
            QFile fileB(todayFileB);
            require(fileB.open(QIODevice::ReadOnly), "Scenario B: old session file must exist and be readable");
            const QString contentB = QString::fromUtf8(fileB.readAll());
            require(!contentB.contains(QString::fromStdString("scenario-b-writer-" + tag)),
                    "Scenario B: late writer erroneously appended to old session log file");
            QDir(dirB.path()).removeRecursively();
        }

        // --- Scenario C: Cross-restart writer suspended before lock; session restarts to new file; release writer; record arrives in new session
        {
            service.shutdown();
            QTemporaryDir dirC1;
            dirC1.setAutoRemove(false);
            const QString logPathC1 = dirC1.filePath("visionrt.log");
            const QString todayFileC1 = QDir(dirC1.path()).filePath(QString("visionrt_%1.log").arg(QDate::currentDate().toString("yyyy-MM-dd")));
            require(service.start(config, logPathC1), "start for Scenario C old session failed");
            service.setViewActive(true);

            std::promise<void> writerWaitingPromise;
            auto writerWaitingFuture = writerWaitingPromise.get_future().share();
            std::promise<void> allowWriterResumePromise;
            auto allowWriterResumeFuture = allowWriterResumePromise.get_future().share();

            std::atomic<bool> unblockC{false};
            auto triggerUnblockC = [&]() {
                if (!unblockC.exchange(true)) {
                    try { allowWriterResumePromise.set_value(); } catch (...) {}
                }
            };

            setLogTestHooks({
                [&](std::string_view source) {
                    if (source == "scenario-c-writer-" + tag) {
                        try { writerWaitingPromise.set_value(); } catch (...) {}
                        const auto st = allowWriterResumeFuture.wait_for(std::chrono::seconds(5));
                        require(st == std::future_status::ready, "Scenario C writer wait_for resume timed out");
                    }
                },
                nullptr, // onInsideWriteLock
                nullptr, // onBeforeShutdownLock
                nullptr  // onInsideShutdownLock
            });

            std::exception_ptr writerExC = nullptr;
            std::thread writerThread([&] {
                try {
                    const std::string srcC = "scenario-c-writer-" + tag;
                    const std::string msgC = "restart payload " + tag;
                    if (level == LogLevel::Error)
                        service.error(srcC, msgC);
                    else
                        service.info(srcC, msgC);
                } catch (...) {
                    writerExC = std::current_exception();
                }
            });

            ThreadScopeGuard guard;
            guard.threads.push_back(&writerThread);
            guard.unblockActions.push_back(triggerUnblockC);

            // 1. Wait for writer to pass admission and reach before-lock hook in old session
            require(writerWaitingFuture.wait_for(std::chrono::seconds(5)) == std::future_status::ready,
                    "Scenario C: writer failed to reach beforeLock hook in old session");

            // 2. Main thread completes shutdown of Session 1
            service.shutdown();
            require(!service.running(), "service must be stopped after Session 1 shutdown");

            // 3. Main thread starts Session 2 pointing to a new, independent log file
            QTemporaryDir dirC2;
            dirC2.setAutoRemove(false);
            const QString logPathC2 = dirC2.filePath("visionrt.log");
            const QString todayFileC2 = QDir(dirC2.path()).filePath(QString("visionrt_%1.log").arg(QDate::currentDate().toString("yyyy-MM-dd")));
            require(service.start(config, logPathC2), "start for Scenario C new session failed");
            service.setViewActive(true);

            // Clear test hooks so writer doesn't hit hook again upon resume
            setLogTestHooks({});

            // 4. Release writer! Writer resumes, takes write lock on Session 2, and posts
            triggerUnblockC();

            if (writerThread.joinable())
                writerThread.join();

            if (writerExC) std::rethrow_exception(writerExC);

            // 5. Drain new session
            service.flush();
            flushPendingForTesting();

            const std::string srcC = "scenario-c-writer-" + tag;
            const std::string msgC = "restart payload " + tag;

            // UI check in new session: exactly one record
            int cUiCount = 0;
            require(QTest::qWaitFor(
                        [&] {
                            for (const auto &rec : service.take(64))
                                if (rec.line.contains(QString::fromStdString(srcC)) && rec.line.contains(QString::fromStdString(msgC)))
                                    ++cUiCount;
                            return cUiCount == 1;
                        },
                        2000),
                    "Scenario C: restarted session failed to receive record in UI");

            service.flush();
            service.shutdown();

            // 6. Disk file checks:
            // Old file must NOT contain the target record
            QFile fileC1(todayFileC1);
            require(fileC1.open(QIODevice::ReadOnly), "Scenario C: old session file must exist and be readable");
            const QString contentC1 = QString::fromUtf8(fileC1.readAll());
            require(!contentC1.contains(QString::fromStdString(srcC)),
                    "Scenario C: old session file erroneously received late record");

            // New file MUST contain exactly one instance of the target record
            QFile fileC2(todayFileC2);
            require(fileC2.open(QIODevice::ReadOnly), "Scenario C: new session file must exist and be readable");
            const QString contentC2 = QString::fromUtf8(fileC2.readAll());
            int cFileCount = 0;
            for (const auto &line : contentC2.split('\n')) {
                if (line.contains(QString::fromStdString(srcC)) && line.contains(QString::fromStdString(msgC)))
                    ++cFileCount;
            }
            require(cFileCount == 1, "Scenario C: new session file must contain exactly one target record");

            QDir(dirC1.path()).removeRecursively();
            QDir(dirC2.path()).removeRecursively();
        }
    };

    runConcurrencyScenario(LogLevel::Info, "info");
    runConcurrencyScenario(LogLevel::Error, "error");
#endif
    
    std::cout << "Log contracts passed: single-line text, daily file in the software directory, append on restart, "
                 "bounded mailbox, batching, drops, coalescing, Qt bridge and honest degradation\n";
    return 0;
}
catch (const std::exception &error)
{
    std::cerr << error.what() << '\n';
    return 1;
}
