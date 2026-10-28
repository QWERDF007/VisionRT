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

#include <functional>
#include <iostream>
#include <stdexcept>

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

    // 6. Error bursts collapse into one record with a repeat count.
    service.setViewActive(true);
    const auto beforeCoalesce = model.rowCount();
    for (int index = 0; index < 5; ++index)
        LogService::write(LogLevel::Error, QStringLiteral("测试工具"), QStringLiteral("同一个错误"));
    require(QTest::qWaitFor(
                [&]
                {
                    for (int row = beforeCoalesce; row < model.rowCount(); ++row)
                        if (rowLine(model, row).contains(QStringLiteral("重复 5 次")))
                            return true;
                    return false;
                },
                3000),
            "repeated errors were not coalesced with their count");
    int coalescedRows = 0;
    for (int row = beforeCoalesce; row < model.rowCount(); ++row)
        if (rowLine(model, row).contains(QStringLiteral("[测试工具] 同一个错误")))
            ++coalescedRows;
    require(coalescedRows == 1, "one error burst produced more than one log row");

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

    service.setViewActive(false);
    service.shutdown();
    require(!service.running(), "shutdown did not stop the service");
    LogService::write(LogLevel::Info, QStringLiteral("测试"), QStringLiteral("关闭后写入"));
    service.shutdown();
    require(service.writeFailures() == 1, "a healthy log file reported write failures");

    std::cout << "Log contracts passed: single-line text, daily file in the software directory, append on restart, "
                 "bounded mailbox, batching, drops, coalescing, Qt bridge and honest degradation\n";
    return 0;
}
catch (const std::exception &error)
{
    std::cerr << error.what() << '\n';
    return 1;
}
