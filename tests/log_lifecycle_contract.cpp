// Lifecycle contract for the log service: an unwritable location that degrades honestly, the
// default location, idempotent start/stop, handler restoration, and shutdown while producers are
// writing (including the instrumented lock interleavings). Text and aggregation live elsewhere.
#include "log_test_support.h"

#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QTemporaryDir>
#include <QTest>

#include <atomic>
#include <chrono>
#include <future>
#include <iostream>
#include <mutex>
#include <string>
#include <vector>

using namespace log_test;
using visionrt::LogLevel;
using visionrt::LogSource;
using visionrt::LogService;

namespace
{
#if defined(VISIONRT_LOG_TEST_HOOKS) && VISIONRT_LOG_TEST_HOOKS
// Clears the hooks even when a requirement throws, so a later scenario never inherits them.
struct HookReset
{
    ~HookReset() { visionrt::setLogTestHooks({}); }
};

int positionOf(const std::vector<std::string> &events, const std::string &name)
{
    for (int index = 0; index < static_cast<int>(events.size()); ++index)
        if (events[static_cast<std::size_t>(index)] == name)
            return index;
    return -1;
}

void runConcurrencyScenario(LogService &service, const visionrt::LogConfig &config, LogLevel level, const char *levelName)
{
    const std::string tag = std::string(levelName);
    Q_UNUSED(tag);
    HookReset hookReset;

    // Scenario A: the writer holds the shared lock; shutdown reaches its exclusive lock; the
    // coordination thread releases the writer; shutdown then drains the record.
    {
        service.shutdown();
        QTemporaryDir directory;
        directory.setAutoRemove(false);
        require(directory.isValid(), "scenario A fixture directory is invalid");
        require(service.start(config, directory.filePath(QStringLiteral("visionrt.log"))),
                "scenario A failed to start the service");
        service.setViewActive(true);

        std::promise<void> writerInLockPromise;
        auto               writerInLockFuture = writerInLockPromise.get_future().share();
        std::promise<void> allowWriterFinishPromise;
        auto               allowWriterFinishFuture = allowWriterFinishPromise.get_future().share();
        std::promise<void> shutdownBeforeLockPromise;
        auto               shutdownBeforeLockFuture = shutdownBeforeLockPromise.get_future().share();

        std::atomic<bool> unblockTriggered{false};
        auto              triggerUnblock = [&]
        {
            if (!unblockTriggered.exchange(true))
                allowWriterFinishPromise.set_value();
        };

        std::mutex               eventMutex;
        std::vector<std::string> events;
        auto                     recordEvent = [&](std::string event)
        {
            std::lock_guard<std::mutex> lock(eventMutex);
            events.push_back(std::move(event));
        };

        visionrt::setLogTestHooks({
            nullptr,
            [&](std::string_view source)
            {
                if (source == visionrt::sourceLabel(LogSource::System))
                {
                    recordEvent("writer_inside_lock");
                    writerInLockPromise.set_value();
                    require(allowWriterFinishFuture.wait_for(std::chrono::seconds(5)) == std::future_status::ready,
                            "scenario A writer timed out waiting for release");
                    recordEvent("writer_done_in_lock");
                }
            },
            [&]
            {
                recordEvent("shutdown_before_lock");
                shutdownBeforeLockPromise.set_value();
            },
            [&] { recordEvent("shutdown_inside_lock"); },
        });

        ProducerThreads threads;
        threads.spawn(
            [&]
            {
                const std::string payload = "in-lock payload " + tag;
                if (level == LogLevel::Error)
                    service.error(LogSource::System, payload);
                else
                    service.info(LogSource::System, payload);
            });
        threads.spawn(
            [&]
            {
                require(shutdownBeforeLockFuture.wait_for(std::chrono::seconds(5)) == std::future_status::ready,
                        "scenario A: shutdown never reached its lock point");
                recordEvent("coord_releasing_writer");
                triggerUnblock();
            });

        require(writerInLockFuture.wait_for(std::chrono::seconds(5)) == std::future_status::ready,
                "scenario A: the writer never entered the lock");

        // Shut down from the owning (main) thread so Qt timers are torn down on their thread.
        service.shutdown();
        threads.join();

        std::lock_guard<std::mutex> lock(eventMutex);
        const int writerIn   = positionOf(events, "writer_inside_lock");
        const int shutBefore = positionOf(events, "shutdown_before_lock");
        const int coord      = positionOf(events, "coord_releasing_writer");
        const int writerDone = positionOf(events, "writer_done_in_lock");
        const int shutInside = positionOf(events, "shutdown_inside_lock");
        require(writerIn >= 0 && shutBefore >= 0 && coord >= 0 && writerDone >= 0 && shutInside >= 0,
                "scenario A: one or more synchronization events did not occur");
        require(writerIn < shutBefore, "scenario A: the writer must enter the lock first");
        require(shutBefore < coord, "scenario A: shutdown must reach the lock before the writer is released");
        require(writerDone < shutInside, "scenario A: the writer must finish before the shutdown drain");
    }
    visionrt::setLogTestHooks({});

    // Scenario B: a writer paused before the lock observes the closed session and drops the record.
    {
        service.shutdown();
        QTemporaryDir directory;
        directory.setAutoRemove(false);
        require(directory.isValid(), "scenario B fixture directory is invalid");
        const QString file = directory.filePath(QStringLiteral("visionrt.log"));
        require(service.start(config, file), "scenario B failed to start the service");
        service.setViewActive(true);

        std::promise<void> writerBeforeLockPromise;
        auto               writerBeforeLockFuture = writerBeforeLockPromise.get_future().share();
        std::promise<void> allowWriterProceedPromise;
        auto               allowWriterProceedFuture = allowWriterProceedPromise.get_future().share();

        std::atomic<bool> unblocked{false};
        auto              triggerUnblock = [&]
        {
            if (!unblocked.exchange(true))
                allowWriterProceedPromise.set_value();
        };

        visionrt::setLogTestHooks({
            [&](std::string_view source)
            {
                if (source == visionrt::sourceLabel(LogSource::System))
                {
                    writerBeforeLockPromise.set_value();
                    require(allowWriterProceedFuture.wait_for(std::chrono::seconds(5)) == std::future_status::ready,
                            "scenario B writer timed out waiting for release");
                }
            },
            nullptr,
            nullptr,
            nullptr,
        });

        ProducerThreads threads;
        threads.spawn(
            [&]
            {
                const std::string payload = "late payload " + tag;
                if (level == LogLevel::Error)
                    service.error(LogSource::System, payload);
                else
                    service.info(LogSource::System, payload);
            });

        require(writerBeforeLockFuture.wait_for(std::chrono::seconds(5)) == std::future_status::ready,
                "scenario B: the writer never reached the admission hook");
        service.shutdown();
        require(!service.running(), "scenario B: the service is still running after shutdown");
        triggerUnblock();
        threads.join();

        require(!fileText(datedLog(directory.path())).contains(QString::fromStdString("late payload " + tag)),
                "scenario B: a late writer appended to the closed session file");
    }
    visionrt::setLogTestHooks({});

    // Scenario C: a writer suspended before the lock across a restart lands in the new session.
    {
        service.shutdown();
        QTemporaryDir oldDirectory;
        oldDirectory.setAutoRemove(false);
        require(oldDirectory.isValid(), "scenario C old fixture directory is invalid");
        require(service.start(config, oldDirectory.filePath(QStringLiteral("visionrt.log"))),
                "scenario C failed to start the old session");
        service.setViewActive(true);

        std::promise<void> writerWaitingPromise;
        auto               writerWaitingFuture = writerWaitingPromise.get_future().share();
        std::promise<void> allowWriterResumePromise;
        auto               allowWriterResumeFuture = allowWriterResumePromise.get_future().share();

        std::atomic<bool> unblocked{false};
        auto              triggerUnblock = [&]
        {
            if (!unblocked.exchange(true))
                allowWriterResumePromise.set_value();
        };

        visionrt::setLogTestHooks({
            [&](std::string_view source)
            {
                if (source == visionrt::sourceLabel(LogSource::System))
                {
                    writerWaitingPromise.set_value();
                    require(allowWriterResumeFuture.wait_for(std::chrono::seconds(5)) == std::future_status::ready,
                            "scenario C writer timed out waiting for resume");
                }
            },
            nullptr,
            nullptr,
            nullptr,
        });

        ProducerThreads threads;
        threads.spawn(
            [&]
            {
                const std::string payload = "restart payload " + tag;
                if (level == LogLevel::Error)
                    service.error(LogSource::System, payload);
                else
                    service.info(LogSource::System, payload);
            });

        require(writerWaitingFuture.wait_for(std::chrono::seconds(5)) == std::future_status::ready,
                "scenario C: the writer never reached the admission hook");
        service.shutdown();
        require(!service.running(), "scenario C: the old session is still running");

        QTemporaryDir newDirectory;
        newDirectory.setAutoRemove(false);
        require(newDirectory.isValid(), "scenario C new fixture directory is invalid");
        require(service.start(config, newDirectory.filePath(QStringLiteral("visionrt.log"))),
                "scenario C failed to start the new session");
        service.setViewActive(true);
        visionrt::setLogTestHooks({});

        triggerUnblock();
        threads.join();

        service.flush();
        visionrt::flushPendingForTesting();

        const std::string payload = "restart payload " + tag;
        int               delivered = 0;
        require(QTest::qWaitFor(
                    [&]
                    {
                        for (const auto &record : service.take(64))
                            if (record.line.contains(QString::fromStdString(payload)))
                                ++delivered;
                        return delivered == 1;
                    },
                    2000),
                "scenario C: the restarted session did not receive the record");
        service.flush();
        service.shutdown();

        require(!fileText(datedLog(oldDirectory.path())).contains(QString::fromStdString(payload)),
                "scenario C: the old session file received the late record");
        int newFileCount = 0;
        for (const auto &line : fileText(datedLog(newDirectory.path())).split('\n'))
            if (line.contains(QString::fromStdString(payload)))
                ++newFileCount;
        require(newFileCount == 1, "scenario C: the new session file must contain exactly one record");
    }
    visionrt::setLogTestHooks({});
}
#endif
} // namespace

