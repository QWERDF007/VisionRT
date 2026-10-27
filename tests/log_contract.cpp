// Contract for the log service: bounded queues, batching, drop accounting, error coalescing, Qt
// bridge mapping, file rotation and shutdown idempotence. The service is driven through its public
// producer API only.
#include "log/LogModel.h"
#include "log/LogService.h"

#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QTest>

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
} // namespace

int main(int argc, char **argv)
try
{
    QCoreApplication application(argc, argv);
    QTemporaryDir  directory;
    require(directory.isValid(), "log fixture directory is invalid");
    const auto logPath = directory.filePath(QStringLiteral("logs/visionrt.log"));

    LogConfig config;
    config.queueCapacity   = 1024;
    config.fileBytes       = 4096;
    config.fileCount       = 2;
    config.uiCapacity      = 32;
    config.maxMessageBytes = 64;
    config.flushSeconds    = 1;
    config.uiBatchIntervalMs = 20;
    config.uiBatchMaximum  = 8;

    auto &service = LogService::instance();
    require(service.start(config, logPath), "log service refused to start on its own thread");
    require(service.running(), "log service did not report running");
    require(LogService::instance().start(config, logPath), "starting twice was not idempotent");

    LogModel model;
    model.setCapacity(static_cast<std::size_t>(config.uiCapacity));

    // Hidden window: records stay in the bounded mailbox and nothing reaches the model.
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
    bool lastVisible = false;
    for (int row = 0; row < model.rowCount(); ++row)
        if (model.data(model.index(row, 0), LogModel::MessageRole).toString() == QStringLiteral("隐藏批次 11"))
            lastVisible = true;
    require(lastVisible, "the visible window lost the last record of the hidden batch");
    require(!batches.isEmpty(), "visible batches were not reported");
    for (const auto &arguments : batches)
    {
        const auto records = arguments.front().value<QVector<LogRecord>>();
        require(records.size() <= config.uiBatchMaximum,
                "the window received more than one bounded batch in a single delivery");
    }

    // Overflow uses overrun_oldest and reports one summary for the discarded records.
    service.setViewActive(false);
    for (int index = 0; index < static_cast<int>(config.uiCapacity) * 3; ++index)
        LogService::write(LogLevel::Info, QStringLiteral("测试"),
                          QStringLiteral("溢出 %1").arg(index));
    QTest::qWait(100);
    QSignalSpy overflow(&service, &LogService::recordsAvailable);
    service.setViewActive(true);
    require(QTest::qWaitFor(
                [&]
                {
                    for (int row = 0; row < model.rowCount(); ++row)
                        if (model.data(model.index(row, 0), LogModel::MessageRole).toString().contains(
                                QStringLiteral("丢弃")))
                            return true;
                    return false;
                },
                3000),
            "mailbox overflow was not reported to the window");
    require(service.droppedRecords() > 0, "discarded records were not counted");

    // Error bursts collapse into one record with a repeat count, keeping the first and last time.
    service.setViewActive(true);
    const auto beforeCoalesce = model.rowCount();
    for (int index = 0; index < 5; ++index)
        LogService::write(LogLevel::Error, QStringLiteral("测试工具"), QStringLiteral("同一个错误"));
    require(QTest::qWaitFor(
                [&]
                {
                    for (int row = beforeCoalesce; row < model.rowCount(); ++row)
                    {
                        const auto message = model.data(model.index(row, 0), LogModel::MessageRole).toString();
                        if (message.contains(QStringLiteral("重复 5 次")))
                            return true;
                    }
                    return false;
                },
                3000),
            "repeated errors were not coalesced with their count");
    int coalescedRows = 0;
    for (int row = beforeCoalesce; row < model.rowCount(); ++row)
        if (model.data(model.index(row, 0), LogModel::MessageRole).toString().startsWith(QStringLiteral("同一个错误")))
            ++coalescedRows;
    require(coalescedRows == 1, "one error burst produced more than one log row");

    // Long messages are truncated in the log only.
    const auto oversized = QString(200, QLatin1Char('x')).toStdString();
    LogService::write(LogLevel::Info, "测试", oversized);
    require(QTest::qWaitFor(
                [&]
                {
                    for (int row = 0; row < model.rowCount(); ++row)
                        if (model.data(model.index(row, 0), LogModel::MessageRole).toString().endsWith(QStringLiteral("…")))
                            return true;
                    return false;
                },
                2000),
            "an oversized log message was not truncated in the window");

    // The Qt bridge maps levels and categories and does not recurse.
    qWarning("日志桥接警告");
    require(QTest::qWaitFor(
                [&]
                {
                    for (int row = 0; row < model.rowCount(); ++row)
                        if (model.data(model.index(row, 0), LogModel::MessageRole).toString()
                                == QStringLiteral("日志桥接警告"))
                            return true;
                    return false;
                },
                2000),
            "qWarning did not reach the log window");
    bool bridged = false;
    for (int row = 0; row < model.rowCount(); ++row)
        if (model.data(model.index(row, 0), LogModel::MessageRole).toString() == QStringLiteral("日志桥接警告"))
            bridged = model.data(model.index(row, 0), LogModel::LevelRole).toString() == QStringLiteral("警告");
    require(bridged, "Qt warning level was not mapped to the log warning level");

    // The window trims its own history; the file keeps rotating instead of growing without bound.
    for (int index = 0; index < 80; ++index)
        LogService::write(LogLevel::Info, QStringLiteral("文件"),
                          QStringLiteral("轮转测试行 %1 0123456789").arg(index));
    service.flush();
    require(model.rowCount() <= config.uiCapacity, "the window model exceeded its capacity");
    QTest::qWait(200);
    require(QFile::exists(logPath), "the rotated log file was not created");
    const auto rotated = QFileInfo(logPath).absolutePath() + QStringLiteral("/visionrt.1.log");
    require(QFileInfo::exists(rotated), "the log did not rotate at its configured size");
    require(fileText(logPath).size() > 0, "the active log file stayed empty");
    require(QFileInfo(rotated).size() <= static_cast<qint64>(config.fileBytes) * 2,
            "rotation ignored the configured size bound");

    service.setViewActive(false);
    service.shutdown();
    require(!service.running(), "shutdown did not stop the service");
    LogService::write(LogLevel::Info, QStringLiteral("测试"), QStringLiteral("关闭后写入"));
    service.shutdown();
    require(service.writeFailures() == 0, "a healthy log file reported write failures");

    std::cout << "Log contracts passed: bounded mailbox, batching, drops, coalescing, Qt bridge and rotation\n";
    return 0;
}
catch (const std::exception &error)
{
    std::cerr << error.what() << '\n';
    return 1;
}
