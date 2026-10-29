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
#include <iostream>
#include <stdexcept>
#include <thread>
#include <vector>

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

        // Allocation observation: short string within cap vs 4096-byte string
        std::string large(4096, 'x');
        const auto rLarge = boundedSingleLine(large, 4096);
        require(rLarge.text.size() == 4096 && !rLarge.truncated, "4096 byte string must bound accurately");
        const auto rLargeCut = boundedSingleLine(large, 100);
        require(rLargeCut.text.size() == 100 && rLargeCut.truncated, "large string with 100 cap must truncate to 100 bytes");
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

    // 17. Error coalescing matrix: Critical bypass, escape expansion, anti-collision, budget threshold, and summary repeat
    {
        // 17a. Critical logs never coalesce or delay
        service.shutdown();
        require(service.start(config), "restart for critical check failed");
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
                "critical records were coalesced or delayed");

        // 17b. Escape expansion A/B: budget 8 vs 16 with raw 5 newlines expanding to 10 bytes "\\n\\n\\n\\n\\n"
        const auto resEsc8 = boundedSingleLine("\n\n\n\n\n", 8);
        require(resEsc8.truncated && resEsc8.text == "\\n\\n…", "escape expansion with cap 8 must truncate to \\n\\n…");
        const auto resEsc16 = boundedSingleLine("\n\n\n\n\n", 16);
        require(!resEsc16.truncated && resEsc16.text == "\\n\\n\\n\\n\\n", "escape expansion with cap 16 must fit without truncation");

        // 17c. Truncated source anti-collision: two long sources sharing prefix but differing in tail
        service.shutdown();
        require(service.start(config), "restart for collision check failed");
        service.setViewActive(true);
        std::string longA = std::string(130, 'X') + "AAA";
        std::string longB = std::string(130, 'X') + "BBB";
        service.error(longA, "collision payload");
        service.error(longB, "collision payload");
        service.flush();
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

        // Budget 22 reaches minCountBudget (22), so repeats coalesce
        service.shutdown();
        auto cap22 = config;
        cap22.maxMessageBytes = 22;
        require(service.start(cap22), "restart for cap 22 failed");
        service.setViewActive(true);
        for (int i = 0; i < 3; ++i)
            service.error("cap22-probe", "threshold payload");
        service.flush();
        service.flushPending();
        bool cap22Summary = false;
        require(QTest::qWaitFor(
                    [&]
                    {
                        for (const auto &record : service.take(64))
                            if (record.line.contains(QStringLiteral("cap22-probe")) && (record.line.contains(QStringLiteral("重复")) || record.line.contains(QStringLiteral("×"))))
                                cap22Summary = true;
                        return cap22Summary;
                    },
                    2000),
                "cap 22 failed to coalesce and emit repeat count summary");

        // 17e. Negative witness: exactly 1 error across full refresh window produces NO summary indicators
        service.shutdown();
        require(service.start(config), "restart for negative witness failed");
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

        service.flushPending();
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
        require(probeCount == 1, "unexpected extra record appeared across refresh window");

        // 17f. 5 occurrences: first is immediate, next 4 produce repeat count 4 summary
        service.shutdown();
        require(service.start(config), "restart for repeat summary failed");
        service.setViewActive(true);
        for (int i = 0; i < 5; ++i)
            service.error("rep-probe", "repeat event payload");
        service.flush();
        service.flushPending();
        bool foundSummary = false;
        require(QTest::qWaitFor(
                    [&]
                    {
                        for (const auto &record : service.take(64))
                            if (record.line.contains(QStringLiteral("rep-probe"))
                                && (record.line.contains(QStringLiteral("（重复 4 次）")) || record.line.contains(QStringLiteral("×4"))))
                                foundSummary = true;
                        return foundSummary;
                    },
                    2000),
                "5 identical errors did not produce repeat count 4 summary");
    }

    // 18. Controlled life-cycle concurrency & lock interleaving (Scenarios A & B)