int main(int argc, char **argv)
try
{
    QCoreApplication application(argc, argv);
    QTemporaryDir    directory;
    require(directory.isValid(), "log fixture directory is invalid");

    LogService &service = LogService::instance();
    const auto  config  = testConfig();

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
    LogService::write(LogLevel::Info, LogSource::System, QStringLiteral("降级会话"));
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
    require(service.start(config), "starting twice was not idempotent");
    require(service.fileSinkReady(), "the default log location is not writable");
    const auto basePath = service.baseFilePath();
    require(QFileInfo(basePath).absolutePath() == QCoreApplication::applicationDirPath() + QStringLiteral("/logs"),
            "the default log directory is not the software directory");
    const auto datedFile = QFileInfo(basePath).absolutePath() + QStringLiteral("/visionrt_")
                         + QDate::currentDate().toString(QStringLiteral("yyyy-MM-dd")) + QStringLiteral(".log");
    require(QFile::exists(datedFile), "the daily log file was not created next to the executable");

    // 3. A shutdown rejects later writes and is idempotent.
    service.setViewActive(false);
    service.shutdown();
    require(!service.running(), "shutdown did not stop the service");
    LogService::write(LogLevel::Info, LogSource::System, QStringLiteral("关闭后写入"));
    service.shutdown();
    require(service.writeFailures() == 1, "a closed healthy service reported additional write failures");

    // 4. Shutdown while producers are writing: the barrier lets every in-flight post finish.
    require(service.start(config), "the service refused to start for the concurrency check");
    {
        std::atomic<bool> stop{false};
        ProducerThreads   producers;
        for (int index = 0; index < 4; ++index)
            producers.spawn(
                [&]
                {
                    while (!stop.load(std::memory_order_relaxed))
                        LogService::write(LogLevel::Info, LogSource::System, QStringLiteral("关闭竞争"));
                });
        QTest::qWait(20);
        service.shutdown();
        stop.store(true, std::memory_order_relaxed);
        producers.join();
    }
    require(!service.running(), "shutdown under concurrent producers did not stop the service");

    // 5. Shutdown restores the handler that was installed before the service started.
    const auto baseline = qInstallMessageHandler(probeHandler);
    require(service.start(config), "the service refused to start for the handler restore check");
    service.shutdown();
    require(qInstallMessageHandler(baseline) == probeHandler, "shutdown left the Qt message bridge installed");
    qInstallMessageHandler(nullptr);
    require(service.start(config), "the service refused to restart for the default handler check");
    service.shutdown();
    require(qInstallMessageHandler(nullptr) == baseline, "shutdown did not restore the default Qt handler");

#if defined(VISIONRT_LOG_TEST_HOOKS) && VISIONRT_LOG_TEST_HOOKS
    // 6. Controlled lock interleavings, once for each level.
    runConcurrencyScenario(service, config, LogLevel::Info, "info");
    runConcurrencyScenario(service, config, LogLevel::Error, "error");
#endif

    std::cout << "Log lifecycle contracts passed: honest degradation, default location, idempotent "
                 "start/stop, handler restoration and shutdown under producers\n";
    return 0;
}
catch (const std::exception &error)
{
    std::cerr << error.what() << '\n';
    return 1;
}
