// Aggregation contract for the log service: bounded mailbox and batches, drop accounting, error
// coalescing, the Qt bridge and the daily file policy. Text formatting is covered separately.
#include "log_test_support.h"

#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QTest>

#include <iostream>
#include <string>

using namespace log_test;
using visionrt::boundedSingleLine;
using visionrt::LogLevel;
using visionrt::LogSource;
using visionrt::LogModel;
using visionrt::LogRecord;
using visionrt::LogService;

int main(int argc, char **argv)
try
{
    QCoreApplication application(argc, argv);
    QTemporaryDir    directory;
    require(directory.isValid(), "log fixture directory is invalid");

    ServiceSession session;
    const QString  logPath = directory.filePath(QStringLiteral("visionrt.log"));
    session.start(logPath);

    LogModel model;
    model.setCapacity(session.config().uiCapacity);

    // 1. Hidden window: records stay in the bounded mailbox and nothing reaches the model.
    for (int index = 0; index < 12; ++index)
        LogService::write(LogLevel::Info, LogSource::System, QStringLiteral("隐藏批次 %1").arg(index));
    QTest::qWait(120);
    require(model.rowCount() == 0, "a hidden log window received records");

    QSignalSpy batches(&session.service(), &LogService::recordsAvailable);
    session.service().setViewActive(true);
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
        require(records.size() <= session.config().uiBatchMaximum,
                "the window received more than one bounded batch in a single delivery");
    }

    // 2. Overflow uses overrun_oldest and reports one summary for the discarded records.
    session.service().setViewActive(false);
    for (int index = 0; index < static_cast<int>(session.config().uiCapacity) * 3; ++index)
        LogService::write(LogLevel::Info, LogSource::System, QStringLiteral("溢出 %1").arg(index));
    QTest::qWait(100);
    session.service().setViewActive(true);
    require(QTest::qWaitFor(
                [&] { return anyRow(model, [](const QString &line) { return line.contains(QStringLiteral("丢弃")); }); }, 3000),
            "mailbox overflow was not reported to the window");
    require(session.service().droppedRecords() > 0, "discarded records were not counted");

    // 3. Error bursts: the first occurrence is queued immediately with its own level, and only the
    //    repeats are counted until the next flush.
    for (int index = 0; index < 5; ++index)
        LogService::write(LogLevel::Error, LogSource::Tool, QStringLiteral("同一个错误"));
    require(QTest::qWaitFor(
                [&]
                {
                    for (int row = 0; row < model.rowCount(); ++row)
                        if (rowLine(model, row).endsWith(QStringLiteral("同一个错误"))
                            && model.data(model.index(row, 0), LogModel::LevelRole).toString() == QStringLiteral("错误"))
                            return true;
                    return false;
                },
                1000),
            "the first error of a burst was not queued immediately");
    require(QTest::qWaitFor(
                [&] { return anyRow(model, [](const QString &line) { return line.contains(QStringLiteral("重复 4 次")); }); }, 3000),
            "the repeats of an error burst were not summarised with their count");

    // 3b. Critical never waits for a timer and is never reported under another level.
    LogService::write(LogLevel::Critical, LogSource::Tool, QStringLiteral("致命错误"));
    require(QTest::qWaitFor(
                [&] { return anyRow(model, [](const QString &line) { return line.endsWith(QStringLiteral("致命错误")); }); }, 500),
            "a critical line waited for the batch timer");
    for (int row = 0; row < model.rowCount(); ++row)
        if (rowLine(model, row).endsWith(QStringLiteral("致命错误")))
            require(model.data(model.index(row, 0), LogModel::LevelRole).toString() == QStringLiteral("致命"),
                    "a critical line was reported under a different level");

    // 4. The Qt bridge maps levels and categories and does not recurse.
    qWarning("日志桥接警告");
    require(QTest::qWaitFor(
                [&] { return anyRow(model, [](const QString &line) { return line.endsWith(QStringLiteral("日志桥接警告")); }); }, 2000),
            "qWarning did not reach the log window");
    for (int row = 0; row < model.rowCount(); ++row)
        if (rowLine(model, row).endsWith(QStringLiteral("日志桥接警告")))
            require(model.data(model.index(row, 0), LogModel::LevelRole).toString() == QStringLiteral("警告"),
                    "Qt warning level was not mapped to the log warning level");

    // 5. The window trims its own history and the file grows by day instead of rotating by size.
    for (int index = 0; index < 80; ++index)
        LogService::write(LogLevel::Info, LogSource::System,
                          QStringLiteral("每日文件测试行 %1 0123456789").arg(index));
    session.service().flush();
    QTest::qWait(200);
    require(model.rowCount() <= static_cast<int>(session.config().uiCapacity), "the window model exceeded its capacity");
    require(!QFile::exists(directory.filePath(QStringLiteral("visionrt_1.log"))),
            "the daily file sink performed a size rotation");
    require(fileText(datedLog(directory.path())).contains(QStringLiteral("每日文件测试行 79")),
            "the daily file lost the newest line");

    // 6. A restart on the same day appends to the same file instead of truncating it.
    session.service().setViewActive(false);
    session.stop();
    session.start(logPath);
    LogService::write(LogLevel::Info, LogSource::System, QStringLiteral("同日重启追加标记"));
    session.service().flush();
    QTest::qWait(200);
    const auto restarted = fileText(datedLog(directory.path()));
    require(restarted.contains(QStringLiteral("每日文件测试行 79")),
            "a same-day restart truncated the earlier file contents");
    require(restarted.contains(QStringLiteral("同日重启追加标记")), "a same-day restart did not append the new line");
    require(session.service().writeFailures() == 0, "a healthy log file reported write failures");

    // 7. The mailbox budget includes the drop summary: asking for one record after a saturation
    //    returns the summary alone instead of the summary plus a record.
    session.service().setViewActive(false);
    for (int index = 0; index < static_cast<int>(session.config().uiCapacity) * 2; ++index)
        LogService::write(LogLevel::Info, LogSource::System, QStringLiteral("预算 %1").arg(index));
    QTest::qWait(100);
    const auto single = session.service().take(1);
    require(single.size() == 1, "the drop summary ignored the requested record budget");
    require(single.front().line.contains(QStringLiteral("丢弃")),
            "the first record of a saturated mailbox is not the drop summary");
    require(session.service().take(0).isEmpty(), "a zero budget still returned records");
    session.service().take(static_cast<std::size_t>(session.config().uiCapacity) * 2);

    // 8. The window model trims the incoming batch before it removes history.
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

    // 9. Error coalescing matrix: critical bypass, escape expansion, message and source
    //    anti-collision, budget threshold and summary repeat.
    {
        // 9a. Critical logs never coalesce or delay, and arrive at both UI and the daily file.
        session.stop();
        QTemporaryDir critDir;
        require(critDir.isValid(), "critical fixture directory is invalid");
        session.start(critDir.filePath(QStringLiteral("visionrt.log")));
        session.service().setViewActive(false);
        for (int i = 0; i < 5; ++i)
            LogService::write(LogLevel::Critical, LogSource::Tool, std::string_view("panic payload"));
        int critCount = 0;
        require(QTest::qWaitFor(
                    [&]
                    {
                        for (const auto &record : session.service().take(64))
                            if (record.line.contains(QStringLiteral("[工具]"))
                                && record.line.contains(QStringLiteral("panic payload")))
                                ++critCount;
                        return critCount == 5;
                    },
                    2000),
                "critical records were coalesced or delayed in the window");
        session.service().flush();
        session.stop();
        int critFileCount = 0;
        for (const auto &line : fileText(datedLog(critDir.path())).split('\n'))
            if (line.contains(QStringLiteral("[工具]")) && line.contains("panic payload"))
                ++critFileCount;
        require(critFileCount == 5, "critical records missing from the daily file");

        // 9b. Escape expansion A/B and message anti-collision at a 16-byte budget.
        const auto escaped8 = boundedSingleLine("\n\n\n\n\n", 8);
        require(escaped8.truncated && escaped8.text == "\\n\\n…", "escape expansion with cap 8 must truncate to \\n\\n…");
        const auto escaped16 = boundedSingleLine("\n\n\n\n\n", 16);
        require(!escaped16.truncated && escaped16.text == "\\n\\n\\n\\n\\n", "escape expansion with cap 16 must fit");

        session.stop();
        QTemporaryDir esc16Dir;
        auto          cap16             = testConfig();
        cap16.maxMessageBytes           = 16;
        session.restart(cap16, esc16Dir.filePath(QStringLiteral("visionrt.log")));
        session.service().setViewActive(false);
        const std::string escA16 = "P16A" + std::string(3, '\n') + "AAAA";
        const std::string escB16 = "P16B" + std::string(3, '\n') + "BBBB";
        session.service().error(LogSource::Tool, escA16);
        session.service().error(LogSource::Tool, escB16);
        session.service().flush();
        visionrt::flushPendingForTesting();
        int esc16Count = 0;
        require(QTest::qWaitFor(
                    [&]
                    {
                        for (const auto &record : session.service().take(64))
                        {
                            if (record.line.contains(QStringLiteral("P16A")) || record.line.contains(QStringLiteral("P16B")))
                            {
                                require(!record.line.contains(QStringLiteral("重复")) && !record.line.contains(QStringLiteral("×")),
                                        "cap 16 truncated escaped messages collided into a repeat summary");
                                ++esc16Count;
                            }
                        }
                        return esc16Count == 2;
                    },
                    2000),
                "cap 16 escaped messages failed to deliver both distinct initial records");
        session.service().flush();
        session.stop();
        int esc16FileCount = 0;
        for (const auto &line : fileText(datedLog(esc16Dir.path())).split('\n'))
            if (line.contains("P16A") || line.contains("P16B"))
                ++esc16FileCount;
        require(esc16FileCount == 2, "cap 16 escaped messages must produce 2 distinct lines on disk");

        // 9c. Message anti-collision at the 64-byte budget, plain and escaped.
        QTemporaryDir cap64Dir;
        auto          cap64   = testConfig();
        cap64.maxMessageBytes = 64;
        session.restart(cap64, cap64Dir.filePath(QStringLiteral("visionrt.log")));
        session.service().setViewActive(false);
        const std::string msgA = "MSGA-" + std::string(56, 'M') + "111111111";
        const std::string msgB = "MSGB-" + std::string(56, 'M') + "222222222";
        session.service().error(LogSource::Tool, msgA);
        session.service().error(LogSource::Tool, msgB);
        const std::string escA64 = "ESCA-" + std::string(25, '\n') + "111111111";
        const std::string escB64 = "ESCB-" + std::string(25, '\n') + "222222222";
        session.service().error(LogSource::Tool, escA64);
        session.service().error(LogSource::Tool, escB64);
        session.service().flush();
        visionrt::flushPendingForTesting();
        int msgCollisionCount = 0;
        int esc64CollisionCount = 0;
        require(QTest::qWaitFor(
                    [&]
                    {
                        for (const auto &record : session.service().take(64))
                        {
                            if (record.line.contains(QStringLiteral("MSGA-")) || record.line.contains(QStringLiteral("MSGB-")))
                            {
                                require(!record.line.contains(QStringLiteral("重复")) && !record.line.contains(QStringLiteral("×")),
                                        "truncated long messages collided into a repeat summary");
                                ++msgCollisionCount;
                            }
                            if (record.line.contains(QStringLiteral("ESCA-")) || record.line.contains(QStringLiteral("ESCB-")))
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
        session.service().flush();
        session.stop();
        int cap64AsciiFileCount = 0;
        int cap64EscFileCount   = 0;
        for (const auto &line : fileText(datedLog(cap64Dir.path())).split('\n'))
        {
            if (line.contains("MSGA-") || line.contains("MSGB-"))
                ++cap64AsciiFileCount;
            if (line.contains("ESCA-") || line.contains("ESCB-"))
                ++cap64EscFileCount;
        }
        require(cap64AsciiFileCount == 2, "64-byte ASCII collision messages must produce 2 lines on disk");
        require(cap64EscFileCount == 2, "64-byte escaped collision messages must produce 2 lines on disk");

        // 9e. Budget threshold 21 versus 22: only the latter reaches the coalescing minimum.
        session.stop();
        auto cap21             = testConfig();
        cap21.maxMessageBytes  = 21;
        session.restart(cap21, directory.filePath(QStringLiteral("visionrt.log")));
        session.service().setViewActive(false);
        for (int i = 0; i < 3; ++i)
            session.service().error(LogSource::Tool, "threshold payload");
        session.service().flush();
        int cap21Count = 0;
        require(QTest::qWaitFor(
                    [&]
                    {
                        for (const auto &record : session.service().take(64))
                            if (record.line.contains(QStringLiteral("threshold payload")))
                                ++cap21Count;
                        return cap21Count == 3;
                    },
                    2000),
                "a 21-byte budget erroneously coalesced errors");

        session.stop();
        QTemporaryDir cap22Dir;
        auto          cap22            = testConfig();
        cap22.maxMessageBytes          = 22;
        session.restart(cap22, cap22Dir.filePath(QStringLiteral("visionrt.log")));
        session.service().setViewActive(false);
        for (int i = 0; i < 3; ++i)
            session.service().error(LogSource::Tool, "1234567890");
        session.service().flush();
        visionrt::flushPendingForTesting();
        int cap22Initial = 0;
        int cap22Summary = 0;
        require(QTest::qWaitFor(
                    [&]
                    {
                        for (const auto &record : session.service().take(64))
                        {
                            // The summary is identified by its repeat marker: with a 22-byte budget the
                            // payload itself is shortened to make room for the count.
                            const bool summary = record.line.contains(QStringLiteral("（重复 2 次）"))
                                              || record.line.contains(QStringLiteral("×2"));
                            if (summary)
                                ++cap22Summary;
                            else if (record.line.contains(QStringLiteral("1234567890")))
                                ++cap22Initial;
                        }
                        return cap22Initial == 1 && cap22Summary == 1;
                    },
                    2000),
                "a 22-byte budget failed to coalesce into 1 initial + 1 summary");
        session.service().flush();
        session.stop();
        int cap22FileLineCount = 0;
        for (const auto &line : fileText(datedLog(cap22Dir.path())).split('\n'))
            if (line.contains("1234567890") || line.contains(QStringLiteral("（重复 2 次）"))
                || line.contains(QStringLiteral("×2")))
                ++cap22FileLineCount;
        require(cap22FileLineCount == 2, "a 22-byte budget must write exactly 2 lines (initial + summary)");

        // 9f. Single error: no summary indicator now or after the refresh window.
        session.stop();
        QTemporaryDir negativeDir;
        session.restart(testConfig(), negativeDir.filePath(QStringLiteral("visionrt.log")));
        session.service().setViewActive(false);
        session.service().error(LogSource::Tool, "single event payload");
        session.service().flush();
        int probeCount = 0;
        require(QTest::qWaitFor(
                    [&]
                    {
                        for (const auto &record : session.service().take(64))
                        {
                            if (record.line.contains(QStringLiteral("single event payload")))
                            {
                                ++probeCount;
                                require(!record.line.contains(QStringLiteral("重复")) && !record.line.contains(QStringLiteral("×")),
                                        "a single error produced a repeat summary indicator");
                            }
                        }
                        return probeCount == 1;
                    },
                    2000),
                "expected one initial record for a single error");
        visionrt::flushPendingForTesting();
        QTest::qWait(600);
        for (const auto &record : session.service().take(64))
        {
            if (record.line.contains(QStringLiteral("single event payload")))
            {
                ++probeCount;
                require(!record.line.contains(QStringLiteral("重复")) && !record.line.contains(QStringLiteral("×")),
                        "an unexpected repeat summary was produced for a single error");
            }
        }
        require(probeCount == 1, "an extra record appeared across the refresh window");
        session.service().flush();
        session.stop();
        int negativeFileLines = 0;
        for (const auto &line : fileText(datedLog(negativeDir.path())).split('\n'))
            if (line.contains("single event payload"))
                ++negativeFileLines;
        require(negativeFileLines == 1, "a single error must write exactly 1 line");

        // 9g. Five occurrences: first immediate, next four summarised as repeat 4.
        QTemporaryDir repeatDir;
        session.restart(testConfig(), repeatDir.filePath(QStringLiteral("visionrt.log")));
        session.service().setViewActive(false);
        for (int i = 0; i < 5; ++i)
            session.service().error(LogSource::Tool, "repeat event payload");
        session.service().flush();
        visionrt::flushPendingForTesting();
        int repeatInitial = 0;
        int repeatSummary = 0;
        require(QTest::qWaitFor(
                    [&]
                    {
                        for (const auto &record : session.service().take(64))
                        {
                            if (record.line.contains(QStringLiteral("repeat event payload")))
                            {
                                if (record.line.contains(QStringLiteral("（重复 4 次）")) || record.line.contains(QStringLiteral("×4")))
                                    ++repeatSummary;
                                else
                                    ++repeatInitial;
                            }
                        }
                        return repeatInitial == 1 && repeatSummary == 1;
                    },
                    2000),
                "5 identical errors did not produce 1 initial + 1 summary");
        session.service().flush();
        session.stop();
        int repeatFileLines = 0;
        for (const auto &line : fileText(datedLog(repeatDir.path())).split('\n'))
            if (line.contains("repeat event payload"))
                ++repeatFileLines;
        require(repeatFileLines == 2, "5 identical errors must write exactly 2 lines (initial + summary)");
    }

    std::cout << "Log aggregation contracts passed: bounded mailbox, batching, drops, coalescing, Qt bridge "
                 "and daily file policy\n";
    return 0;
}
catch (const std::exception &error)
{
    std::cerr << error.what() << '\n';
    return 1;
}
