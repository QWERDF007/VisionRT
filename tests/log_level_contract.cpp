// Log level contract: the threshold is applied at the producer, before the record is formatted or
// queued. Records below it reach neither the file nor the window, a repeated error produces no
// coalesced summary while logging is off, and a change applies to new records only.
#include "log_test_support.h"

#include <QCoreApplication>
#include <QTemporaryDir>

#include <iostream>

using namespace log_test;
using visionrt::LogLevel;
using visionrt::LogSource;
using visionrt::LogService;

int main(int argc, char **argv)
{
    QCoreApplication application(argc, argv);

    // S04: Warn keeps Warn/Error/Critical and drops everything below it, in both channels.
    {
        QTemporaryDir directory;
        require(directory.isValid(), "temporary directory is unavailable");
        auto config         = testConfig();
        config.minimumLevel = LogLevel::Warn;
        ServiceSession session(config);
        session.start(directory.filePath(QStringLiteral("visionrt.log")));
        session.service().setViewActive(true);
        LogService::write(LogLevel::Trace, LogSource::System, QStringLiteral("跟踪记录"));
        LogService::write(LogLevel::Debug, LogSource::System, QStringLiteral("调试记录"));
        LogService::write(LogLevel::Info, LogSource::System, QStringLiteral("信息记录"));
        LogService::write(LogLevel::Warn, LogSource::System, QStringLiteral("警告记录"));
        LogService::write(LogLevel::Error, LogSource::System, QStringLiteral("错误记录"));
        LogService::write(LogLevel::Critical, LogSource::System, QStringLiteral("严重记录"));
        session.service().flush();

        const auto file = fileText(datedLog(directory.path()));
        require(!file.contains(QStringLiteral("跟踪记录")) && !file.contains(QStringLiteral("调试记录"))
                    && !file.contains(QStringLiteral("信息记录")),
                "a record below the threshold reached the file");
        require(file.contains(QStringLiteral("警告记录")) && file.contains(QStringLiteral("错误记录"))
                    && file.contains(QStringLiteral("严重记录")),
                "a record at or above the threshold did not reach the file");

        const auto rows = session.service().take(32);
        require(rows.size() == 3, "the window channel did not receive exactly the admitted records");
    }

    // S04: Off rejects everything, including the error coalescing summary.
    {
        QTemporaryDir directory;
        require(directory.isValid(), "temporary directory is unavailable");
        auto config         = testConfig();
        config.minimumLevel = LogLevel::Off;
        ServiceSession session(config);
        session.start(directory.filePath(QStringLiteral("visionrt.log")));
        LogService::write(LogLevel::Critical, LogSource::System, QStringLiteral("关闭时不应写入"));
        for (int repeat = 0; repeat < 3; ++repeat)
            LogService::write(LogLevel::Error, LogSource::System, QStringLiteral("关闭时重复错误"));
        session.service().flush();
        visionrt::flushPendingForTesting();
        session.service().flush();

        const auto file = fileText(datedLog(directory.path())).trimmed();
        require(file.isEmpty(), "logging off still produced a record or a repeat summary");
    }

    // S04: a saved level takes effect immediately, without rebuilding the pool or the sinks, and it
    // only affects records written after the change.
    {
        QTemporaryDir directory;
        require(directory.isValid(), "temporary directory is unavailable");
        ServiceSession session;
        session.start(directory.filePath(QStringLiteral("visionrt.log")));
        LogService::write(LogLevel::Info, LogSource::System, QStringLiteral("切换前信息"));
        session.service().setMinimumLevel(LogLevel::Error);
        require(session.service().minimumLevel() == LogLevel::Error, "the reported level did not change");
        LogService::write(LogLevel::Info, LogSource::System, QStringLiteral("切换后信息"));
        LogService::write(LogLevel::Error, LogSource::System, QStringLiteral("切换后错误"));
        session.service().flush();

        const auto file = fileText(datedLog(directory.path()));
        require(file.contains(QStringLiteral("切换前信息")), "a record written before the change was lost");
        require(!file.contains(QStringLiteral("切换后信息")), "a record written after the change was admitted");
        require(file.contains(QStringLiteral("切换后错误")), "a record at the new threshold was dropped");
    }

    std::cout << "The production threshold filters before formatting, applies to new records only and "
                 "never manufactures a summary while logging is off\n";
    return 0;
}