#if defined(VISIONRT_LOG_TEST_HOOKS) && VISIONRT_LOG_TEST_HOOKS
    // Scenario A: In-lock writer paused; shutdown waits; release writer; shutdown finishes and records drained
    {
        service.shutdown();
        require(service.start(config, directory.filePath("visionrt.log")), "restart for Scenario A failed");
        service.setViewActive(true);

        std::atomic<bool> writerInsideLock{false};
        std::atomic<bool> allowWriterFinish{false};
        std::atomic<bool> shutdownStarted{false};
        std::atomic<bool> shutdownCompleted{false};
        std::atomic<bool> drainPremature{false};
        std::condition_variable cvWriter;
        std::mutex mtxWriter;

        setLogTestHooks({
            nullptr, // onBeforeWriteLock
            [&](std::string_view source) {
                if (source == "scenario-a-writer") {
                    writerInsideLock.store(true, std::memory_order_release);
                    std::unique_lock<std::mutex> lk(mtxWriter);
                    cvWriter.wait_for(lk, std::chrono::seconds(5), [&] {
                        return allowWriterFinish.load(std::memory_order_acquire);
                    });
                }
            }
        });

        std::thread writerThread([&] {
            LogService::write(LogLevel::Info, std::string_view("scenario-a-writer"), std::string_view("in-lock payload"));
        });

        const auto startWait = std::chrono::steady_clock::now();
        while (!writerInsideLock.load(std::memory_order_acquire))
        {
            if (std::chrono::steady_clock::now() - startWait > std::chrono::seconds(3))
                break;
            std::this_thread::yield();
        }
        require(writerInsideLock.load(std::memory_order_acquire), "Scenario A writer failed to reach insideLock hook");

        std::thread coordinatorThread([&] {
            while (!shutdownStarted.load(std::memory_order_acquire))
                std::this_thread::yield();
            std::this_thread::sleep_for(std::chrono::milliseconds(80));
            if (shutdownCompleted.load(std::memory_order_acquire))
                drainPremature.store(true, std::memory_order_release);
            {
                std::lock_guard<std::mutex> lk(mtxWriter);
                allowWriterFinish.store(true, std::memory_order_release);
            }
            cvWriter.notify_all();
        });

        shutdownStarted.store(true, std::memory_order_release);
        service.shutdown();
        shutdownCompleted.store(true, std::memory_order_release);

        writerThread.join();
        coordinatorThread.join();
        setLogTestHooks({});

        require(!drainPremature.load(std::memory_order_acquire),
                "service shutdown completed prematurely while writer held shared write lock");

        // Verify the record was successfully drained to the daily log file before shutdown closed it
        const QString todayFile = QDir(directory.path()).filePath(QString("visionrt_%1.log").arg(QDate::currentDate().toString("yyyy-MM-dd")));
        QFile file(todayFile);
        require(file.open(QIODevice::ReadOnly | QIODevice::Text), "failed to open daily log file to verify Scenario A payload");
        const QString fileContent = QString::fromUtf8(file.readAll());
        require(fileContent.contains(QStringLiteral("scenario-a-writer")) && fileContent.contains(QStringLiteral("in-lock payload")),
                "in-lock writer record was lost during shutdown drain");
    }

    // Scenario B: Before-lock writer paused; shutdown finishes; release writer; writer sees closed service and safely aborts
    {
        require(service.start(config, directory.filePath("visionrt.log")), "restart for Scenario B failed");
        service.setViewActive(true);

        std::atomic<bool> writerBeforeLock{false};
        std::atomic<bool> allowWriterProceed{false};
        std::condition_variable cvBefore;
        std::mutex mtxBefore;

        setLogTestHooks({
            [&](std::string_view source) {
                if (source == "scenario-b-writer") {
                    writerBeforeLock.store(true, std::memory_order_release);
                    std::unique_lock<std::mutex> lk(mtxBefore);
                    cvBefore.wait_for(lk, std::chrono::seconds(5), [&] {
                        return allowWriterProceed.load(std::memory_order_acquire);
                    });
                }
            },
            nullptr // onInsideWriteLock
        });

        std::thread writerThread([&] {
            LogService::write(LogLevel::Info, std::string_view("scenario-b-writer"), std::string_view("late payload"));
        });

        const auto startWait = std::chrono::steady_clock::now();
        while (!writerBeforeLock.load(std::memory_order_acquire))
        {
            if (std::chrono::steady_clock::now() - startWait > std::chrono::seconds(3))
                break;
            std::this_thread::yield();
        }
        require(writerBeforeLock.load(std::memory_order_acquire), "Scenario B writer failed to reach beforeLock hook");

        service.shutdown();
        require(!service.running(), "service must be stopped after shutdown");

        {
            std::lock_guard<std::mutex> lk(mtxBefore);
            allowWriterProceed.store(true, std::memory_order_release);
        }
        cvBefore.notify_all();

        writerThread.join();
        setLogTestHooks({});

        require(service.start(config, directory.filePath("visionrt.log")), "restart after Scenario B failed");
        service.setViewActive(true);
        service.flush();
        const auto recordsAfter = service.take(64);
        for (const auto &rec : recordsAfter)
        {
            require(!rec.line.contains(QStringLiteral("scenario-b-writer")),
                    "late writer in closed session incorrectly posted to restarted session");
        }
    }
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
