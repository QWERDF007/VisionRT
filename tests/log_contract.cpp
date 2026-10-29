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

    // 15. Single-line bounded UTF-8 text truncation: prefix token bounds and true prefix guarantee
    {
        const auto r1 = boundedSingleLine("中abc", 4);
        require(r1.text == "…" && r1.truncated, "中abc with cap 4 must truncate to …");

        const auto r2 = boundedSingleLine("中文abc", 7);
        require(r2.text == "中…" && r2.truncated, "中文abc with cap 7 must truncate to 中…");

        const auto r3 = boundedSingleLine("中文", 6);
        require(r3.text == "中文" && !r3.truncated, "中文 with cap 6 must not truncate");

        const auto r4 = boundedSingleLine("\xE2\x0A\x78", 16);
        require(r4.text == "?\\nx" && !r4.truncated, "embedded newline in malformed UTF-8 must escape without leaking raw newline");

        const auto r5 = boundedSingleLine("abc", 4096);
        require(r5.text == "abc" && !r5.truncated, "short text within cap must not truncate");
    }

    // 16. UTF-8 boundary & invalid bytes: invalid byte sequences do not leak raw newlines and produce valid UTF-8
    service.shutdown();
    require(service.start(config), "the service refused to restart for UTF-8 check");
    service.setViewActive(true);
    std::string badSeq = "\xE2\x0A\x78";
    LogService::write(LogLevel::Info, "测试", badSeq);
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

    // 17. Repetitive long error preserves repeat count in summary with unbiased probe source
    service.shutdown();
    auto tight = config;
    tight.maxMessageBytes = 32;
    require(service.start(tight), "failed to restart with tight cap");
    service.setViewActive(true);
    std::string probeSource = "summary-probe";
    std::string repeatMsg = "Repeat error message test";

    // 17a. Negative witness: feed exactly 1 record -> must NOT contain repeat count indicators
    service.error(probeSource, repeatMsg);
    service.flush();
    require(QTest::qWaitFor(
                [&]
                {
                    const auto records = service.take(64);
                    for (const auto &record : records)
                    {
                        if (record.line.contains(QStringLiteral("summary-probe")))
                        {
                            require(!record.line.contains(QStringLiteral("重复")) && !record.line.contains(QStringLiteral("×")),
                                    "first record unexpectedly contains repeat indicator");
                            return true;
                        }
                    }
                    return false;
                },
                2000),
            "first error record was not posted");

    // 17b. Now repeat 4 more times (total 5 occurrences): summary must report repeat count
    for (int rep = 0; rep < 4; ++rep)
        service.error(probeSource, repeatMsg);
    service.flush();
    require(QTest::qWaitFor(
                [&]
                {
                    for (const auto &record : service.take(64))
                        if (record.line.contains(QStringLiteral("（重复 4 次）")) || record.line.contains(QStringLiteral("×4")))
                            return true;
                    return false;
                },
                2000),
            "repeat count was lost or truncated away");

    // 18. Controlled late writer and restart barrier: concurrent writer does not deadlock during restart
    {
        std::atomic<bool> workerArrived{false};
        std::atomic<bool> allowPost{false};
        std::atomic<bool> workerFinished{false};

        std::thread lateWriter([&] {
            workerArrived.store(true, std::memory_order_release);
            while (!allowPost.load(std::memory_order_acquire))
                std::this_thread::yield();
            LogService::write(LogLevel::Info, std::string_view("late-writer"), std::string_view("in-flight record"));
            workerFinished.store(true, std::memory_order_release);
        });

        while (!workerArrived.load(std::memory_order_acquire))
            std::this_thread::yield();

        service.shutdown();
        require(service.start(config), "service failed to restart during concurrent late writer check");
        allowPost.store(true, std::memory_order_release);
        lateWriter.join();
        require(workerFinished.load(std::memory_order_acquire), "late writer did not finish cleanly");
    }

    std::cout << "Log contracts passed: single-line text, daily file in the software directory, append on restart, "
                 "bounded mailbox, batching, drops, coalescing, Qt bridge and honest degradation\n";
    return 0;
}
catch (const std::exception &error)
{
    std::cerr << error.what() << '\n';
    return 1;
}
